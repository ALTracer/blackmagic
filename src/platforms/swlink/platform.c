/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2011  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
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
 * This file implements the platform specific functions for the "swlink" (ST-Link clones) implementation.
 * This is targeted to STM8S discovery and STM32F103 Minimum System Development Board (also known as the bluepill).
 */

#include "general.h"
#include "platform.h"
#include "usb.h"
#include "aux_serial.h"

#include <libopencm3/cm3/vector.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/scs.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/usart.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/spi.h>

#include "platform_common.h"

static uint32_t led_idlerun_port;
static uint16_t led_idlerun_pin;
static uint8_t rev;

#ifdef GD32F3

#define RCC_CFGR_USBPRE_SHIFT          22
#define RCC_CFGR_USBPRE_MASK           (0x3 << RCC_CFGR_USBPRE_SHIFT)
#define RCC_CFGR_USBPRE_PLL_CLK_DIV1_5 0x0
#define RCC_CFGR_USBPRE_PLL_CLK_NODIV  0x1
#define RCC_CFGR_USBPRE_PLL_CLK_DIV2_5 0x2
#define RCC_CFGR_USBPRE_PLL_CLK_DIV2   0x3

static const struct rcc_clock_scale rcc_hse_config_hse8_120mhz = {
	/* hse8, pll to 120 */
	.pll_mul = RCC_CFGR_PLLMUL_PLL_CLK_MUL15,
	.pll_source = RCC_CFGR_PLLSRC_HSE_CLK,
	.hpre = RCC_CFGR_HPRE_NODIV,
	.ppre1 = RCC_CFGR_PPRE_DIV2,
	.ppre2 = RCC_CFGR_PPRE_NODIV,
	.adcpre = RCC_CFGR_ADCPRE_DIV8,
	.flash_waitstates = 5, /* except WSEN is 0 and WSCNT don't care */
	.prediv1 = RCC_CFGR2_PREDIV_NODIV,
	.usbpre = RCC_CFGR_USBPRE_PLL_CLK_NODIV, /* libopencm3_stm32f1 hack */
	.ahb_frequency = 120e6,
	.apb1_frequency = 60e6,
	.apb2_frequency = 120e6,
};

/* Set USB CK48M prescaler on GD32F30x before enabling RCC_APB1ENR_USBEN */
static void rcc_set_usbpre_gd32f30x(uint32_t usbpre)
{
#if 1
	/* NuttX style */
	uint32_t regval = RCC_CFGR;
	regval &= ~RCC_CFGR_USBPRE_MASK;
	regval |= (usbpre << RCC_CFGR_USBPRE_SHIFT);
	RCC_CFGR = regval;
#else
	/* libopencm3 style */
	RCC_CFGR = (RCC_CFGR & ~RCC_CFGR_USBPRE_MASK) | (usbpre << RCC_CFGR_USBPRE_SHIFT);
#endif
}
#endif

static void adc_init(void);

int platform_hwversion(void)
{
	return rev;
}

void platform_init(void)
{
	SCS_DEMCR |= SCS_DEMCR_VC_MON_EN;
#ifdef GD32F3
	rcc_clock_setup_pll(&rcc_hse_config_hse8_120mhz);
	/* Set 120/2.5=48MHz USB divisor before enabling PLL (and fixup libopencm3 resetting it to DIV1_5) */
	rcc_set_usbpre_gd32f30x(RCC_CFGR_USBPRE_PLL_CLK_DIV2_5);
	/* TODO: Alternatively, use CTC Clock Trim Controller with HSI48M for USB CK48M */
#else
	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
#endif
	rev = detect_rev();
	/* Enable peripherals */
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_CRC);

	/* Unmap JTAG Pins so we can reuse as GPIO */
	gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_OFF, 0U);

	/* Setup JTAG GPIO ports */
	gpio_set_mode(TMS_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_INPUT_FLOAT, TMS_PIN);
	gpio_set_mode(TCK_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TCK_PIN);
	gpio_set_mode(TDI_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, TDI_PIN);

	gpio_set_mode(TDO_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, TDO_PIN);

	switch (rev) {
	case 0:
		/* LED GPIO already set in detect_rev() */
		led_idlerun_port = GPIOA;
		led_idlerun_pin = GPIO8;
		adc_init();
		break;
	case 1:
#ifdef BLUEPILLPLUS
		led_idlerun_port = GPIOB;
		led_idlerun_pin = GPIO2;
#else
		led_idlerun_port = GPIOC;
		led_idlerun_pin = GPIO13;
#endif
		/* Enable MCO Out on PA8 */
		RCC_CFGR &= ~(0xfU << 24U);
		RCC_CFGR |= (RCC_CFGR_MCO_HSE << 24U);
		gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO8);
		break;
	}
	platform_nrst_set_val(false);

	/*
	 * Remap TIM2 TIM2_REMAP[1]
	 * TIM2_CH1_ETR -> PA15 (TDI, set as output above)
	 * TIM2_CH2     -> PB3  (TDO)
	 */
	gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_OFF, AFIO_MAPR_TIM2_REMAP_PARTIAL_REMAP1);

	/* Remap USART1 from PA9/PA10 to PB6/PB7 */
	gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_OFF, AFIO_MAPR_USART1_REMAP);

	/* Relocate interrupt vector table here */
	SCB_VTOR = (uintptr_t)&vector_table;

	platform_timing_init();
	blackmagic_usb_init();
	aux_serial_init();
}

