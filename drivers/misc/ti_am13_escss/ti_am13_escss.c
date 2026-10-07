/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_am13_escss

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/ti_am13_escss/ti_am13_escss.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ti_am13_escss, CONFIG_LOG_DEFAULT_LEVEL);

/* SYSCTL power, reset and clock control of the EtherCAT subsystem */
#define SYSCTL_BASE                 0x400AF000U
#define SYSCTL_PWREN_MCPERIPH       0x1424U
#define SYSCTL_RSTCTL_ASSERT_MCPERI 0x1428U
#define SYSCTL_RSTCTL_CLEAR_MCPERI  0x142CU
#define SYSCTL_PWREN_SYSPERIPH      0x1434U
#define SYSCTL_ETHERCATCLKCTL       0x14B0U
#define SYSCTL_ETHERCATCTL          0x14B4U

#define SYSCTL_PWREN_KEY            0x26000000U
#define SYSCTL_RSTCTL_KEY           0xB1000000U
#define SYSCTL_ECAT_CORE0_BIT       BIT(19)
#define SYSCTL_ECAT_SS0_BIT         BIT(5)
#define SYSCTL_ECATCLK_PHYCLKEN     BIT(8)
#define SYSCTL_ECATCLK_ECAT100CLKEN BIT(7)
#define SYSCTL_ECATCLK_ECATDIV_MASK GENMASK(3, 1)
#define SYSCTL_ECATCTL_I2CLOOPBACK  BIT(8)

/* Settling time after power and reset changes */
#define ESCSS_STARTUP_DELAY_US 10

/* ESCSS registers */
#define ESCSS_IPREVNUM          0x00U
#define ESCSS_INTR_RIS          0x04U
#define ESCSS_INTR_MASK         0x08U
#define ESCSS_INTR_MIS          0x0CU
#define ESCSS_INTR_CLR          0x10U
#define ESCSS_INTR_SET          0x14U
#define ESCSS_LATCH_SEL         0x18U
#define ESCSS_ACCESS_CTRL       0x1CU
#define ESCSS_GPIN_DAT          0x20U
#define ESCSS_GPIN_PIPE         0x24U
#define ESCSS_GPIN_GRP_CAP_SEL  0x28U
#define ESCSS_GPOUT_DAT         0x2CU
#define ESCSS_GPOUT_PIPE        0x30U
#define ESCSS_GPOUT_GRP_CAP_SEL 0x34U
#define ESCSS_MEM_TEST          0x38U
#define ESCSS_RESET_DEST_CONFIG 0x3CU
#define ESCSS_SYNC0_CONFIG      0x40U
#define ESCSS_SYNC1_CONFIG      0x44U

/* ESCSS_CONFIG registers */
#define ESCSS_CONFIG_LOCK    0x200U
#define ESCSS_MISC_IO_CONFIG 0x204U
#define ESCSS_PHY_IO_CONFIG  0x208U
#define ESCSS_GPIN_SEL       0x214U
#define ESCSS_MISC_CONFIG    0x228U

#define ESCSS_WRITE_KEY 0xA500U

#define ESCSS_IPREVNUM_MASK GENMASK(7, 0)

#define ESCSS_LATCH_SEL_LATCH0_MASK GENMASK(4, 0)
#define ESCSS_LATCH_SEL_LATCH1_MASK GENMASK(12, 8)
#define ESCSS_LATCH_SOURCE_MAX      26U

#define ESCSS_ACCESS_CTRL_WAIT_STATES_MASK   GENMASK(6, 0)
#define ESCSS_ACCESS_CTRL_EN_TIMEOUT         BIT(7)
#define ESCSS_ACCESS_CTRL_DEBUG_ACCESS       BIT(9)
#define ESCSS_ACCESS_CTRL_TIMEOUT_COUNT_MASK GENMASK(27, 16)

#define ESCSS_GPI_PINS_MASK           GENMASK(3, 0)
#define ESCSS_GPIN_GRP_CAP_SEL0_MASK  GENMASK(2, 0)
#define ESCSS_GPOUT_GRP_CAP_SEL0_MASK GENMASK(1, 0)

#define ESCSS_MEM_TEST_INITIATE_MEM_INIT BIT(0)
#define ESCSS_MEM_TEST_MEM_INIT_DONE     BIT(1)

