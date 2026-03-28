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

#define MDR32VF_RSTCLK_BASE      0x40020000U
#define MDR32VF_RSTCLK_PER2CLOCK (MDR32VF_RSTCLK_BASE + 0x1cU)

#define MDR32VF_RSTCLK_PER2CLOCK_FLASH  (1 << 3)
#define MDR32VF_RSTCLK_PER2CLOCK_RSTCLK (1 << 4)

#define FLASH_REG_BASE 0x40018000U
#define FLASH_CMD      (FLASH_REG_BASE + 0x00U)
#define FLASH_ADR      (FLASH_REG_BASE + 0x04U)
#define FLASH_DI       (FLASH_REG_BASE + 0x08U)
#define FLASH_DO       (FLASH_REG_BASE + 0x0cU)
#define FLASH_KEY      (FLASH_REG_BASE + 0x10U)

#define MDR32VF_FLASH_KEY 0x8aaa5551U

#define FLASH_CMD_TMR    (1 << 14) /* Test Mode Reset, always write as one */
#define FLASH_CMD_NVSTR  (1 << 13)
#define FLASH_CMD_PROG   (1 << 12)
#define FLASH_CMD_MAS1   (1 << 11)
#define FLASH_CMD_ERASE  (1 << 10)
#define FLASH_CMD_IFREN  (1 << 9)
#define FLASH_CMD_SE     (1 << 8)
#define FLASH_CMD_YE     (1 << 7)
#define FLASH_CMD_XE     (1 << 6)
#define FLASH_DELAY_MASK (7 << 3)
#define FLASH_CMD_CON    (1 << 0) /* Detach from instruction bus */
//#define FLASH_CMD_RD     (1 << 2)
//#define FLASH_CMD_WR     (1 << 1)

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
	/* Memory layer returns true for errors */
	bool mem_error = target_mem32_write32(target, FLASH_KEY, MDR32VF_FLASH_KEY);
	if (mem_error) {
		DEBUG_ERROR("%s failed\n", __func__);
	}
	/* Flash layer returns true for success */
	return !mem_error;
}

static bool mdr32vf_flash_prepare(target_flash_s *const target_flash)
{
	target_s *target = target_flash->t;
	uint32_t per2_clock = target_mem32_read32(target, MDR32VF_RSTCLK_PER2CLOCK);
	if ((per2_clock & MDR32VF_RSTCLK_PER2CLOCK_RSTCLK) == 0) {
		DEBUG_ERROR("%s: Peripheral clock for RST_CLK is missing. Please reset.\n", __func__);
		return false;
	}
	/* Enable peripheral clock to FLASH registers */
	if ((per2_clock & MDR32VF_RSTCLK_PER2CLOCK_FLASH) == 0) {
		per2_clock |= MDR32VF_RSTCLK_PER2CLOCK_FLASH;
		target_mem32_write32(target, MDR32VF_RSTCLK_PER2CLOCK, per2_clock);
	}
	return mdr32vf_flash_unlock(target);
}

static bool mdr32vf_flash_done(target_flash_s *const target_flash)
{
	target_s *target = target_flash->t;
	const uint32_t mem_error = target_mem32_write32(target, FLASH_KEY, 0);
	return !mem_error;
}

static bool mdr32vf_flash_erase(target_flash_s *flash, const target_addr_t addr, const size_t len)
{
	target_s *target = flash->t;
	/* Reconnect flash to the register interface */
	uint32_t flash_cmd = target_mem32_read32(target, FLASH_CMD);
	const uint32_t flash_delay = flash_cmd & FLASH_DELAY_MASK;
	flash_cmd = FLASH_CMD_CON | FLASH_CMD_TMR | flash_delay;
	target_mem32_write32(target, FLASH_CMD, flash_cmd);

	for (uint32_t offset = addr; offset < addr + len; offset += flash->blocksize) {
		target_mem32_write32(target, FLASH_ADR, offset);
		flash_cmd |= FLASH_CMD_XE | FLASH_CMD_ERASE;
		target_mem32_write32(target, FLASH_CMD, flash_cmd);
		/* Delay 5us */
		flash_cmd |= FLASH_CMD_NVSTR;
		target_mem32_write32(target, FLASH_CMD, flash_cmd);
		platform_delay(40); /* Delay 40ms for sector erase, polling not possible */
		flash_cmd &= ~FLASH_CMD_ERASE;
		target_mem32_write32(target, FLASH_CMD, flash_cmd);
		/* Delay 5us */
		flash_cmd &= ~(FLASH_CMD_XE | FLASH_CMD_NVSTR);
		target_mem32_write32(target, FLASH_CMD, flash_cmd);
		/* Delay 10us */
	}

	/* Reconnect flash to instruction bus */
	target_mem32_write32(target, FLASH_CMD, flash_delay);
	return true;
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
