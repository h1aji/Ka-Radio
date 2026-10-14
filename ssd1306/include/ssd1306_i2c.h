#ifndef SSD1306_I2C_H
#define SSD1306_I2C_H

#include "c_types.h"

/*
 * Dedicated software (bit-banged) I2C master for the SSD1306 panel.
 *
 * driver_lib/include/i2c_master.h (ESP8266_RTOS_SDK v2.1.1) hardcodes
 * GPIO2 (SDA) / GPIO4 (SCL) and its macros call gpio_output_set() /
 * ETS_GPIO_INTR_ENABLE()/DISABLE(), none of which exist in this SDK version
 * (checked against the v2.1.1 tag - driver_lib/gpio.c never defines them).
 * GPIO2 also collides with the Ka-Radio IR receiver (include/ir.h, IR_PIN).
 * This reimplements the same start/stop/byte protocol on GPIO5 (SDA) /
 * GPIO4 (SCL) using only primitives that driver_lib/driver/gpio.c actually
 * provides (GPIO_OUTPUT_SET, GPIO_AS_OUTPUT, GPIO_INPUT_GET).
 */
#define SSD1306_I2C_SDA_GPIO 5
#define SSD1306_I2C_SCL_GPIO 4

void ssd1306_i2c_master_gpio_init(void);
void ssd1306_i2c_master_init(void);
void ssd1306_i2c_master_start(void);
void ssd1306_i2c_master_stop(void);
void ssd1306_i2c_master_writeByte(uint8_t wrdata);
uint8_t ssd1306_i2c_master_readByte(void);
bool ssd1306_i2c_master_checkAck(void);
void ssd1306_i2c_master_send_ack(void);
void ssd1306_i2c_master_send_nack(void);

#endif