#define ESCSS_RESET_DEST_MASK (BIT(0) | BIT(1) | BIT(2) | BIT(7))

#define ESCSS_SYNC_CONFIG_CPU_INT_EN  BIT(0)
#define ESCSS_SYNC_CONFIG_DMA_TRIG_EN BIT(2)

#define ESCSS_CONFIG_LOCK_LOCK_ENABLE      BIT(0)
#define ESCSS_CONFIG_LOCK_IO_CONFIG_ENABLE BIT(4)

#define ESCSS_MISC_IO_CONFIG_EEPROM_I2C_IO_EN BIT(1)

#define ESCSS_PHY_IO_CONFIG_PHY_PORT_CNT_MASK GENMASK(3, 2)
#define ESCSS_PHY_IO_CONFIG_TX_CLK_AUTO_COMP  BIT(6)

#define ESCSS_MISC_CONFIG_TX0_SHIFT_MASK GENMASK(1, 0)
#define ESCSS_MISC_CONFIG_TX1_SHIFT_MASK GENMASK(3, 2)
#define ESCSS_MISC_CONFIG_EEPROM_SIZE    BIT(4)
#define ESCSS_MISC_CONFIG_PHY_ADDR_MASK  GENMASK(10, 6)

/* Beckhoff ESC DL status register, bit 0 is set once the EEPROM is loaded */
#define ESC_DL_STATUS             0x0110U
#define ESC_DL_STATUS_EEPROM_LOAD BIT(0)

/* Number of tries to get the debug access acknowledged */
#define ESCSS_DEBUG_ACCESS_RETRIES 1000U

/* Events signalled on each interrupt line */
#define ESCSS_EVT_LINE_PDI                                                                         \
	(TI_ESCSS_EVT_PDI_IRQ | TI_ESCSS_EVT_DMA_DONE | TI_ESCSS_EVT_TIMEOUT_ERR)
#define ESCSS_EVT_LINE_SYNC0 TI_ESCSS_EVT_SYNC0
#define ESCSS_EVT_LINE_SYNC1 TI_ESCSS_EVT_SYNC1
#define ESCSS_EVT_LINE_RESET TI_ESCSS_EVT_MASTER_RESET

struct ti_escss_config {
	mem_addr_t base;
	mem_addr_t esc_base;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config_func)(void);
	unsigned int irq_pdi;
	unsigned int irq_sync0;
	unsigned int irq_sync1;
	unsigned int irq_reset;
	uint32_t clk_div;
	uint32_t phy_port_count;
	uint32_t phy_address;
	uint32_t tx0_shift;
	uint32_t tx1_shift;
	uint32_t gpin_pad_mask;
	uint32_t wait_states;
	uint32_t pdi_timeout;
	uint32_t latch0_source;
	uint32_t latch1_source;
	uint32_t reset_out_dest;
	uint32_t eeprom_load_timeout_ms;
	bool tx_clk_auto_comp;
	bool eeprom_i2c_pads;
	bool eeprom_16k_or_greater;
	bool i2c_loopback;
	bool lock_config;
};

struct ti_escss_data {
	struct k_spinlock lock;
	ti_escss_callback_t cb;
	void *user_data;
	uint32_t enabled_events;
};

static inline uint32_t escss_read(const struct device *dev, uint32_t offset)
{
	const struct ti_escss_config *cfg = dev->config;

	return sys_read32(cfg->base + offset);
}

static inline void escss_write(const struct device *dev, uint32_t offset, uint32_t value)
{
	const struct ti_escss_config *cfg = dev->config;

	sys_write32(value, cfg->base + offset);
}

static inline void escss_update(const struct device *dev, uint32_t offset, uint32_t mask,
				uint32_t value)
{
	escss_write(dev, offset, (escss_read(dev, offset) & ~mask) | (value & mask));
}

/* Update a register whose bits [15:8] must hold the write key */
static inline void escss_update_keyed(const struct device *dev, uint32_t offset, uint32_t mask,
				      uint32_t value)
{
	uint32_t reg = escss_read(dev, offset) & ~(mask | GENMASK(15, 8));

	escss_write(dev, offset, reg | (value & mask) | ESCSS_WRITE_KEY);
}

