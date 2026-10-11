/*
 * Copyright 2016 Piotr Sperka (http://www.piotrsperka.info)
*/


#include "esp8266/pin_mux_register.h"
#include "esp8266/eagle_soc.h"
#include "esp8266/spi_register.h"

#include "extram.h"
#include "esp_common.h"
#include "gpio.h"
#include "spilib.h"
#include "buffer.h"
#include "interface.h"

/* Direct register access: avoids the SDK's gpio_output_conf(), which is not linked in this project */
#define SPIRAM_CS_LOW()    GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, (1<<SPIRAM_CS))
#define SPIRAM_CS_HIGH()   GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, (1<<SPIRAM_CS))

void extramInit() {
	char test[17]   = {"FFFFFFFF"};
	char testram[17]= {"01234567"};;

	/* ===== Configure GPIO ===== */
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);   // route GPIO15 to GPIO first
	SPIRAM_CS_HIGH();                                      // CS high (deselected) before enabling the output
	GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, (1<<SPIRAM_CS));   // enable output driver

	externram = false;
	spi_speed_down(); //2MHz
	extramWrite(strlen(test), 0, testram);
	extramRead(strlen(test), 0, test);
	if (memcmp(test,testram,16) == 0)
		externram = true;
	if (externram)
		printf(PSTR("\nSPIRAM detected%c"),0x0d);
	else
		printf(PSTR("\nSPIRAM not detected%c"),0x0d);
}

uint32_t extramRead(uint32_t size, uint32_t address, uint8_t *buffer) {
	uint32_t i = 0;
	spi_take_semaphore();
	spi_speed_up(); //13MHz

	SPIRAM_CS_LOW();

	spi_put_char(0x03);
	spi_put_char((address>>16)&0xFF);
	spi_put_char((address>>8)&0xFF);
	spi_put_char(address&0xFF);
	for(i = 0; i < size; i++) {
		buffer[i] = spi_get_char();
	}

	SPIRAM_CS_HIGH();

//	spi_speed_down(); //2MHz
	spi_give_semaphore();
	return i;
}

uint32_t extramWrite(uint32_t size, uint32_t address, uint8_t *data) {
	uint32_t i = 0;
	spi_take_semaphore();
	spi_speed_up(); //13MHz

	SPIRAM_CS_LOW();

	spi_put_char(0x02);
	spi_put_char((address>>16)&0xFF);
	spi_put_char((address>>8)&0xFF);
	spi_put_char(address&0xFF);
	for(i = 0; i < size; i++) {
		spi_put_char(data[i]);
	}

	SPIRAM_CS_HIGH();

//	spi_speed_down(); //2MHz
	spi_give_semaphore();
	return i;
}
