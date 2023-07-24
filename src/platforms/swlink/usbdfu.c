/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2013 Gareth McMullin <gareth@blacksphere.co.nz>
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

#include <string.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/scb.h>

#include "usbdfu.h"
#include "platform.h"
#include "platform_common.h"

uintptr_t app_address = 0x08002000U;
static uint32_t rev;
static int dfu_activity_counter;

void dfu_detach(void)
{
	platform_detach_usb();
	scb_reset_system();
}

int main(void)
{
	/* Map SWJ back for DFU debugging (BMP swlink unmaps them) */
	gpio_primary_remap(AFIO_MAPR_SWJ_CFG_FULL_SWJ, 0U);

	/* Check the force bootloader pin*/
	bool normal_boot = 0;
	rev = detect_rev();
	switch (rev) {
	case 0:
		/* For Stlink on  STM8S check that CN7 PIN 4 RESET# is
		 * forced to GND, Jumper CN7 PIN3/4 is plugged).
		 * Switch PB5 high. Read PB6 low means jumper plugged.
		 */
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, GPIO6);
		gpio_set(GPIOB, GPIO6);
		gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO5);
		while (gpio_get(GPIOB, GPIO5))
			gpio_clear(GPIOB, GPIO5);
		while (!gpio_get(GPIOB, GPIO5))
			gpio_set(GPIOB, GPIO5);
		normal_boot = (gpio_get(GPIOB, GPIO6));
		break;
	case 1:
		/* Boot0/1 pins have 100k between Jumper and MCU
		 * and are jumperd to low by default.
		 * If we read PB2 high, force bootloader entry.*/
		gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO2);
		normal_boot = !(gpio_get(GPIOB, GPIO2));
	}
	if ((GPIOA_CRL & 0x40U) == 0x40U && normal_boot)
		dfu_jump_app_if_valid();

	dfu_protect(false);

	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	systick_set_reload(900000);

	systick_interrupt_enable();
	systick_counter_enable();

	dfu_init(&st_usbfs_v1_usb_driver);

	dfu_main();
}

void set_idle_state(bool state)
{
	switch (rev) {
	case 0:
		gpio_set_val(GPIOA, GPIO8, state);
		break;
	case 1:
		gpio_set_val(LED_PORT, LED_IDLE_RUN, !state);
		break;
	}
}

void dfu_event(void)
{
	static bool idle_state = false;
	/* Ask systick to pause blinking for 1 second */
	dfu_activity_counter = 10;
	/* Blink it ourselves */
	set_idle_state(idle_state);
	idle_state = !idle_state;
}

void sys_tick_handler(void)
{
	static int count = 0;
	if (dfu_activity_counter > 0) {
		dfu_activity_counter--;
		return;
	}

	switch (count) {
	case 0:
		/* Reload downcounter */
		count = 10;
		set_idle_state(false);
		break;
	case 1:
		count--;
		/* Blink like a very slow PWM */
		set_idle_state(true);
		break;
	default:
		count--;
		break;
	}
}
