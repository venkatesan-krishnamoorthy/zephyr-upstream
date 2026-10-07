/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for the TI AM13 EtherCAT Subsystem (ESCSS) driver
 * @ingroup ti_am13_escss_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_TI_AM13_ESCSS_TI_AM13_ESCSS_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_TI_AM13_ESCSS_TI_AM13_ESCSS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ti_am13_escss_interface TI AM13 ESCSS
 * @ingroup misc_interfaces
 *
 * The ESCSS wraps a Beckhoff EtherCAT Slave Controller (ESC). The ESC is
 * accessed through its PDI address space: ESC registers at 0x0000-0x0FFF and
 * process data RAM from @ref TI_ESCSS_PDRAM_START.
 * @{
 */

/** First ESC address of the process data RAM */
#define TI_ESCSS_PDRAM_START 0x1000U
/** Size of the process data RAM in bytes */
#define TI_ESCSS_PDRAM_SIZE  0x4000U
/** Size of the ESC PDI address space (registers + process data RAM) */
#define TI_ESCSS_ESC_SIZE    (TI_ESCSS_PDRAM_START + TI_ESCSS_PDRAM_SIZE)

/**
 * @name ESCSS events
 * Bit values match the ESCSS interrupt registers.
 * @{
 */
/** SYNC0 pulse from the ESC distributed clock */
#define TI_ESCSS_EVT_SYNC0        BIT(0)
/** SYNC1 pulse from the ESC distributed clock */
#define TI_ESCSS_EVT_SYNC1        BIT(1)
/** ESC PDI (AL event) interrupt request */
#define TI_ESCSS_EVT_PDI_IRQ      BIT(2)
/** SYNC triggered DMA transfer complete */
#define TI_ESCSS_EVT_DMA_DONE     BIT(3)
/** PDI bus access timeout */
#define TI_ESCSS_EVT_TIMEOUT_ERR  BIT(4)
/** EtherCAT master requested reset (ResetOut) */
#define TI_ESCSS_EVT_MASTER_RESET BIT(5)
/** All ESCSS events */
#define TI_ESCSS_EVT_ALL          GENMASK(5, 0)
/** @} */

/** GPI/GPO pipeline capture trigger */
enum ti_escss_capture_trigger {
	/** No capture, data follows the pins / master writes directly */
	TI_ESCSS_CAPTURE_NONE = 0,
	/** Capture on SYNC0 */
	TI_ESCSS_CAPTURE_SYNC0 = 1,
	/** Capture on SYNC1 */
	TI_ESCSS_CAPTURE_SYNC1 = 2,
	/** Capture on LATCH0 */
	TI_ESCSS_CAPTURE_LATCH0 = 3,
	/** Capture on LATCH1 (GPI only) */
	TI_ESCSS_CAPTURE_LATCH1 = 4,
	/** Capture on start of frame (GPI only) */
	TI_ESCSS_CAPTURE_SOF = 5,
	/** Capture on end of frame (GPI only) */
	TI_ESCSS_CAPTURE_EOF = 6,
};

/**
 * @brief ESCSS event callback
 *
 * Called from interrupt context.
 *
 * @param dev ESCSS device.
 * @param events Bitwise OR of TI_ESCSS_EVT_* that occurred.
 * @param user_data User data given to ti_escss_set_callback().
 */
typedef void (*ti_escss_callback_t)(const struct device *dev, uint32_t events, void *user_data);

/**
 * @brief Set the event callback
 *
 * @param dev ESCSS device.
 * @param cb Callback, or NULL to remove it.
 * @param user_data Passed to @p cb.
 */
void ti_escss_set_callback(const struct device *dev, ti_escss_callback_t cb, void *user_data);

/**
 * @brief Enable events
 *
 * Unmasks the events in the ESCSS, routes SYNC0/SYNC1 to the CPU when
 * requested and enables the matching interrupt lines.
 *
 * @param dev ESCSS device.
 * @param events Bitwise OR of TI_ESCSS_EVT_*.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p events has unknown bits.
 */
int ti_escss_irq_enable(const struct device *dev, uint32_t events);

/**
 * @brief Disable events
 *
 * @param dev ESCSS device.
 * @param events Bitwise OR of TI_ESCSS_EVT_*.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p events has unknown bits.
 */
int ti_escss_irq_disable(const struct device *dev, uint32_t events);

/**
 * @brief Raise events by software
 *
 * Sets the events in the ESCSS interrupt status as if hardware raised them.
 *
 * @param dev ESCSS device.
 * @param events Bitwise OR of TI_ESCSS_EVT_*.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p events has unknown bits.
 */
int ti_escss_irq_trigger(const struct device *dev, uint32_t events);

/**
 * @brief Read from the ESC PDI address space
 *
 * Uses the widest naturally aligned access for each part of the transfer.
 *
 * @param dev ESCSS device.
 * @param addr ESC address.
 * @param buf Destination buffer.
 * @param len Number of bytes.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the range is outside the ESC address space.
 */
int ti_escss_read(const struct device *dev, uint16_t addr, void *buf, size_t len);

/**
 * @brief Write to the ESC PDI address space
 *
 * Uses the widest naturally aligned access for each part of the transfer.
 *
 * @param dev ESCSS device.
 * @param addr ESC address.
 * @param buf Source buffer.
 * @param len Number of bytes.
 *
 * @retval 0 on success.
 * @retval -EINVAL if the range is outside the ESC address space.
 */
int ti_escss_write(const struct device *dev, uint16_t addr, const void *buf, size_t len);

/**
 * @brief Get the address of the ESC PDI address space
 *
 * For stacks that access the ESC memory directly. Accesses must be 8, 16
 * or 32 bit wide and naturally aligned.
 *
 * @param dev ESCSS device.
 *
 * @return CPU address of ESC address 0.
 */
uintptr_t ti_escss_esc_base(const struct device *dev);

/**
 * @brief Wait for the SII EEPROM load and initialize the ESC memory
 *
 * Needed only when the driver skips the wait at init, see the
 * ti,eeprom-load-timeout-ms devicetree property.
 *
 * @param dev ESCSS device.
 * @param timeout Maximum time to wait for each step.
 *
 * @retval 0 on success.
 * @retval -ETIMEDOUT if the EEPROM load or memory init did not complete.
 */
int ti_escss_wait_eeprom_loaded(const struct device *dev, k_timeout_t timeout);

/**
 * @brief Check whether the I/O configuration registers are locked
 *
 * @param dev ESCSS device.
 *
 * @return true if locked until the next EtherCAT subsystem reset.
 */
bool ti_escss_is_config_locked(const struct device *dev);

/**
 * @brief Get the ESCSS IP revision
 *
 * @param dev ESCSS device.
 *
 * @return Major revision in bits [7:4], minor revision in bits [3:0].
 */
uint8_t ti_escss_get_ip_revision(const struct device *dev);

/**
 * @brief Select the input source of a LATCH signal
 *
 * @param dev ESCSS device.
 * @param latch LATCH input, 0 or 1.
 * @param source One of TI_ESCSS_LATCH_* from
 *        <zephyr/dt-bindings/misc/ti-am13-escss.h>.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p latch or @p source is invalid.
 */
int ti_escss_set_latch_source(const struct device *dev, uint8_t latch, uint8_t source);

/**
 * @brief Read the GPI data
 *
 * @param dev ESCSS device.
 *
 * @return GPI data seen by the ESC.
 */
uint32_t ti_escss_gpin_get(const struct device *dev);

/**
 * @brief Drive GPI pins that are not connected to I/O pads
 *
 * @param dev ESCSS device.
 * @param mask GPI pins to change.
 * @param value New values of the pins in @p mask.
 */
void ti_escss_gpin_set(const struct device *dev, uint32_t mask, uint32_t value);

/**
 * @brief Read the GPO data driven by the ESC
 *
 * @param dev ESCSS device.
 *
 * @return GPO data.
 */
uint32_t ti_escss_gpout_get(const struct device *dev);

/**
 * @brief Configure GPI pipeline capture
 *
 * @param dev ESCSS device.
 * @param mask GPI pins captured on @p trigger, the other pins follow the
 *        input directly.
 * @param trigger Capture trigger.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p trigger is invalid.
 */
int ti_escss_gpin_capture(const struct device *dev, uint32_t mask,
			  enum ti_escss_capture_trigger trigger);

/**
 * @brief Configure GPO pipeline capture
 *
 * @param dev ESCSS device.
 * @param mask GPO pins updated on @p trigger, the other pins follow master
 *        writes directly.
 * @param trigger Capture trigger, up to @ref TI_ESCSS_CAPTURE_LATCH0.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p trigger is invalid.
 */
int ti_escss_gpout_capture(const struct device *dev, uint32_t mask,
			   enum ti_escss_capture_trigger trigger);

/**
 * @brief Enable or disable a SYNC signal as DMA trigger
 *
 * @param dev ESCSS device.
 * @param sync SYNC signal, 0 or 1.
 * @param enable true to enable the trigger.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @p sync is invalid.
 */
int ti_escss_sync_dma_trigger(const struct device *dev, uint8_t sync, bool enable);

/**
 * @brief Enable or disable ESC debug register access
 *
 * @param dev ESCSS device.
 * @param enable true to enable debug access.
 *
 * @retval 0 on success.
 * @retval -ETIMEDOUT if the hardware did not acknowledge the request.
 */
int ti_escss_debug_access(const struct device *dev, bool enable);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_TI_AM13_ESCSS_TI_AM13_ESCSS_H_ */
