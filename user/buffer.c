/*
 * Stream buffer with optional external SPIRAM (23LC1024)
 *
 */

#include "esp_common.h"
#include "esp8266/pin_mux_register.h"
#include "esp8266/eagle_soc.h"
#include "esp8266/spi_register.h"

#include "buffer.h"
#include "gpio.h"
#include "spilib.h"
#include "interface.h"

/* Direct register access: avoids the SDK's gpio_output_conf(), which is not linked in this project */
#define SPIRAM_CS_LOW()    GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, (1<<SPIRAM_CS))
#define SPIRAM_CS_HIGH()   GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, (1<<SPIRAM_CS))

bool spiram;                 // true when an external SPIRAM chip was detected at boot
uint32_t BUFFER_SIZE;
uint8_t *buffer;
uint32_t wptr = 0;
uint32_t rptr = 0;
uint8_t bempty = 1;

/* ===================================================================== */
/*  SPIRAM access                                                        */
/* ===================================================================== */

void spiramInit() {
	char test[17]   = {"FFFFFFFF"};
	char testram[17]= {"01234567"};

	/* ===== Configure GPIO ===== */
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);        // route GPIO15 to GPIO first
	SPIRAM_CS_HIGH();                                           // CS high (deselected) before enabling the output
	GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, (1<<SPIRAM_CS));   // enable output driver

	spiram = false;
	spi_speed_down(); //2MHz
	spiramWrite(strlen(test), 0, testram);
	spiramRead(strlen(test), 0, test);
	if (memcmp(test,testram,16) == 0)
		spiram = true;
	if (spiram)
		printf(PSTR("\nSPIRAM detected%c"),0x0d);
	else
		printf(PSTR("\nSPIRAM not detected%c"),0x0d);
}

uint32_t spiramRead(uint32_t size, uint32_t address, uint8_t *dst) {
	uint32_t i = 0;
	spi_take_semaphore();
	spi_speed_up(); //13MHz

	SPIRAM_CS_LOW();

	spi_put_char(0x03);
	spi_put_char((address>>16)&0xFF);
	spi_put_char((address>>8)&0xFF);
	spi_put_char(address&0xFF);
	for(i = 0; i < size; i++) {
		dst[i] = spi_get_char();
	}

	SPIRAM_CS_HIGH();

//	spi_speed_down(); //2MHz
	spi_give_semaphore();
	return i;
}

uint32_t spiramWrite(uint32_t size, uint32_t address, uint8_t *data) {
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

/* ===================================================================== */
/*  Ring buffer (internal RAM, or SPIRAM when detected)                  */
/* ===================================================================== */

ICACHE_FLASH_ATTR void initBuffer()
{
	if (spiram)
	{
		BUFFER_SIZE = SPIRAM_SIZE;
	} else {
		BUFFER_SIZE = 20000; // 12960 14080 16000
		buffer = malloc(BUFFER_SIZE);
	}
}

ICACHE_FLASH_ATTR uint32_t getBufferFree() {
	if(wptr > rptr ) return BUFFER_SIZE - wptr + rptr;
	else if(wptr < rptr) return rptr - wptr;
	else if(bempty) return BUFFER_SIZE; else return 0;
}

ICACHE_FLASH_ATTR bool getBufferEmpty() {
	return bempty;
}

ICACHE_FLASH_ATTR uint32_t getBufferFilled() {
	return BUFFER_SIZE - getBufferFree();
}

ICACHE_FLASH_ATTR uint32_t bufferWrite(uint8_t *data, uint32_t size) {
	if (spiram) {
		if(getBufferFree() < size) size = getBufferFree();
		spiramWrite(size, wptr, data);
		wptr += size;
		if(bempty) bempty = 0;
		if(wptr >= BUFFER_SIZE) wptr -= BUFFER_SIZE;
	} else {
		uint32_t i;
		for(i=0; i<size; i++) {
			if(getBufferFree() == 0) { return i;}
			buffer[wptr++] = data[i];
			if(bempty) bempty = 0;
			if(wptr == BUFFER_SIZE) wptr = 0;
		}
	}
	return size;
}

ICACHE_FLASH_ATTR uint32_t bufferRead(uint8_t *data, uint32_t size) {
	uint32_t i = 0;
	uint32_t bf = getBufferFilled();
	if(size > bf) size = bf;
	if (spiram) {
		spiramRead(size, rptr, data);
		rptr += size;
		if(rptr >= BUFFER_SIZE) rptr -= BUFFER_SIZE;
		if(rptr == wptr) bempty = 1;
	} else {
		for (i = 0; i < size; i++) {
			if(bf == 0) { return i;}
			data[i] = buffer[rptr++];
			if(rptr == BUFFER_SIZE) rptr = 0;
			if(rptr == wptr) bempty = 1;
		}
	}
	return size;
}

ICACHE_FLASH_ATTR void bufferReset() {
	wptr = 0;
	rptr = 0;
	bempty = 1;
}