static void sysctl_power(uint32_t offset, uint32_t bit, bool enable)
{
	uint32_t reg = sys_read32(SYSCTL_BASE + offset) | SYSCTL_PWREN_KEY;

	sys_write32(enable ? (reg | bit) : (reg & ~bit), SYSCTL_BASE + offset);
}

static void escss_power_up(const struct device *dev)
{
	const struct ti_escss_config *cfg = dev->config;
	uint32_t reg;

	/*
	 * A debugger reload or CPU reset does not reset the EtherCAT subsystem,
	 * so the configuration lock and old register values can survive. Power
	 * the subsystem down and reset the core so the ESC starts from reset
	 * and reloads its EEPROM.
	 */
	sysctl_power(SYSCTL_PWREN_SYSPERIPH, SYSCTL_ECAT_SS0_BIT, false);
	sysctl_power(SYSCTL_PWREN_MCPERIPH, SYSCTL_ECAT_CORE0_BIT, false);
	sys_write32(SYSCTL_RSTCTL_KEY | SYSCTL_ECAT_CORE0_BIT,
		    SYSCTL_BASE + SYSCTL_RSTCTL_CLEAR_MCPERI);
	sys_write32(SYSCTL_RSTCTL_KEY | SYSCTL_ECAT_CORE0_BIT,
		    SYSCTL_BASE + SYSCTL_RSTCTL_ASSERT_MCPERI);
	k_busy_wait(ESCSS_STARTUP_DELAY_US);

	sysctl_power(SYSCTL_PWREN_MCPERIPH, SYSCTL_ECAT_CORE0_BIT, true);
	sysctl_power(SYSCTL_PWREN_SYSPERIPH, SYSCTL_ECAT_SS0_BIT, true);
	k_busy_wait(ESCSS_STARTUP_DELAY_US);

	/* Program the divider before the clocks are enabled */
	reg = sys_read32(SYSCTL_BASE + SYSCTL_ETHERCATCLKCTL) & ~SYSCTL_ECATCLK_ECATDIV_MASK;
	reg |= FIELD_PREP(SYSCTL_ECATCLK_ECATDIV_MASK, cfg->clk_div - 1U);
	sys_write32(reg, SYSCTL_BASE + SYSCTL_ETHERCATCLKCTL);
	sys_write32(reg | SYSCTL_ECATCLK_ECAT100CLKEN | SYSCTL_ECATCLK_PHYCLKEN,
		    SYSCTL_BASE + SYSCTL_ETHERCATCLKCTL);

	reg = sys_read32(SYSCTL_BASE + SYSCTL_ETHERCATCTL);
	if (cfg->i2c_loopback) {
		reg |= SYSCTL_ECATCTL_I2CLOOPBACK;
	} else {
		reg &= ~SYSCTL_ECATCTL_I2CLOOPBACK;
	}
	sys_write32(reg, SYSCTL_BASE + SYSCTL_ETHERCATCTL);

	k_busy_wait(ESCSS_STARTUP_DELAY_US);
}

static void escss_runtime_config(const struct device *dev)
{
	const struct ti_escss_config *cfg = dev->config;
	uint32_t reg;

	/* Keep the debug access bit, it is managed by ti_escss_debug_access() */
	reg = FIELD_PREP(ESCSS_ACCESS_CTRL_WAIT_STATES_MASK, cfg->wait_states);
	if (cfg->pdi_timeout != 0U) {
		reg |= ESCSS_ACCESS_CTRL_EN_TIMEOUT |
		       FIELD_PREP(ESCSS_ACCESS_CTRL_TIMEOUT_COUNT_MASK, cfg->pdi_timeout);
	}
	escss_update(dev, ESCSS_ACCESS_CTRL,
		     ESCSS_ACCESS_CTRL_WAIT_STATES_MASK | ESCSS_ACCESS_CTRL_EN_TIMEOUT |
			     ESCSS_ACCESS_CTRL_TIMEOUT_COUNT_MASK,
		     reg);

	escss_write(dev, ESCSS_LATCH_SEL,
		    FIELD_PREP(ESCSS_LATCH_SEL_LATCH0_MASK, cfg->latch0_source) |
			    FIELD_PREP(ESCSS_LATCH_SEL_LATCH1_MASK, cfg->latch1_source));

	/* All events start disabled */
	escss_write(dev, ESCSS_INTR_MASK, 0U);
	escss_write(dev, ESCSS_INTR_CLR, TI_ESCSS_EVT_ALL);

	escss_write(dev, ESCSS_RESET_DEST_CONFIG, cfg->reset_out_dest | ESCSS_WRITE_KEY);
	escss_write(dev, ESCSS_SYNC0_CONFIG, ESCSS_WRITE_KEY);
	escss_write(dev, ESCSS_SYNC1_CONFIG, ESCSS_WRITE_KEY);
}

