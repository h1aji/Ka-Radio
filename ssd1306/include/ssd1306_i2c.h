#ifndef SSD1306_I2C_H
#define SSD1306_I2C_H

#include "c_types.h"

/*
 * Dedicated software (bit-banged) I2C master for the SSD1306 panel.
 *
 * driver_lib/include/i2c_master.h (ESP8266_RTOS_SDK v2.1.1) hardcodes GPIO2 (SDA) /
 * GPIO4 (SCL), and GPIO2 collides with the Ka-Radio IR receiver (include/ir.h, IR_PIN).
 * This reimplements the same start/stop/byte protocol on GPIO4 (SDA) / GPIO5 (SCL).
 *
 * All GPIO access is done with direct register writes (GPIO_OUT_W1TS/W1TC,
 * GPIO_ENABLE_W1TS, GPIO_IN). The SDK macros GPIO_OUTPUT_SET / GPIO_AS_OUTPUT /
 * GPIO_INPUT_GET are NOT used, because they end up in gpio_output_conf(), which is
 * defined in driver_lib/driver/gpio.c and that file is not part of this build.
 */
#define SSD1306_I2C_SDA_GPIO 4
#define SSD1306_I2C_SCL_GPIO 5

void ssd1306_i2c_master_gpio_init(void);
void ssd1306_i2c_master_init(void);
void ssd1306_i2c_master_start(void);
void ssd1306_i2c_master_stop(void);
void ssd1306_i2c_master_writeByte(uint8_t wrdata);
uint8_t ssd1306_i2c_master_readByte(void);
bool ssd1306_i2c_master_checkAck(void);
void ssd1306_i2c_master_send_ack(void);
void ssd1306_i2c_master_send_nack(void);
void ssd1306_i2c_scan(void);   /* debug: print idle levels and the addresses of the devices on the bus */

#endif
