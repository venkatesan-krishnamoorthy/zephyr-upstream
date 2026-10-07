/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/ti_am13_escss/ti_am13_escss.h>
#include <zephyr/dt-bindings/misc/ti-am13-escss.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define ESCSS_NODE DT_INST(0, ti_am13_escss)

static const struct device *const escss = DEVICE_DT_GET(ESCSS_NODE);

static K_SEM_DEFINE(event_sem, 0, 1);
static uint32_t last_events;

static void event_cb(const struct device *dev, uint32_t events, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	last_events |= events;
	k_sem_give(&event_sem);
}

ZTEST(ti_am13_escss, test_access_bounds)
{
	uint8_t buf[4];

	zassert_equal(ti_escss_read(escss, TI_ESCSS_ESC_SIZE - 1U, buf, 1), 0);
	zassert_equal(ti_escss_read(escss, TI_ESCSS_ESC_SIZE - 1U, buf, 2), -EINVAL);
	zassert_equal(ti_escss_write(escss, TI_ESCSS_ESC_SIZE - 2U, buf, 4), -EINVAL);
	zassert_equal(ti_escss_read(escss, 0, buf, TI_ESCSS_ESC_SIZE + 1U), -EINVAL);
}

ZTEST(ti_am13_escss, test_pdram_readback)
{
	static uint8_t wr[61];
	static uint8_t rd[61];
	/* Odd start and length exercise 8, 16 and 32 bit accesses */
	const uint16_t addr = TI_ESCSS_PDRAM_START + 0x101U;

	for (size_t i = 0; i < sizeof(wr); i++) {
		wr[i] = (uint8_t)(0xA5U ^ (i * 7U));
	}

	zassert_ok(ti_escss_write(escss, addr, wr, sizeof(wr)));
	zassert_ok(ti_escss_read(escss, addr, rd, sizeof(rd)));
	zassert_mem_equal(wr, rd, sizeof(wr));
}

ZTEST(ti_am13_escss, test_invalid_args)
{
	zassert_equal(ti_escss_irq_enable(escss, BIT(6)), -EINVAL);
	zassert_equal(ti_escss_irq_disable(escss, BIT(31)), -EINVAL);
	zassert_equal(ti_escss_irq_trigger(escss, BIT(8)), -EINVAL);
	zassert_equal(ti_escss_set_latch_source(escss, 2, TI_ESCSS_LATCH_PIN_LATCH0), -EINVAL);
	zassert_equal(ti_escss_set_latch_source(escss, 0, TI_ESCSS_LATCH_PWMXBAR(8) + 1), -EINVAL);
	zassert_equal(ti_escss_gpout_capture(escss, 0x1, TI_ESCSS_CAPTURE_LATCH1), -EINVAL);
	zassert_equal(ti_escss_sync_dma_trigger(escss, 2, true), -EINVAL);
}

ZTEST(ti_am13_escss, test_latch_source)
{
	zassert_ok(ti_escss_set_latch_source(escss, 0, TI_ESCSS_LATCH_INPUTXBAR(1)));
	zassert_ok(ti_escss_set_latch_source(escss, 1, TI_ESCSS_LATCH_PWMXBAR(8)));
	zassert_ok(ti_escss_set_latch_source(escss, 0, DT_PROP(ESCSS_NODE, ti_latch0_source)));
	zassert_ok(ti_escss_set_latch_source(escss, 1, DT_PROP(ESCSS_NODE, ti_latch1_source)));
}

ZTEST(ti_am13_escss, test_config_lock)
{
	zassert_equal(ti_escss_is_config_locked(escss), !DT_PROP(ESCSS_NODE, ti_no_config_lock));
}

static void check_forced_event(uint32_t event)
{
	last_events = 0U;
	k_sem_reset(&event_sem);

	zassert_ok(ti_escss_irq_enable(escss, event));
	zassert_ok(ti_escss_irq_trigger(escss, event));
	zassert_ok(k_sem_take(&event_sem, K_MSEC(100)), "event 0x%x not reported", event);
	zassert_ok(ti_escss_irq_disable(escss, event));
	zassert_true((last_events & event) != 0U);
}

ZTEST(ti_am13_escss, test_forced_events)
{
	ti_escss_set_callback(escss, event_cb, NULL);

	check_forced_event(TI_ESCSS_EVT_SYNC0);
	check_forced_event(TI_ESCSS_EVT_SYNC1);
	check_forced_event(TI_ESCSS_EVT_TIMEOUT_ERR);

	ti_escss_set_callback(escss, NULL, NULL);
}

static void *escss_setup(void)
{
	zassert_true(device_is_ready(escss), "ESCSS device not ready");

	return NULL;
}

ZTEST_SUITE(ti_am13_escss, NULL, escss_setup, NULL, NULL, NULL);
