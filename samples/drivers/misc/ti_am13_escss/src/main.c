/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/ti_am13_escss/ti_am13_escss.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/* Beckhoff ESC registers */
#define ESC_TYPE      0x0000U
#define ESC_REVISION  0x0001U
#define ESC_BUILD     0x0002U
#define ESC_AL_STATUS 0x0130U
#define ESC_AL_EVENT  0x0220U

static const struct device *const escss = DEVICE_DT_GET_ONE(ti_am13_escss);

static atomic_t pdi_events;
static atomic_t sync0_events;

static void escss_event(const struct device *dev, uint32_t events, void *user_data)
{
	uint16_t al_event;

	ARG_UNUSED(user_data);

	if ((events & TI_ESCSS_EVT_PDI_IRQ) != 0U) {
		/* Reading the AL event request register acknowledges the IRQ */
		(void)ti_escss_read(dev, ESC_AL_EVENT, &al_event, sizeof(al_event));
		atomic_inc(&pdi_events);
	}

	if ((events & TI_ESCSS_EVT_SYNC0) != 0U) {
		atomic_inc(&sync0_events);
	}
}

int main(void)
{
	uint8_t type;
	uint8_t revision;
	uint16_t build;
	uint16_t al_status;
	int ret;

	if (!device_is_ready(escss)) {
		printf("ESCSS device not ready\n");
		return 0;
	}

	printf("ESCSS IP revision 0x%02x\n", ti_escss_get_ip_revision(escss));

	ret = ti_escss_read(escss, ESC_TYPE, &type, sizeof(type));
	ret |= ti_escss_read(escss, ESC_REVISION, &revision, sizeof(revision));
	ret |= ti_escss_read(escss, ESC_BUILD, &build, sizeof(build));
	if (ret != 0) {
		printf("ESC read failed\n");
		return 0;
	}
	printf("ESC type 0x%02x revision 0x%02x build 0x%04x\n", type, revision, build);

	ti_escss_set_callback(escss, escss_event, NULL);
	ret = ti_escss_irq_enable(escss, TI_ESCSS_EVT_PDI_IRQ | TI_ESCSS_EVT_SYNC0);
	if (ret != 0) {
		printf("Failed to enable events (%d)\n", ret);
		return 0;
	}

	while (true) {
		(void)ti_escss_read(escss, ESC_AL_STATUS, &al_status, sizeof(al_status));
		printf("AL status 0x%04x, PDI events %ld, SYNC0 events %ld\n", al_status,
		       (long)atomic_get(&pdi_events), (long)atomic_get(&sync0_events));
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