static void escss_io_config(const struct device *dev)
{
	const struct ti_escss_config *cfg = dev->config;
	uint32_t reg;

	reg = FIELD_PREP(ESCSS_PHY_IO_CONFIG_PHY_PORT_CNT_MASK, cfg->phy_port_count - 1U);
	if (cfg->tx_clk_auto_comp) {
		reg |= ESCSS_PHY_IO_CONFIG_TX_CLK_AUTO_COMP;
	}
	escss_write(dev, ESCSS_PHY_IO_CONFIG, reg | ESCSS_WRITE_KEY);

	reg = cfg->eeprom_i2c_pads ? ESCSS_MISC_IO_CONFIG_EEPROM_I2C_IO_EN : 0U;
	escss_write(dev, ESCSS_MISC_IO_CONFIG, reg | ESCSS_WRITE_KEY);

	/* Other MISC_CONFIG fields keep their reset values */
	reg = FIELD_PREP(ESCSS_MISC_CONFIG_TX0_SHIFT_MASK, cfg->tx0_shift) |
	      FIELD_PREP(ESCSS_MISC_CONFIG_TX1_SHIFT_MASK, cfg->tx1_shift) |
	      FIELD_PREP(ESCSS_MISC_CONFIG_PHY_ADDR_MASK, cfg->phy_address);
	if (cfg->eeprom_16k_or_greater) {
		reg |= ESCSS_MISC_CONFIG_EEPROM_SIZE;
	}
	escss_update(dev, ESCSS_MISC_CONFIG,
		     ESCSS_MISC_CONFIG_TX0_SHIFT_MASK | ESCSS_MISC_CONFIG_TX1_SHIFT_MASK |
			     ESCSS_MISC_CONFIG_EEPROM_SIZE | ESCSS_MISC_CONFIG_PHY_ADDR_MASK,
		     reg);

	escss_update(dev, ESCSS_GPIN_SEL, ESCSS_GPI_PINS_MASK, cfg->gpin_pad_mask);
}

static uint32_t esc_read16(const struct device *dev, uint16_t addr)
{
	const struct ti_escss_config *cfg = dev->config;

	return sys_read16(cfg->esc_base + addr);
}

int ti_escss_wait_eeprom_loaded(const struct device *dev, k_timeout_t timeout)
{
	k_timepoint_t end = sys_timepoint_calc(timeout);

	while ((esc_read16(dev, ESC_DL_STATUS) & ESC_DL_STATUS_EEPROM_LOAD) == 0U) {
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
		k_busy_wait(100);
	}

	escss_write(dev, ESCSS_MEM_TEST, ESCSS_MEM_TEST_INITIATE_MEM_INIT);

	end = sys_timepoint_calc(timeout);
	while ((escss_read(dev, ESCSS_MEM_TEST) & ESCSS_MEM_TEST_MEM_INIT_DONE) == 0U) {
		if (sys_timepoint_expired(end)) {
			return -ETIMEDOUT;
		}
		k_busy_wait(10);
	}

	return 0;
}

bool ti_escss_is_config_locked(const struct device *dev)
{
	return (escss_read(dev, ESCSS_CONFIG_LOCK) & ESCSS_CONFIG_LOCK_LOCK_ENABLE) != 0U;
}

uint8_t ti_escss_get_ip_revision(const struct device *dev)
{
	return (uint8_t)(escss_read(dev, ESCSS_IPREVNUM) & ESCSS_IPREVNUM_MASK);
}

void ti_escss_set_callback(const struct device *dev, ti_escss_callback_t cb, void *user_data)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	data->cb = cb;
	data->user_data = user_data;

	k_spin_unlock(&data->lock, key);
}

