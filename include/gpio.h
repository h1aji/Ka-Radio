/******************************************************************************
 * 
 * Copyright 2018 karawin (http://www.karawin.fr)
 *
*******************************************************************************/

#pragma once

#include "nvs_flash.h"
#include "driver/spi.h"
#include "driver/adc.h"
#include "driver/gpio.h"

#include "app_main.h"

#define GPIO_NONE 255

// I2C lcd (and rda5807 if lcd is i2c or LCD_NONE)
//------------------------------------------------
#define PIN_I2C_SCL GPIO_NUM_5
#define PIN_I2C_SDA GPIO_NUM_4
#define PIN_I2C_RST	GPIO_NONE		// or not used

// init a gpio as output
void gpio_output_conf(gpio_num_t gpio);

// get the hardware partition infos
esp_err_t open_partition(const char *partition_label, const char *namespace,nvs_open_mode open_mode,nvs_handle *handle);
void close_partition(nvs_handle handle,const char *partition_label);
void gpio_get_label(char** label);
void gpio_get_comment(char** label);
void gpio_get_active_buttons(bool *abtn0, bool *abtn1);
void gpio_get_i2c(gpio_num_t *scl,gpio_num_t *sda,gpio_num_t *rsti2c);
void gpio_get_ir_signal(gpio_num_t *ir);
bool gpio_get_ir_key(nvs_handle handle,const char *key, uint32_t *out_value1 , uint32_t *out_value2);
void gpio_get_ledgpio(gpio_num_t *enca);
void gpio_set_ledgpio(gpio_num_t enca);
void option_get_lcd_info(uint8_t *enca,uint8_t* rt);
void option_set_lcd_info(uint8_t enca, uint8_t rt);
void option_get_ddmm(uint8_t *enca);
void option_set_ddmm(uint8_t enca);
void option_get_lcd_out(uint32_t *enca,uint32_t *encb);
void option_get_lcd_blv(int *blv);
void option_set_lcd_stop(uint32_t enca);
void option_set_lcd_out(uint32_t enca);
void option_set_lcd_blv(int blv);
