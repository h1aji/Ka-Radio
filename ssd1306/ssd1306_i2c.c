#include "ssd1306_i2c.h"

#include "espressif/esp_common.h"
#include "espressif/esp8266/pin_mux_register.h"
#include "gpio.h"

static uint8_t m_nLastSDA;
static uint8_t m_nLastSCL;

/* roles of the two bus pins; ssd1306_i2c_scan() swaps them temporarily to detect crossed wires */
static uint8_t m_sda = SSD1306_I2C_SDA_GPIO;
static uint8_t m_scl = SSD1306_I2C_SCL_GPIO;

#define SSD1306_SDA_BIT  (1u << m_sda)
#define SSD1306_SCL_BIT  (1u << m_scl)

/*
 * Direct register access. The SDK macros GPIO_OUTPUT_SET / GPIO_AS_OUTPUT expand to
 * gpio_output_conf(), which lives in driver_lib/driver/gpio.c and is not linked in this
 * project. The W1TS/W1TC registers are atomic, so no interrupt lock is needed either.
 */
static void ssd1306_i2c_setDC(uint8_t sda, uint8_t scl)
{
	sda &= 0x01;
	scl &= 0x01;
	m_nLastSDA = sda;
	m_nLastSCL = scl;

	if (sda) GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, SSD1306_SDA_BIT);
	else     GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, SSD1306_SDA_BIT);

	if (scl) GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, SSD1306_SCL_BIT);
	else     GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, SSD1306_SCL_BIT);
}

static uint8_t ssd1306_i2c_getDC(void)
{
	return (GPIO_REG_READ(GPIO_IN_ADDRESS) >> m_sda) & 0x01;
}

void ssd1306_i2c_master_stop(void)
{
	os_delay_us(5);

	ssd1306_i2c_setDC(0, m_nLastSCL);
	os_delay_us(5);
	ssd1306_i2c_setDC(0, 1);
	os_delay_us(5);
	ssd1306_i2c_setDC(1, 1);
	os_delay_us(5);
}

void ssd1306_i2c_master_init(void)
{
	uint8_t i;

	ssd1306_i2c_setDC(1, 0);
	os_delay_us(5);

	/* when SCL = 0, toggle SDA to clear up */
	ssd1306_i2c_setDC(0, 0);
	os_delay_us(5);
	ssd1306_i2c_setDC(1, 0);
	os_delay_us(5);

	/* set data_cnt to max value */
	for (i = 0; i < 28; i++) {
		ssd1306_i2c_setDC(1, 0);
		os_delay_us(5);
		ssd1306_i2c_setDC(1, 1);
		os_delay_us(5);
	}

	/* reset all */
	ssd1306_i2c_master_stop();
}

void ssd1306_i2c_master_gpio_init(void)
{
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);

	/* open-drain pad driver on both lines */
	GPIO_REG_WRITE(GPIO_PIN_ADDR(SSD1306_I2C_SDA_GPIO),
		GPIO_REG_READ(GPIO_PIN_ADDR(SSD1306_I2C_SDA_GPIO)) | GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE));
	GPIO_REG_WRITE(GPIO_PIN_ADDR(SSD1306_I2C_SCL_GPIO),
		GPIO_REG_READ(GPIO_PIN_ADDR(SSD1306_I2C_SCL_GPIO)) | GPIO_PIN_PAD_DRIVER_SET(GPIO_PAD_DRIVER_ENABLE));

	/* lines released (high) first, then enable the open-drain outputs */
	GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, SSD1306_SDA_BIT | SSD1306_SCL_BIT);
	GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, SSD1306_SDA_BIT | SSD1306_SCL_BIT);

	ssd1306_i2c_setDC(1, 1);

	ssd1306_i2c_master_init();
}

void ssd1306_i2c_master_start(void)
{
	ssd1306_i2c_setDC(1, m_nLastSCL);
	os_delay_us(5);
	ssd1306_i2c_setDC(1, 1);
	os_delay_us(5);
	ssd1306_i2c_setDC(0, 1);
	os_delay_us(5);
}

static void ssd1306_i2c_setAck(uint8_t level)
{
	ssd1306_i2c_setDC(m_nLastSDA, 0);
	os_delay_us(5);
	ssd1306_i2c_setDC(level, 0);
	os_delay_us(5);
	ssd1306_i2c_setDC(level, 1);
	os_delay_us(8);
	ssd1306_i2c_setDC(level, 0);
	os_delay_us(5);
	ssd1306_i2c_setDC(1, 0);
	os_delay_us(5);
}