/* Enable or disable each interrupt line according to the enabled events */
static void escss_update_irq_lines(const struct device *dev, uint32_t events)
{
	const struct ti_escss_config *cfg = dev->config;
	const struct {
		uint32_t events;
		unsigned int irq;
	} lines[] = {
		{ESCSS_EVT_LINE_PDI, cfg->irq_pdi},
		{ESCSS_EVT_LINE_SYNC0, cfg->irq_sync0},
		{ESCSS_EVT_LINE_SYNC1, cfg->irq_sync1},
		{ESCSS_EVT_LINE_RESET, cfg->irq_reset},
	};

	ARRAY_FOR_EACH(lines, i) {
		if ((events & lines[i].events) != 0U) {
			irq_enable(lines[i].irq);
		} else {
			irq_disable(lines[i].irq);
		}
	}
}

int ti_escss_irq_enable(const struct device *dev, uint32_t events)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key;

	if ((events & ~TI_ESCSS_EVT_ALL) != 0U) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	if ((events & TI_ESCSS_EVT_SYNC0) != 0U) {
		escss_update_keyed(dev, ESCSS_SYNC0_CONFIG, ESCSS_SYNC_CONFIG_CPU_INT_EN,
				   ESCSS_SYNC_CONFIG_CPU_INT_EN);
	}
	if ((events & TI_ESCSS_EVT_SYNC1) != 0U) {
		escss_update_keyed(dev, ESCSS_SYNC1_CONFIG, ESCSS_SYNC_CONFIG_CPU_INT_EN,
				   ESCSS_SYNC_CONFIG_CPU_INT_EN);
	}
	escss_update(dev, ESCSS_INTR_MASK, events, events);

	data->enabled_events |= events;
	escss_update_irq_lines(dev, data->enabled_events);

	k_spin_unlock(&data->lock, key);

	return 0;
}

int ti_escss_irq_disable(const struct device *dev, uint32_t events)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key;

	if ((events & ~TI_ESCSS_EVT_ALL) != 0U) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	if ((events & TI_ESCSS_EVT_SYNC0) != 0U) {
		escss_update_keyed(dev, ESCSS_SYNC0_CONFIG, ESCSS_SYNC_CONFIG_CPU_INT_EN, 0U);
	}
	if ((events & TI_ESCSS_EVT_SYNC1) != 0U) {
		escss_update_keyed(dev, ESCSS_SYNC1_CONFIG, ESCSS_SYNC_CONFIG_CPU_INT_EN, 0U);
	}
	escss_update(dev, ESCSS_INTR_MASK, events, 0U);

	data->enabled_events &= ~events;
	escss_update_irq_lines(dev, data->enabled_events);

	k_spin_unlock(&data->lock, key);

	return 0;
}

int ti_escss_irq_trigger(const struct device *dev, uint32_t events)
{
	if ((events & ~TI_ESCSS_EVT_ALL) != 0U) {
		return -EINVAL;
	}

	escss_write(dev, ESCSS_INTR_SET, events | ESCSS_WRITE_KEY);

	return 0;
}

static void escss_isr(const struct device *dev, uint32_t line_events)
{
	struct ti_escss_data *data = dev->data;
	uint32_t events = escss_read(dev, ESCSS_INTR_MIS) & line_events;

	escss_write(dev, ESCSS_INTR_CLR, events);

	if ((events != 0U) && (data->cb != NULL)) {
		data->cb(dev, events, data->user_data);
	}
}

static void escss_isr_pdi(const struct device *dev)
{
	escss_isr(dev, ESCSS_EVT_LINE_PDI);
}

static void escss_isr_sync0(const struct device *dev)
{
	escss_isr(dev, ESCSS_EVT_LINE_SYNC0);
}

static void escss_isr_sync1(const struct device *dev)
{
	escss_isr(dev, ESCSS_EVT_LINE_SYNC1);
}

static void escss_isr_reset(const struct device *dev)
{
	escss_isr(dev, ESCSS_EVT_LINE_RESET);
}

static bool escss_range_valid(uint16_t addr, size_t len)
{
	return (len <= TI_ESCSS_ESC_SIZE) && ((size_t)addr <= (TI_ESCSS_ESC_SIZE - len));
}