void platform_nrst_set_val(bool assert)
{
	/* We reuse nTRST as nRST. */
	if (assert) {
		gpio_set_mode(TRST_PORT, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_OPENDRAIN, TRST_PIN);
		/* Wait until requested value is active. */
		while (gpio_get(TRST_PORT, TRST_PIN))
			gpio_clear(TRST_PORT, TRST_PIN);
	} else {
		gpio_set_mode(TRST_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_PULL_UPDOWN, TRST_PIN);
		/* Wait until requested value is active .*/
		while (!gpio_get(TRST_PORT, TRST_PIN))
			gpio_set(TRST_PORT, TRST_PIN);
	}
}

bool platform_nrst_get_val(void)
{
	return gpio_get(TRST_PORT, TRST_PIN) == 0;
}

static void adc_init(void)
{
	rcc_periph_clock_enable(RCC_ADC1);
	/* PA0 measures CN7 Pin 1 VDD divided by two. */
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO0);
	adc_power_off(ADC1);
	adc_disable_scan_mode(ADC1);
	adc_set_single_conversion_mode(ADC1);
	adc_disable_external_trigger_regular(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_28DOT5CYC);

	adc_power_on(ADC1);

	/* Wait for the ADC to finish starting up */
	for (volatile size_t i = 0; i < 800000U; ++i)
		continue;

	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
}

const char *platform_target_voltage(void)
{
	static char ret[] = "0.0V";
	const uint8_t channel = 0;
	switch (rev) {
	case 0:
		adc_set_regular_sequence(ADC1, 1, (uint8_t *)&channel);
		adc_start_conversion_direct(ADC1);
		/* Wait for end of conversion. */
		while (!adc_eoc(ADC1))
			continue;
		/*
		 * Reference voltage is 3.3V.
		 * We expect the measured voltage to be half of the actual voltage.
		 * The computed value read is expressed in 0.1mV steps
		 */
		uint32_t value = (adc_read_regular(ADC1) * 66U) / 4096U;
		ret[0] = '0' + value / 10U;
		ret[2] = '0' + value % 10U;
		return ret;
	}
	return "Unknown";
}

void set_idle_state(bool state)
{
	switch (rev) {
	case 0:
		gpio_set_val(GPIOA, GPIO8, state);
		break;
	case 1:
		gpio_set_val(led_idlerun_port, led_idlerun_pin, !state);
		break;
	}
}

void platform_target_clk_output_enable(bool enable)
{
	(void)enable;
}

bool platform_spi_init(const spi_bus_e bus)
{
	uint32_t controller = 0;
	if (bus == SPI_BUS_INTERNAL) {
		/* Set up onboard flash SPI GPIOs: PA5/6/7 as SPI1 in AF5, PA4 as nCS output push-pull */
		gpio_set_mode(OB_SPI_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
			OB_SPI_SCLK | OB_SPI_MISO | OB_SPI_MOSI);
		gpio_set_mode(OB_SPI_PORT, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, OB_SPI_CS);
		/* Deselect the targeted peripheral chip */
		gpio_set(OB_SPI_PORT, OB_SPI_CS);

		rcc_periph_clock_enable(RCC_SPI1);
		rcc_periph_reset_pulse(RST_SPI1);
		controller = OB_SPI;
	} else
		return false;

	/* Set up hardware SPI: master, PCLK/8, Mode 0, 8-bit MSB first */
	spi_init_master(controller, SPI_CR1_BAUDRATE_FPCLK_DIV_8, SPI_CR1_CPOL_CLK_TO_0_WHEN_IDLE,
		SPI_CR1_CPHA_CLK_TRANSITION_1, SPI_CR1_DFF_8BIT, SPI_CR1_MSBFIRST);
	spi_enable(controller);
	return true;
}

bool platform_spi_deinit(const spi_bus_e bus)
{
	if (bus == SPI_BUS_INTERNAL) {
		spi_disable(OB_SPI);
		/* Gate SPI1 APB clock */
		rcc_periph_clock_disable(RCC_SPI1);
		/* Unmap GPIOs */
		gpio_set_mode(
			OB_SPI_PORT, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, OB_SPI_SCLK | OB_SPI_MISO | OB_SPI_MOSI | OB_SPI_CS);
		return true;
	} else
		return false;
}

bool platform_spi_chip_select(const uint8_t device_select)
{
	const uint8_t device = device_select & 0x7fU;
	const bool select = !(device_select & 0x80U);
	uint32_t port;
	uint16_t pin;
	switch (device) {
	case SPI_DEVICE_INT_FLASH:
		port = OB_SPI_CS_PORT;
		pin = OB_SPI_CS;
		break;
	default:
		return false;
	}
	gpio_set_val(port, pin, select);
	return true;
}

uint8_t platform_spi_xfer(const spi_bus_e bus, const uint8_t value)
{
	switch (bus) {
	case SPI_BUS_INTERNAL:
		return spi_xfer(OB_SPI, value);
		break;
	default:
		return 0U;
	}
}
