#include "ssd1306_i2c.h"

#include "espressif/esp_common.h"
#include "espressif/esp8266/pin_mux_register.h"
#include "gpio.h"

static uint8_t m_nLastSDA;
static uint8_t m_nLastSCL;

static void ssd1306_i2c_setDC(uint8_t sda, uint8_t scl)
{
	sda &= 0x01;
	scl &= 0x01;
	m_nLastSDA = sda;
	m_nLastSCL = scl;

	ETS_INTR_LOCK();
	GPIO_OUTPUT_SET(SSD1306_I2C_SDA_GPIO, sda);
	GPIO_OUTPUT_SET(SSD1306_I2C_SCL_GPIO, scl);
	ETS_INTR_UNLOCK();
}

static uint8_t ssd1306_i2c_getDC(void)
{
	uint8_t sda_in;

	ETS_INTR_LOCK();
	sda_in = GPIO_INPUT_GET(SSD1306_I2C_SDA_GPIO);
	ETS_INTR_UNLOCK();

	return sda_in;
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

	GPIO_AS_OUTPUT(1 << SSD1306_I2C_SDA_GPIO);
	GPIO_AS_OUTPUT(1 << SSD1306_I2C_SCL_GPIO);

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