int ti_escss_read(const struct device *dev, uint16_t addr, void *buf, size_t len)
{
	const struct ti_escss_config *cfg = dev->config;
	uint8_t *dst = buf;

	if (!escss_range_valid(addr, len)) {
		return -EINVAL;
	}

	while (len > 0U) {
		mem_addr_t src = cfg->esc_base + addr;
		size_t step;

		if (((addr & 0x3U) == 0U) && (len >= 4U)) {
			sys_put_le32(sys_read32(src), dst);
			step = 4U;
		} else if (((addr & 0x1U) == 0U) && (len >= 2U)) {
			sys_put_le16(sys_read16(src), dst);
			step = 2U;
		} else {
			*dst = sys_read8(src);
			step = 1U;
		}

		addr += step;
		dst += step;
		len -= step;
	}

	return 0;
}

int ti_escss_write(const struct device *dev, uint16_t addr, const void *buf, size_t len)
{
	const struct ti_escss_config *cfg = dev->config;
	const uint8_t *src = buf;

	if (!escss_range_valid(addr, len)) {
		return -EINVAL;
	}

	while (len > 0U) {
		mem_addr_t dst = cfg->esc_base + addr;
		size_t step;

		if (((addr & 0x3U) == 0U) && (len >= 4U)) {
			sys_write32(sys_get_le32(src), dst);
			step = 4U;
		} else if (((addr & 0x1U) == 0U) && (len >= 2U)) {
			sys_write16(sys_get_le16(src), dst);
			step = 2U;
		} else {
			sys_write8(*src, dst);
			step = 1U;
		}

		addr += step;
		src += step;
		len -= step;
	}

	return 0;
}

uintptr_t ti_escss_esc_base(const struct device *dev)
{
	const struct ti_escss_config *cfg = dev->config;

	return (uintptr_t)cfg->esc_base;
}

int ti_escss_set_latch_source(const struct device *dev, uint8_t latch, uint8_t source)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key;

	if ((latch > 1U) || (source > ESCSS_LATCH_SOURCE_MAX)) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	if (latch == 0U) {
		escss_update(dev, ESCSS_LATCH_SEL, ESCSS_LATCH_SEL_LATCH0_MASK,
			     FIELD_PREP(ESCSS_LATCH_SEL_LATCH0_MASK, source));
	} else {
		escss_update(dev, ESCSS_LATCH_SEL, ESCSS_LATCH_SEL_LATCH1_MASK,
			     FIELD_PREP(ESCSS_LATCH_SEL_LATCH1_MASK, source));
	}
	k_spin_unlock(&data->lock, key);

	return 0;
}

uint32_t ti_escss_gpin_get(const struct device *dev)
{
	return escss_read(dev, ESCSS_GPIN_DAT);
}

void ti_escss_gpin_set(const struct device *dev, uint32_t mask, uint32_t value)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	escss_update(dev, ESCSS_GPIN_DAT, mask, value);

	k_spin_unlock(&data->lock, key);
}

uint32_t ti_escss_gpout_get(const struct device *dev)
{
	return escss_read(dev, ESCSS_GPOUT_DAT);
}

/* Only GPI[3:0] / GPO[3:0] exist, all in capture group 0 */
int ti_escss_gpin_capture(const struct device *dev, uint32_t mask,
			  enum ti_escss_capture_trigger trigger)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key;

	if ((uint32_t)trigger > TI_ESCSS_CAPTURE_EOF) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	escss_update(dev, ESCSS_GPIN_GRP_CAP_SEL, ESCSS_GPIN_GRP_CAP_SEL0_MASK, trigger);
	escss_update(dev, ESCSS_GPIN_PIPE, ESCSS_GPI_PINS_MASK, mask);
	k_spin_unlock(&data->lock, key);

	return 0;
}

int ti_escss_gpout_capture(const struct device *dev, uint32_t mask,
			   enum ti_escss_capture_trigger trigger)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key;

	if ((uint32_t)trigger > TI_ESCSS_CAPTURE_LATCH0) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	escss_update(dev, ESCSS_GPOUT_GRP_CAP_SEL, ESCSS_GPOUT_GRP_CAP_SEL0_MASK, trigger);
	escss_update(dev, ESCSS_GPOUT_PIPE, ESCSS_GPI_PINS_MASK, mask);
	k_spin_unlock(&data->lock, key);

	return 0;
}

