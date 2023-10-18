/******************************************************************************
 *
 * Copyright 2018 karawin (http://www.karawin.fr)
 *
*******************************************************************************/

#pragma once
#ifndef __FLASH_H__
#define __FLASH_H__

#include "nvs_flash.h"

// get the hardware partition infos
esp_err_t open_partition(const char *partition_label, const char *namespace,nvs_open_mode open_mode,nvs_handle *handle);
void close_partition(nvs_handle handle,const char *partition_label);
void gpio_get_label(char** label);
void gpio_get_comment(char** label);
void gpio_get_active_buttons(bool *abtn0, bool *abtn1);
void gpio_get_i2c(gpio_num_t *scl,gpio_num_t *sda,gpio_num_t *rsti2c);
void gpio_get_ir_signal(gpio_num_t *ir);
bool gpio_get_ir_key(nvs_handle handle,const char *key, uint32_t *out_value1 , uint32_t *out_value2);
void option_get_lcd_info(uint8_t *enca,uint8_t* rt);
void option_set_lcd_info(uint8_t enca, uint8_t rt);
void option_get_ddmm(uint8_t *enca);
void option_set_ddmm(uint8_t enca);
void option_get_lcd_out(uint32_t *enca,uint32_t *encb);
void option_get_lcd_blv(int *blv);
void option_set_lcd_stop(uint32_t enca);
void option_set_lcd_out(uint32_t enca);
void option_set_lcd_blv(int blv);

#endif
