/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 * Copyright (C) 2022  1bitsquared - Rachel Mant <git@dragonmux.network>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* This file implements JTAG protocol support.  Provides functionality
 * to detect devices on the scan chain and read their IDCODEs.
 * It depends on the low-level function provided by the platform's jtagtap.c.
 */

#include "general.h"
#include "jtagtap.h"
#include "jtag_scan.h"
#include "target.h"
#include "adiv5.h"
#include "jtag_devs.h"

jtag_dev_s jtag_devs[JTAG_MAX_DEVS + 1U];
uint32_t jtag_dev_count = 0;

/* bucket of ones for don't care TDI */
const uint8_t ones[8] = {0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU, 0xffU};

static bool jtag_read_idcodes();
static void jtag_display_idcodes();

#if PC_HOSTED == 0
void jtag_add_device(const uint32_t dev_index, const jtag_dev_s *jtag_dev)
{
	if (dev_index == 0)
		memset(&jtag_devs, 0, sizeof(jtag_devs));
	memcpy(&jtag_devs[dev_index], jtag_dev, sizeof(jtag_dev_s));
	jtag_dev_count = dev_index + 1U;
}
#endif

/*
 * Scan JTAG chain for devices, store IR length and IDCODE (if present).
 * Reset TAP state machine.
 * Select Shift-IR state.
 * Each device is assumed to shift out IR at 0x01. (this may not always be true)
 * Shift in ones until we read two consecutive ones, then we have shifted out the
 * 	IRs of all devices.
 *
 * After this process all the IRs are loaded with the BYPASS command.
 * Select Shift-DR state.
 * Shift in ones and count zeros shifted out. Should be one for each device.
 * Check this against device count obtained by IR scan above.
 *
 * Reset the TAP state machine again. This should load all IRs with IDCODE.
 * For each device, shift out one bit. If this is zero IDCODE isn't present,
 *	continue to next device. If this is one shift out the remaining 31 bits
 *	of the IDCODE register.
 */
uint32_t jtag_scan(const uint8_t *irlens)
{
	/* Free the device list if any, and clean state ready */
	target_list_free();

	jtag_dev_count = 0;
	memset(&jtag_devs, 0, sizeof(jtag_devs));

	/*
	 * Initialise the JTAG backend if it's not already
	 * This will automatically do the SWD-to-JTAG sequence just in case we've got any SWJ-DP's in chain
	 */
	DEBUG_INFO("Resetting TAP\n");
#if PC_HOSTED == 1
	if (!platform_jtagtap_init()) {
		DEBUG_WARN("JTAG not available\n");
		return 0;
	}
#else
	jtagtap_init();
#endif

	/* Start by reading out the ID Codes for all the devices on the chain */
	if (!jtag_read_idcodes())
		return 0;

	if (irlens) {
		DEBUG_WARN("Given list of IR lengths, skipping probe\n");
		DEBUG_INFO("Change state to Shift-IR\n");
		jtagtap_shift_ir();

		size_t device = 0;
		for (size_t prescan = 0; device <= JTAG_MAX_DEVS && jtag_devs[device].ir_len <= JTAG_MAX_IR_LEN; ++device) {
			if (irlens[device] == 0)
				break;

			uint32_t irout = 0;
			jtag_proc.jtagtap_tdi_tdo_seq((uint8_t *)&irout, 0, ones, irlens[device]);

			/* IEEE 1149.1 requires the first bit to be a 1, but not all devices conform (see #1130 on GH) */
			if (!(irout & 1U))
				DEBUG_WARN("check failed: IR[0] != 1\n");

			jtag_devs[device].ir_len = irlens[device];
			jtag_devs[device].ir_prescan = prescan;
			jtag_devs[device].jd_dev = device;
			prescan += irlens[device];
		}
		jtag_dev_count = device;
	} else {
		DEBUG_INFO("Change state to Shift-IR\n");
		jtagtap_shift_ir();

		DEBUG_INFO("Scanning out IRs\n");
		/* IEEE 1149.1 requires the first bit to be a 1, but not all devices conform (see #1130 on GH) */
		if (!jtag_proc.jtagtap_next(false, true))
			DEBUG_WARN("jtag_scan: Sanity check failed: IR[0] shifted out as 0\n");

		jtag_devs[0].ir_len = 1;
		size_t device = 0;
		for (size_t prescan = 1; device <= JTAG_MAX_DEVS && jtag_devs[device].ir_len <= JTAG_MAX_IR_LEN;) {
			/* If we read out a '1' from TDO, we're at the end of the current device and the start of the next */
			if (jtag_proc.jtagtap_next(false, true)) {
				/* If the device was not actually a new device, exit */
				if (jtag_devs[device].ir_len == 1)
					break;
				++device;
				/* Set up the next device */
				jtag_devs[device].ir_len = 1;
				jtag_devs[device].ir_prescan = prescan;
				jtag_devs[device].jd_dev = device;
			} else
				/* Otherwise we have another bit in this device's IR */
				++jtag_devs[device].ir_len;
			++prescan;
		}
		jtag_dev_count = device;

		if (jtag_dev_count > JTAG_MAX_DEVS) {
			DEBUG_WARN("jtag_scan: Maximum device count exceeded\n");
			jtag_dev_count = 0;
			return 0;
		}

		if (jtag_devs[jtag_dev_count].ir_len > JTAG_MAX_IR_LEN) {
			DEBUG_WARN("jtag_scan: Maximum IR length exceeded\n");
			jtag_dev_count = 0;
			return 0;
		}
	}

	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(true, true);
	jtagtap_return_idle(1);

	/* All devices should be in BYPASS now */

	/* Count device on chain */
	DEBUG_INFO("Change state to Shift-DR\n");
	jtagtap_shift_dr();
	size_t device = 0;
	for (; !jtag_proc.jtagtap_next(false, true) && device <= jtag_dev_count; ++device)
		jtag_devs[device].dr_postscan = jtag_dev_count - device - 1U;

	if (device != jtag_dev_count) {
		DEBUG_WARN("jtag_scan: Sanity check failed: BYPASS dev count doesn't match IR scan\n");
		jtag_dev_count = 0;
		return 0;
	}

	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(true, true);
	jtagtap_return_idle(1);
	if (!jtag_dev_count)
		return 0;

	/* Fill in the ir_postscan fields */
	for (size_t device = jtag_dev_count - 1U; device > 0; --device)
		jtag_devs[device - 1U].ir_postscan = jtag_devs[device].ir_postscan + jtag_devs[device].ir_len;

#if PC_HOSTED == 1
	/*Transfer needed device information to firmware jtag_devs */
	for (size_t device = 0; device < jtag_dev_count; ++device)
		platform_add_jtag_dev(device, jtag_devs + device);
#endif

	jtag_display_idcodes();

	/* Check for known devices and handle accordingly */
	for (size_t device = 0; device < jtag_dev_count; device++) {
		for (size_t descr = 0; dev_descr[descr].idcode; descr++) {
			if ((jtag_devs[device].jd_idcode & dev_descr[descr].idmask) == dev_descr[descr].idcode) {
				jtag_devs[device].current_ir = UINT32_MAX;
				/* Save description in table */
				jtag_devs[device].jd_descr = dev_descr[descr].descr;
				/* Call handler to initialise/probe device further */
				if (dev_descr[descr].handler)
					dev_descr[descr].handler(device);
				break;
			}
		}
	}

	return jtag_dev_count;
}