int ti_escss_sync_dma_trigger(const struct device *dev, uint8_t sync, bool enable)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key;

	if (sync > 1U) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	escss_update_keyed(dev, (sync == 0U) ? ESCSS_SYNC0_CONFIG : ESCSS_SYNC1_CONFIG,
			   ESCSS_SYNC_CONFIG_DMA_TRIG_EN,
			   enable ? ESCSS_SYNC_CONFIG_DMA_TRIG_EN : 0U);
	k_spin_unlock(&data->lock, key);

	return 0;
}

int ti_escss_debug_access(const struct device *dev, bool enable)
{
	struct ti_escss_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	int ret = 0;

	if (!enable) {
		escss_update(dev, ESCSS_ACCESS_CTRL, ESCSS_ACCESS_CTRL_DEBUG_ACCESS, 0U);
		goto out;
	}

	/* The request has to be repeated until the hardware acknowledges it */
	for (uint32_t i = 0U; i < ESCSS_DEBUG_ACCESS_RETRIES; i++) {
		escss_update(dev, ESCSS_ACCESS_CTRL, ESCSS_ACCESS_CTRL_DEBUG_ACCESS,
			     ESCSS_ACCESS_CTRL_DEBUG_ACCESS);
		if ((escss_read(dev, ESCSS_ACCESS_CTRL) & ESCSS_ACCESS_CTRL_DEBUG_ACCESS) != 0U) {
			goto out;
		}
	}
	ret = -ETIMEDOUT;

out:
	k_spin_unlock(&data->lock, key);

	return ret;
}

static int ti_escss_init(const struct device *dev)
{
	const struct ti_escss_config *cfg = dev->config;
	int ret;

	if (cfg->pcfg != NULL) {
		ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
		if (ret < 0) {
			return ret;
		}
	}

	escss_power_up(dev);
	escss_runtime_config(dev);

	if (ti_escss_is_config_locked(dev)) {
		LOG_WRN("I/O configuration locked, keeping current settings");
	} else {
		escss_io_config(dev);
		if (cfg->lock_config) {
			escss_update_keyed(dev, ESCSS_CONFIG_LOCK, ESCSS_CONFIG_LOCK_LOCK_ENABLE,
					   ESCSS_CONFIG_LOCK_LOCK_ENABLE);
		}
	}

	/* Connect the EtherCAT ports to the pads once the I/O setup is done */
	escss_update_keyed(dev, ESCSS_CONFIG_LOCK, ESCSS_CONFIG_LOCK_IO_CONFIG_ENABLE,
			   ESCSS_CONFIG_LOCK_IO_CONFIG_ENABLE);

	cfg->irq_config_func();

	if (cfg->eeprom_load_timeout_ms != 0U) {
		ret = ti_escss_wait_eeprom_loaded(dev, K_MSEC(cfg->eeprom_load_timeout_ms));
		if (ret < 0) {
			LOG_ERR("SII EEPROM load timed out");
			return ret;
		}
	}

	LOG_DBG("ESCSS IP revision 0x%02x", ti_escss_get_ip_revision(dev));

	return 0;
}

#define ESCSS_IRQ_CONNECT(n, name, isr)                                                            \
	IRQ_CONNECT(DT_INST_IRQ_BY_NAME(n, name, irq), DT_INST_IRQ_BY_NAME(n, name, priority),     \
		    isr, DEVICE_DT_INST_GET(n), 0)

#define ESCSS_PINCTRL_DEFINE(n)                                                                    \
	IF_ENABLED(DT_INST_PINCTRL_HAS_NAME(n, default), (PINCTRL_DT_INST_DEFINE(n);))

#define ESCSS_PINCTRL_GET(n)                                                                       \
	COND_CODE_1(DT_INST_PINCTRL_HAS_NAME(n, default), (PINCTRL_DT_INST_DEV_CONFIG_GET(n)),    \
		    (NULL))

