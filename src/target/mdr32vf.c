/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2026 1BitSquared <info@1bitsquared.com>
 * Written by ALTracer <11005378+ALTracer@users.noreply.github.com>
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

/*
 * This file implements Milandr MDR32F02FI target specific functions
 * for detecting the device, providing the XML memory map
 * ~~and Flash memory programming.~~
 * Also known as MDR187. Based on RV32IMC CloudBEAR BM-310S core.
 * Replaced by MDR1206FI (aka MDR217) based on RV32IMCNZX BM-310S0,
 * and MDR1206AFI (aka MDR215).
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "riscv_debug.h"

#define MDR32F02FI_SRAM_TCMA_BASE 0x80000000U
#define MDR32F02FI_SRAM_TCMB_BASE 0x80010000U
#define MDR32F02FI_SRAM_AHB_BASE  0x20000000U

#define MDR32F02FI_FLASH_BASE 0x10000000U

static bool mdr32vf_flash_prepare(target_flash_s *flash);
static bool mdr32vf_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len);
static bool mdr32vf_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len);
static bool mdr32vf_flash_done(target_flash_s *flash);
static bool mdr32vf_mass_erase(target_flash_s *flash, platform_timeout_s *print_progress);

static void mdr32vf_add_flash(
	target_s *const target, const target_addr32_t addr, const size_t length, const size_t erasesize)
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
	flash->erased = 0xff;
	flash->prepare = mdr32vf_flash_prepare;
	flash->erase = mdr32vf_flash_erase;
	flash->mass_erase = mdr32vf_mass_erase;
	flash->write = mdr32vf_flash_write;
	flash->done = mdr32vf_flash_done;
	target_add_flash(target, flash);
}

bool mdr32f02fi_probe(target_s *const target)
{
	const riscv_hart_s *hart = (riscv_hart_s *)target->priv;
	if (hart->extensions == (RV_ISA_EXT_INTEGER | RV_ISA_EXT_MUL_DIV_INT | RV_ISA_EXT_COMPRESSED | (1U << 20U)))
		target->driver = "MDR32F02FI"; // RV32IMC with User mode 0x00101104

	/* SRAM: 16+64+32=112 KiB */
	target_add_ram32(target, MDR32F02FI_SRAM_AHB_BASE, 16U * 1024U);
	target_add_ram32(target, MDR32F02FI_SRAM_TCMA_BASE, 64U * 1024U);
	target_add_ram32(target, MDR32F02FI_SRAM_TCMB_BASE, 32U * 1024U);
	/* Flash: 256 KiB in 64 sectors of 4 KiB, driver: MDR187 */
	mdr32vf_add_flash(target, MDR32F02FI_FLASH_BASE, 256U * 1024U, 4096U);
	return true;
}

static bool mdr32vf_flash_unlock(target_s *const target)
{
	(void)target;
	return false;
}

static bool mdr32vf_flash_prepare(target_flash_s *const target_flash)
{
	target_s *target = target_flash->t;
	return mdr32vf_flash_unlock(target);
}

static bool mdr32vf_flash_done(target_flash_s *const target_flash)
{
	(void)target_flash;
	return false;
}

static bool mdr32vf_flash_erase(target_flash_s *flash, target_addr_t addr, size_t len)
{
	(void)flash;
	(void)addr;
	(void)len;
	return false;
}

static bool mdr32vf_flash_write(target_flash_s *flash, target_addr_t dest, const void *src, size_t len)
{
	(void)flash;
	(void)dest;
	(void)src;
	(void)len;
	return false;
}

static bool mdr32vf_mass_erase(target_flash_s *flash, platform_timeout_s *print_progress)
{
	(void)flash;
	(void)print_progress;
	return false;
}