static bool jtag_read_idcodes()
{
	/* Reset the chain ready and transition to Shift-DR */
	jtag_proc.jtagtap_reset();
	DEBUG_INFO("Change state to Shift-DR\n");
	jtagtap_shift_dr();

	DEBUG_INFO("Scanning out ID codes\n");
	size_t device = 0;
	/* Try scanning to one ID past the end of the chain */
	for (; device <= JTAG_MAX_DEVS; ++device) {
		/* Try to read out 32 bits, while shifting in 1's */
		uint32_t idcode = 0;
		jtag_proc.jtagtap_tdi_tdo_seq((uint8_t *)&idcode, false, ones, 32);
		/* If the IDCode read is all 1's, we've reached the end */
		if (idcode == 0xffffffffU)
			break;
		/* Check if the max suported chain length is exceeded */
		if (device == JTAG_MAX_DEVS) {
			DEBUG_WARN("jtag_scan: Maximum chain length exceeded\n");
			jtag_dev_count = 0;
			return false;
		}
		/* We got a valid device, add it to the set */
		jtag_devs[device].jd_idcode = idcode;
		jtag_devs[device].jd_dev = device;
	}

	/* Well, it worked, so clean up and do housekeeping */
	DEBUG_INFO("Return to Run-Test/Idle\n");
	jtag_proc.jtagtap_next(true, true);
	jtagtap_return_idle(1);
	jtag_dev_count = device;
	return true;
}

static void jtag_display_idcodes()
{
#if ENABLE_DEBUG
	for (size_t device = 0; device < jtag_dev_count; ++device) {
		const char *description = "Unknown";
		for (size_t idx = 0; dev_descr[idx].idcode; ++idx) {
			if ((jtag_devs[device].jd_idcode & dev_descr[idx].idmask) == dev_descr[idx].idcode) {
				if (dev_descr[idx].descr)
					description = dev_descr[idx].descr;
				break;
			}
		}
		DEBUG_INFO("ID code 0x%08" PRIx32 ": %s\n", jtag_devs[device].jd_idcode, description);
	}
#endif
}

void jtag_dev_write_ir(const uint8_t dev_index, const uint32_t ir)
{
	jtag_dev_s *device = &jtag_devs[dev_index];
	/* If the request would duplicate work already done, do nothing */
	if (ir == device->current_ir)
		return;

	/* Set all the other devices IR's to being in bypass */
	for (size_t device = 0; device < jtag_dev_count; device++)
		jtag_devs[device].current_ir = UINT32_MAX;
	device->current_ir = ir;

	/* Do the work to make the scanchain match the jtag_devs state */
	jtagtap_shift_ir();
	jtag_proc.jtagtap_tdi_seq(false, ones, device->ir_prescan);
	jtag_proc.jtagtap_tdi_seq(!device->ir_postscan, (const uint8_t *)&ir, device->ir_len);
	jtag_proc.jtagtap_tdi_seq(true, ones, device->ir_postscan);
	jtagtap_return_idle(1);
}

void jtag_dev_shift_dr(const uint8_t dev_index, uint8_t *data_out, const uint8_t *data_in, const size_t clock_cycles)
{
	jtag_dev_s *device = &jtag_devs[dev_index];
	jtagtap_shift_dr();
	jtag_proc.jtagtap_tdi_seq(false, ones, device->dr_prescan);
	if (data_out)
		jtag_proc.jtagtap_tdi_tdo_seq(
			(uint8_t *)data_out, !device->dr_postscan, (const uint8_t *)data_in, clock_cycles);
	else
		jtag_proc.jtagtap_tdi_seq(!device->dr_postscan, (const uint8_t *)data_in, clock_cycles);
	jtag_proc.jtagtap_tdi_seq(true, ones, device->dr_postscan);
	jtagtap_return_idle(1);
}
