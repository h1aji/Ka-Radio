
#pragma once

#include "nvs_flash.h"
#include "driver/spi.h"
#include "driver/adc.h"
#include "driver/gpio.h"

#include "app_main.h"

#define GPIO_NONE 255


// Must be HSPI or VSPI
#define KSPI HSPI_HOST

// KSPI pins of the SPI bus
//-------------------------
#define PIN_NUM_MISO GPIO_NUM_12 	// Master Input, Slave Output
#define PIN_NUM_MOSI GPIO_NUM_13	// Master Output, Slave Input   Named Data or SDA or D1 for oled
#define PIN_NUM_CLK  GPIO_NUM_14 	// Master clock  Named SCL or SCK or D0 for oled

// status led if any.
//------------------- 
// Set the right one with command sys.led
// GPIO can be changed with command sys.ledgpio("x")
#define GPIO_LED	GPIO_NONE		// Flashing led or Playing led

// gpio of the vs1053
//-------------------
#define PIN_NUM_XCS  GPIO_NUM_2
#define PIN_NUM_RST  GPIO_NONE
#define PIN_NUM_XDCS GPIO_NUM_16
#define PIN_NUM_DREQ GPIO_NUM_10
// + KSPI pins

// Encoder knob
//-------------
#define PIN_ENC0_A   GPIO_NONE	    //16	// 255 if encoder not used
#define PIN_ENC0_B   GPIO_NONE	    //17	// DT
#define PIN_ENC0_BTN GPIO_NONE		//5     // SW
#define PIN_ENC1_A   GPIO_NONE		// 255 if encoder not used
#define PIN_ENC1_B   GPIO_NONE		// DT
#define PIN_ENC1_BTN GPIO_NONE		// SW

// 3 Buttons
//-------------
#define PIN_BTN0_A   GPIO_NONE		
#define PIN_BTN0_B   GPIO_NONE		
#define PIN_BTN0_C   GPIO_NONE		
#define PIN_BTN1_A   GPIO_NONE		
#define PIN_BTN1_B   GPIO_NONE		
#define PIN_BTN1_C 	 GPIO_NONE		


// Joystick (2 buttons emulation on ADC)
//--------------------------------------
#define PIN_JOY_0	GPIO_NONE
#define PIN_JOY_1	GPIO_NONE

// I2C lcd (and rda5807 if lcd is i2c or LCD_NONE)
//------------------------------------------------
#define PIN_I2C_SCL GPIO_NUM_5
#define PIN_I2C_SDA GPIO_NUM_4
#define PIN_I2C_RST	GPIO_NONE		// or not used


// SPI lcd
//---------
#define PIN_LCD_CS	GPIO_NUM_5		//CS
#define PIN_LCD_A0	GPIO_NUM_4		//A0 or D/C
#define PIN_LCD_RST	GPIO_NONE		//Reset RES RST or not used
// KSPI pins +

// IR Signal
//-----------
#define PIN_IR_SIGNAL GPIO_NUM_0	// Remote IR source


// I2S DAC or PDM output
//-----------------------
#define PIN_I2S_LRCK GPIO_NONE	// or Channel1
#define PIN_I2S_BCLK GPIO_NONE	// or channel2
#define PIN_I2S_DATA GPIO_NONE	//  

// ADC for keyboard buttons
#define PIN_ADC	GPIO_NONE	    //GPIO_NUM_32 TO GPIO_NUM_39 or GPIO_NONE if not used.

// LCD backlight control
#define PIN_LCD_BACKLIGHT	GPIO_NONE // the gpio to be used in custom.c 

// touch screen  T_DO is MISO, T_DIN is MOSI, T_CLK is CLk of the spi bus
#define PIN_TOUCH_CS	GPIO_NONE //Chip select T_CS

// SPI RAM 23LC1024
#define PIN_SRAM_CS	    GPIO_NUM_15 // Chip select SPI RAM

// Sleep Input. https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html 
//-------------
#define PIN_SLEEP   GPIO_NONE // 13 . Enter Deep Sleep if pin P_SLEEP is set to P_LEVEL_SLEEP. Only GPIOs which have RTC functionality can be used: 0,2,4,12-15,25-27,32-39. And note that GPIO12 is a bootstrap pin, ESP32 might not even start up if GPIO12 is grounded.
#define LEVEL_SLEEP   1		  // Level of P_SLEEP to enter Deep Sleep.

// init a gpio as output
void gpio_output_conf(gpio_num_t gpio);

// get the hardware partition infos
esp_err_t open_partition(const char *partition_label, const char *namespace,nvs_open_mode open_mode,nvs_handle *handle);
void close_partition(nvs_handle handle,const char *partition_label);
void gpio_get_label(char** label);
void gpio_get_comment(char** label);
void gpio_get_spi_bus(uint8_t *spi_no,gpio_num_t *miso,gpio_num_t *mosi,gpio_num_t *sclk);
void gpio_get_vs1053(gpio_num_t * xcs,gpio_num_t *rst,gpio_num_t *xdcs,gpio_num_t *dreq);
void option_get_audio_output(output_mode_t *oom);
bool option_get_esplay();
void gpio_get_encoders(gpio_num_t *enca, gpio_num_t *encb, gpio_num_t *encbtn, gpio_num_t *enca1, gpio_num_t *encb1, gpio_num_t *encbtn1);
void gpio_get_active_buttons(bool *abtn0, bool *abtn1);
void gpio_get_buttons(gpio_num_t *enca, gpio_num_t *encb, gpio_num_t *encc, gpio_num_t *enca1, gpio_num_t *encb1, gpio_num_t *encc1);
void gpio_get_joysticks(gpio_num_t *enca,gpio_num_t *enca1);
void gpio_get_i2c(gpio_num_t *scl,gpio_num_t *sda,gpio_num_t *rsti2c);
void gpio_get_spi_lcd(gpio_num_t *cs ,gpio_num_t *a0,gpio_num_t *rstlcd);
void gpio_get_ir_signal(gpio_num_t *ir);
void gpio_get_adc(adc_mode_t  *channel, adc_mode_t *chanbatt);
void gpio_get_lcd_backlightl(gpio_num_t *lcdb);
void gpio_get_i2s(gpio_num_t *lrck ,gpio_num_t *bclk ,gpio_num_t *i2sdata );
bool gpio_get_ir_key(nvs_handle handle,const char *key, uint32_t *out_value1 , uint32_t *out_value2);
void gpio_get_touch(gpio_num_t *cs);
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
uint8_t gpioToChannel(uint8_t gpio);
void gpio_get_pinSleep(gpio_num_t *pin, bool *aLevel);