#define TI_ESCSS_INIT(n)                                                                           \
	BUILD_ASSERT(DT_INST_PROP(n, ti_phy_address) <= 31, "ti,phy-address out of range");        \
	BUILD_ASSERT(DT_INST_PROP(n, ti_wait_states) <= 127, "ti,wait-states out of range");       \
	BUILD_ASSERT(DT_INST_PROP(n, ti_pdi_timeout) <= 4095, "ti,pdi-timeout out of range");      \
	BUILD_ASSERT(DT_INST_PROP(n, ti_latch0_source) <= ESCSS_LATCH_SOURCE_MAX,                  \
		     "ti,latch0-source out of range");                                             \
	BUILD_ASSERT(DT_INST_PROP(n, ti_latch1_source) <= ESCSS_LATCH_SOURCE_MAX,                  \
		     "ti,latch1-source out of range");                                             \
	BUILD_ASSERT((DT_INST_PROP(n, ti_gpin_pad_mask) & ~ESCSS_GPI_PINS_MASK) == 0,              \
		     "ti,gpin-pad-mask out of range");                                             \
	BUILD_ASSERT((DT_INST_PROP(n, ti_reset_out_dest) & ~ESCSS_RESET_DEST_MASK) == 0,           \
		     "ti,reset-out-dest has invalid bits");                                        \
                                                                                                   \
	ESCSS_PINCTRL_DEFINE(n)                                                                    \
                                                                                                   \
	static void ti_escss_irq_config_##n(void)                                                  \
	{                                                                                          \
		ESCSS_IRQ_CONNECT(n, pdi, escss_isr_pdi);                                          \
		ESCSS_IRQ_CONNECT(n, sync0, escss_isr_sync0);                                      \
		ESCSS_IRQ_CONNECT(n, sync1, escss_isr_sync1);                                      \
		ESCSS_IRQ_CONNECT(n, reset, escss_isr_reset);                                      \
	}                                                                                          \
                                                                                                   \
	static const struct ti_escss_config ti_escss_config_##n = {                                \
		.base = DT_INST_REG_ADDR_BY_NAME(n, escss),                                        \
		.esc_base = DT_INST_REG_ADDR_BY_NAME(n, esc),                                      \
		.pcfg = ESCSS_PINCTRL_GET(n),                                                      \
		.irq_config_func = ti_escss_irq_config_##n,                                        \
		.irq_pdi = DT_INST_IRQ_BY_NAME(n, pdi, irq),                                       \
		.irq_sync0 = DT_INST_IRQ_BY_NAME(n, sync0, irq),                                   \
		.irq_sync1 = DT_INST_IRQ_BY_NAME(n, sync1, irq),                                   \
		.irq_reset = DT_INST_IRQ_BY_NAME(n, reset, irq),                                   \
		.clk_div = DT_INST_PROP(n, ti_clk_div),                                            \
		.phy_port_count = DT_INST_PROP(n, ti_phy_port_count),                              \
		.phy_address = DT_INST_PROP(n, ti_phy_address),                                    \
		.tx0_shift = DT_INST_PROP(n, ti_tx0_shift),                                        \
		.tx1_shift = DT_INST_PROP(n, ti_tx1_shift),                                        \
		.gpin_pad_mask = DT_INST_PROP(n, ti_gpin_pad_mask),                                \
		.wait_states = DT_INST_PROP(n, ti_wait_states),                                    \
		.pdi_timeout = DT_INST_PROP(n, ti_pdi_timeout),                                    \
		.latch0_source = DT_INST_PROP(n, ti_latch0_source),                                \
		.latch1_source = DT_INST_PROP(n, ti_latch1_source),                                \
		.reset_out_dest = DT_INST_PROP(n, ti_reset_out_dest),                              \
		.eeprom_load_timeout_ms = DT_INST_PROP(n, ti_eeprom_load_timeout_ms),              \
		.tx_clk_auto_comp = !DT_INST_PROP(n, ti_tx_clk_auto_comp_disable),                 \
		.eeprom_i2c_pads = !DT_INST_PROP(n, ti_eeprom_i2c_pads_disable),                   \
		.eeprom_16k_or_greater = !DT_INST_PROP(n, ti_eeprom_size_less_than_16k),           \
		.i2c_loopback = DT_INST_PROP(n, ti_i2c_loopback),                                  \
		.lock_config = !DT_INST_PROP(n, ti_no_config_lock),                                \
	};                                                                                         \
                                                                                                   \
	static struct ti_escss_data ti_escss_data_##n;                                             \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, ti_escss_init, NULL, &ti_escss_data_##n, &ti_escss_config_##n,    \
			      POST_KERNEL, CONFIG_TI_AM13_ESCSS_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(TI_ESCSS_INIT)
