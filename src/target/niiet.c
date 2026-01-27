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
 * This file implements NIIET K1921VG015 target specific functions
 * for detecting the device, providing the XML memory map
 * ~~and Flash memory programming.~~
 *
 * References:
 * K1921VG015 User Manual "UM_K1921VG015.pdf"
 *   https://gitflic.ru/project/niiet/niiet/blob/raw?file=doc%2F%D0%9A1921VG015%2FUM_K1921VG015.pdf&inline=false
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "riscv_debug.h"
#include "jep106.h"
#include "gdb_packet.h"

#define PMUSYS_CHIPID_K1921VG015 0xdeadbee0
#define PMUSYS_BASE              0x3000f000
#define PMUSYS_SERVCTL           (PMUSYS_BASE + 0x104)
#define PMUSYS_CHIPID            (PMUSYS_BASE + 0x100)

#define K1921VG015_SRAM0_AHB_BASE 0x40000000
#define K1921VG015_SRAM1_AHB_BASE 0x10000000

#define K1921VG015_SRAM0_SIZE 0x40000
#define K1921VG015_SRAM1_SIZE 0x10000

#define MFLASH_BANK_ADDR  0x80000000
#define MFLASH_PAGE_SIZE  4096
#define MFLASH_PAGE_COUNT 256

bool niiet_probe(target_s *target)
{
	uint32_t chipid = target_mem32_read32(target, PMUSYS_CHIPID);
	chipid &= 0xfffffff0U; /* Mask revision bits */
	if (chipid != PMUSYS_CHIPID_K1921VG015)
		return false;

	target->driver = "K1921VG015";
	/* SRAM0 also has TCM-A & TCM-B alias. */
	target_add_ram32(target, K1921VG015_SRAM0_AHB_BASE, K1921VG015_SRAM0_SIZE);
	target_add_ram32(target, K1921VG015_SRAM1_AHB_BASE, K1921VG015_SRAM1_SIZE);
	/* Flash driver: TODO */
	target_add_ram32(target, MFLASH_BANK_ADDR, MFLASH_PAGE_SIZE * MFLASH_PAGE_COUNT);
	return true;
}
