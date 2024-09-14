/******************************************************************************
 * Copyright 2013-2015 Espressif Systems
 *
 * FileName: spiram.c
 *
 * Description: 
 * Driver for a 23LC1024 or similar chip connected to the HSPI port.
 * The chip is driven in 1-bit SPI mode: theoretically, we can move data faster 
 * by using double- or quad-SPI mode but that is not implemented here. 
 * The chip also is used like a generic SPI device, nothing memory-mapped 
 * like the main flash. Also: these routines are not thread-safe; 
 * use mutexes around them if you access the SPI RAM from different threads.
 *
 * Modification history:
 *     2015/06/01, v1.0 File created.
*******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "esp8266/eagle_soc.h"
#include "esp8266/spi_struct.h"
#include "esp8266/pin_mux_register.h"
#include "esp8266/spi_register.h"
#include "esp8266/gpio_struct.h"
#include "esp_log.h"
#include "spiram.h"

#define TAG	"Spiram"

#define SPI 	0
#define HSPI	1

int spiramEnabled = 0;

//Initialize the SPI port to talk to the chip.
void spiRamInit() {
	char dummy[64];
	ESP_LOGI(TAG, "Init 23LC1024");

	// Configure the SPI interface
	//WRITE_PERI_REG(PERIPHS_IO_MUX,0x105); // Configure pins to use for SPI
	//PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U,FUNC_HSPIQ_MISO); // GPIO12 for MISO
	//PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U,FUNC_HSPID_MOSI); // GPIO13 for MOSI
	//PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U,FUNC_HSPI_CLK);   // GPIO14 for CLK
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U,FUNC_HSPI_CS0);     // GPIO15 for CS

	WRITE_PERI_REG(SPI_CLOCK(HSPI), 
					((9&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
					((3&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
					((1&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
					((3&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S)); 

	//Dummy read to clear any weird state the SPI ram chip may be in
	spiRamRead(0x0, dummy, 64);
}

//Macro to quickly access the W-registers of the SPI peripherial
#define SPI_W(i, j)           (REG_SPI_BASE(i) + 0x40 + ((j)*4))


//Read bytes from a memory location. The max amount of bytes that can be read is 64.
void spiRamRead(int addr, char *buff, int len) {
	//int *p=(int*)buff;
	int d;
	int i=0;

	//Set SPI clock to 20 MHz
	WRITE_PERI_REG(SPI_CLOCK(HSPI), 
					(((0)&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
					(((3)&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
					(((1)&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
					(((3)&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S));

	while(READ_PERI_REG(SPI_CMD(HSPI))&SPI_USR) ;
	SET_PERI_REG_MASK(SPI_USER(HSPI),SPI_CS_SETUP|SPI_CS_HOLD|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_MISO);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI),SPI_FLASH_MODE|SPI_USR_MOSI);
	WRITE_PERI_REG(SPI_USER1(HSPI),((0&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S)|	//no data out
			((((8*len)-1)&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S)|					//len bits of data in
			((23&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S));							//address is 24 bits A0-A23
	WRITE_PERI_REG(SPI_ADDR(HSPI),addr<<8);												//write address
	WRITE_PERI_REG(SPI_USER2(HSPI),(((7&SPI_USR_COMMAND_BITLEN)<<SPI_USR_COMMAND_BITLEN_S) | 0x03)); //set read mode

	SET_PERI_REG_MASK(SPI_CMD(HSPI),SPI_USR);
	while(READ_PERI_REG(SPI_CMD(HSPI))&SPI_USR);
	//Unaligned dest address. Copy 8bit at a time
	while (len>0) {
		d=READ_PERI_REG(SPI_W(HSPI, i));
		buff[i*4+0]=(d>>0)&0xff;
		if (len>=1) buff[i*4+1]=(d>>8)&0xff;
		if (len>=2) buff[i*4+2]=(d>>16)&0xff;
		if (len>=3) buff[i*4+3]=(d>>24)&0xff;
		len-=4;
		i++;
	}
}

//Write bytes to a memory location. The max amount of bytes that can be written is 64.
void spiRamWrite(int addr, char *buff, int len) {
	int i;
	int d;

	//Set SPI clock to 20 MHz
	WRITE_PERI_REG(SPI_CLOCK(HSPI), 
					(((0)&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
					(((3)&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
					(((1)&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
					(((3)&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S));

	while(READ_PERI_REG(SPI_CMD(HSPI))&SPI_USR);
	SET_PERI_REG_MASK(SPI_USER(HSPI),SPI_CS_SETUP|SPI_CS_HOLD|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_MOSI);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI),SPI_FLASH_MODE|SPI_USR_MISO);
	WRITE_PERI_REG(SPI_USER1(HSPI),((((8*len)-1)&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S)|	//len bits of data out
			((0&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S)|									//no data in
			((23&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S));									//address is 24 bits A0-A23
	WRITE_PERI_REG(SPI_ADDR(HSPI),addr<<8);														//write address
	WRITE_PERI_REG(SPI_USER2(HSPI),(((7&SPI_USR_COMMAND_BITLEN)<<SPI_USR_COMMAND_BITLEN_S) | 0x02)); //set write mode

	//Assume unaligned src: Copy byte-wise.
	for (i=0; i<(len+3)/4; i++) {
		d=buff[i*4+0]<<0;
		d|=buff[i*4+1]<<8;
		d|=buff[i*4+2]<<16;
		d|=buff[i*4+3]<<24;
		WRITE_PERI_REG(SPI_W(HSPI, (i)), d);
	}
	SET_PERI_REG_MASK(SPI_CMD(HSPI), SPI_USR);
}

//Simple routine to see if the SPI actually stores bytes. 
//This is not a full memory test, but will tell you if the RAM chip is connected well.
int spiRamTest() {
	int x;
	int err=0;
	char a[64];
	char b[64];
	char aa, bb;
	for (x=0; x<64; x++) {
		a[x]=x^(x<<2);
		b[x]=0xaa^x;
	}
	spiRamWrite(0x0, a, 64);
	spiRamWrite(0x100, b, 64);

	spiRamRead(0x0, a, 64);
	spiRamRead(0x100, b, 64);
	for (x=0; x<64; x++) {
		aa=x^(x<<2);
		bb=0xaa^x;
		if (aa!=a[x]) {
			err=1;
			ESP_LOGV(TAG,"aa: 0x%x != 0x%x\n", aa, a[x]);
		}
		if (bb!=b[x]) {
			err=1;
			ESP_LOGV(TAG,"bb: 0x%x != 0x%x\n", bb, b[x]); 
		}
	}
	spiramEnabled = !err; // Set the global variable based on the test result
	return !err;
}
