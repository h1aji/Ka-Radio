/*
 * Stream buffer with optional external SPIRAM (23LC1024)
 *
 */

#include "esp_common.h"
#include "esp8266/pin_mux_register.h"
#include "esp8266/eagle_soc.h"
#include "gpio.h"

#include <stdlib.h>
#include <string.h>

#include "buffer.h"
#include "interface.h"
#include "spilib.h"

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

/*
 * Measure the real SPIRAM capacity. Chips ignore the address bits they do not
 * implement, so writing at address n aliases back onto address 0 once n reaches the
 * chip size. Returns 0 when nothing answers on the bus.
 */
static uint32_t spiramProbeSize() {
	uint8_t pa[8] = {0xA5,0x5A,0xC3,0x3C,0x96,0x69,0x0F,0xF0};
	uint8_t pb[8] = {0x5A,0xA5,0x3C,0xC3,0x69,0x96,0xF0,0x0F};	// pa inverted
	uint8_t rd[8];
	uint32_t n;

	spiramWrite(8, 0, pa);
	spiramRead(8, 0, rd);
	if (memcmp(rd,pa,8) != 0)
		return 0;
	for (n = 256; n < (1UL<<24); n <<= 1) {
		spiramWrite(8, n, pb);
		spiramRead(8, 0, rd);
		if (memcmp(rd,pb,8) == 0)	// address 0 changed: n wrapped onto it
			return n;
		spiramRead(8, n, rd);
		if (memcmp(rd,pb,8) != 0)	// address n does not hold data: stop here
			return n;
	}
	return n;
}

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
	if (memcmp(test,testram,16) == 0) {
		spiram = true;
		printf(PSTR("\nSPIRAM detected%c"),0x0d);
		printf(PSTR("\nSPIRAM installed size: %d bytes%c"),spiramProbeSize(),0x0d);
	} else {
		printf(PSTR("\nSPIRAM not detected%c"),0x0d);
	}
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
	if(wptr > rptr) return BUFFER_SIZE - wptr + rptr;
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
		uint32_t nw = wptr + size;
		spiramWrite(size, wptr, data);
		if(nw >= BUFFER_SIZE) nw -= BUFFER_SIZE;	// wrap before publishing, the reader must never see wptr > BUFFER_SIZE
		wptr = nw;
		if(bempty) bempty = 0;
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
