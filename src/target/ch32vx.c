/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2022-2025 1BitSquared <info@1bitsquared.com>
 * Written by Rafael Silva <perigoso@riseup.net>
 * Modified by Rachel Mant <git@dragonmux.network>
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

/* This file implements RISC-V CH32Vx target specific functions */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "buffer_utils.h"

/*
 * IDCODE register
 * [31:16] - REVID
 * [15:0]  - DEVID
 */
#define CH32V003X_IDCODE 0x1ffff7c4U

/* IDCODE register */
#define CH32VX_IDCODE               0x1ffff704U
#define CH32VX_IDCODE_MASK          0x0ffffff0f
#define CH32VX_IDCODE_FAMILY_OFFSET 20U
#define CH32VX_IDCODE_FAMILY_MASK   (0xfffU << CH32VX_IDCODE_FAMILY_OFFSET)

#define CH32V203_IDCODE_FAMILY 0x203U
#define CH32V208_IDCODE_FAMILY 0x208U
#define CH32V305_IDCODE_FAMILY 0x305U
#define CH32V303_IDCODE_FAMILY 0x303U
#define CH32V307_IDCODE_FAMILY 0x307U

/* Electronic Signature (ESIG) registers */
#define CH32VX_ESIG_FLASH_CAP 0x1ffff7e0U /* Flash capacity register, 16 bits, KiB units */
#define CH32VX_ESIG_UID1      0x1ffff7e8U /* Unique ID register, bits 0:31 */
#define CH32VX_ESIG_UID2      0x1ffff7ecU /* Unique ID register, bits 32:63 */
#define CH32VX_ESIG_UID3      0x1ffff7f0U /* Unique ID register, bits 64:95 */

static bool ch32vx_uid_cmd(target_s *target, int argc, const char **argv);

const command_s ch32vx_cmd_list[] = {
	{"uid", ch32vx_uid_cmd, "Prints 96 bit unique id"},
	{NULL, NULL, NULL},
};

static size_t ch32vx_read_flash_size(target_s *const target)
{
	return target_mem32_read16(target, CH32VX_ESIG_FLASH_CAP) * 1024U;
}

static void ch32vx_read_uid(target_s *const target, uint8_t *const uid)
{
	for (size_t uid_reg_offset = 0; uid_reg_offset < 12U; uid_reg_offset += 4U)
		write_be4(uid, uid_reg_offset, target_mem32_read32(target, CH32VX_ESIG_UID1 + uid_reg_offset));
}

static bool ch32vx_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	(void)flash;
	(void)addr;
	(void)len;
	return false;
}

static bool ch32vx_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	(void)flash;
	(void)dest;
	(void)src;
	(void)len;
	return false;
}

static void ch32vx_add_flash(target_s *const target, const uint32_t addr, const size_t length, const size_t erasesize)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	flash->start = addr;
	flash->length = length;
	flash->blocksize = erasesize;
	flash->writesize = 1024U;
	flash->erase = ch32vx_flash_erase;
	flash->write = ch32vx_flash_write;
	flash->erased = 0xff; // actually 0xe339e339
	target_add_flash(target, flash);
}

bool ch32v003x_probe(target_s *const target)
{
	const uint32_t idcode = target_mem32_read32(target, CH32V003X_IDCODE);

	switch (idcode & CH32VX_IDCODE_MASK) {
	case 0x00300500U: /* CH32V003F4P6 */
	case 0x00310500U: /* CH32V003F4U6 */
	case 0x00320500U: /* CH32V003A4M6 */
	case 0x00330500U: /* CH32V003J4M6 */
		break;
	default:
		DEBUG_INFO("Unrecognized CH32V003x IDCODE: 0x%08" PRIx32 "\n", idcode);
		return false;
		break;
	}

	target->driver = "CH32V003";

#ifndef DEBUG_INFO_IS_NOOP
	const size_t flash_size = ch32vx_read_flash_size(target);
	DEBUG_INFO("CH32V003x flash size: %" PRIu32 "\n", (uint32_t)flash_size);
#endif

	target->part_id = idcode;

	target_add_commands(target, ch32vx_cmd_list, "CH32Vx");

	return true;
}

bool ch32vx_probe(target_s *const target)
{
	const uint32_t idcode = target_mem32_read32(target, CH32VX_IDCODE);

	switch (idcode & CH32VX_IDCODE_MASK) {
	case 0x20310500U: /* CH32V203C8T6 */
	case 0x30330504U: /* CH32V303CBT6 */
	case 0x30320504U: /* CH32V303RBT6 */
	case 0x30310504U: /* CH32V303RCT6 */
	case 0x30300504U: /* CH32V303VCT6 */
	case 0x30520508U: /* CH32V305FBP6 */
	case 0x30500508U: /* CH32V305RBT6 */
	case 0x30730508U: /* CH32V307WCU6 */
	case 0x30720508U: /* CH32V307FBP6 */
	case 0x30710508U: /* CH32V307RCT6 */
	case 0x30700508U: /* CH32V307VCT6 */
		break;
	default:
		DEBUG_INFO("Unrecognized CH32Vx IDCODE: 0x%08" PRIx32 "\n", idcode);
		return false;
		break;
	}

	const uint16_t family = (idcode & CH32VX_IDCODE_FAMILY_MASK) >> CH32VX_IDCODE_FAMILY_OFFSET;
	switch (family) {
	case CH32V203_IDCODE_FAMILY:
		target->driver = "CH32V203";
		break;
	case CH32V303_IDCODE_FAMILY:
		target->driver = "CH32V303";
		break;
	case CH32V305_IDCODE_FAMILY:
		target->driver = "CH32V305";
		break;
	case CH32V307_IDCODE_FAMILY:
		target->driver = "CH32V307";
		break;
	default:
		return false;
		break;
	}

	const size_t flash_size = ch32vx_read_flash_size(target);
	DEBUG_INFO("CH32Vx flash size: %" PRIu32 "\n", (uint32_t)flash_size);

	target->part_id = idcode;

	target_add_commands(target, ch32vx_cmd_list, "CH32Vx");
	ch32vx_add_flash(target, 0x0, flash_size, 4096U);
	ch32vx_add_flash(target, 0x0 + flash_size, 163840, 4096U);
	target_add_ram32(target, 0x20000000, 20480U);

	return true;
}

/* Reads the 96 bit unique id */
static bool ch32vx_uid_cmd(target_s *const target, const int argc, const char **const argv)
{
	(void)argc;
	(void)argv;

	uint8_t uid[12U];
	ch32vx_read_uid(target, uid);

	tc_printf(target, "Unique id: 0x");
	for (size_t i = 0U; i < sizeof(uid); i++)
		tc_printf(target, "%02x", uid[i]);
	tc_printf(target, "\n");

	return true;
}