static uint8_t ssd1306_i2c_getAck(void)
{
	uint8_t ret;

	ssd1306_i2c_setDC(m_nLastSDA, 0);
	os_delay_us(5);
	ssd1306_i2c_setDC(1, 0);
	os_delay_us(5);
	ssd1306_i2c_setDC(1, 1);
	os_delay_us(5);

	ret = ssd1306_i2c_getDC();
	os_delay_us(5);
	ssd1306_i2c_setDC(1, 0);
	os_delay_us(5);

	return ret;
}

bool ssd1306_i2c_master_checkAck(void)
{
	return ssd1306_i2c_getAck() ? false : true;
}

void ssd1306_i2c_master_send_ack(void)
{
	ssd1306_i2c_setAck(0);
}

void ssd1306_i2c_master_send_nack(void)
{
	ssd1306_i2c_setAck(1);
}

uint8_t ssd1306_i2c_master_readByte(void)
{
	uint8_t ret = 0;
	uint8_t i, bit;

	os_delay_us(5);
	ssd1306_i2c_setDC(m_nLastSDA, 0);
	os_delay_us(5);

	for (i = 0; i < 8; i++) {
		os_delay_us(5);
		ssd1306_i2c_setDC(1, 0);
		os_delay_us(5);
		ssd1306_i2c_setDC(1, 1);
		os_delay_us(5);

		bit = ssd1306_i2c_getDC();
		os_delay_us(5);

		if (i == 7) {
			os_delay_us(3);
		}

		ret |= bit << (7 - i);
	}

	ssd1306_i2c_setDC(1, 0);
	os_delay_us(5);

	return ret;
}

void ssd1306_i2c_master_writeByte(uint8_t wrdata)
{
	int8_t i;
	uint8_t bit;

	os_delay_us(5);
	ssd1306_i2c_setDC(m_nLastSDA, 0);
	os_delay_us(5);

	for (i = 7; i >= 0; i--) {
		bit = (wrdata >> i) & 0x01;
		ssd1306_i2c_setDC(bit, 0);
		os_delay_us(5);
		ssd1306_i2c_setDC(bit, 1);
		os_delay_us(5);

		if (i == 0) {
			os_delay_us(3);
		}

		ssd1306_i2c_setDC(bit, 0);
		os_delay_us(5);
	}
}

/* ------------------------------------------------------------------------- */
/*  Bus scan (debug)                                                         */
/* ------------------------------------------------------------------------- */

/* probe every 7-bit address; returns the number of devices that answered */
static uint8_t i2c_probe_bus(void)
{
	uint8_t addr, found = 0;

	for (addr = 1; addr < 127; addr++) {
		ssd1306_i2c_master_start();
		ssd1306_i2c_master_writeByte(addr << 1);
		if (ssd1306_i2c_master_checkAck()) {
			printf("I2C device found at 0x%02X\n", addr);
			found++;
		}
		ssd1306_i2c_master_stop();
	}
	return found;
}

static void i2c_print_idle(void)
{
	uint32_t in = GPIO_REG_READ(GPIO_IN_ADDRESS);

	printf("I2C SDA=GPIO%d SCL=GPIO%d, idle levels: SDA=%d SCL=%d (both must be 1)\n",
		m_sda, m_scl, (int)((in >> m_sda) & 1), (int)((in >> m_scl) & 1));
}

/*
 * Lists the I2C devices on the bus. If nothing answers, the scan is repeated with
 * SDA and SCL swapped, so crossed wires are reported instead of just failing.
 */
void ssd1306_i2c_scan(void)
{
	uint8_t found, tmp;

	printf("\nI2C scan\n");
	ssd1306_i2c_master_gpio_init();
	i2c_print_idle();
	found = i2c_probe_bus();

	if (!found) {
		/* try with the two lines swapped */
		tmp = m_sda; m_sda = m_scl; m_scl = tmp;
		ssd1306_i2c_master_gpio_init();
		i2c_print_idle();
		if (i2c_probe_bus()) {
			printf("I2C: devices answer only with SDA and SCL swapped -> swap the two wires, or the pin numbers in ssd1306_i2c.h\n");
			found = 1;
		}
		/* back to the configured pins */
		tmp = m_sda; m_sda = m_scl; m_scl = tmp;
		ssd1306_i2c_master_gpio_init();
	}

	if (!found) printf("I2C scan: no device answered\n");
}
