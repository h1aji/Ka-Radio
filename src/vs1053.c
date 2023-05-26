/******************************************************************************
 * 
 * Copyright 2017 karawin (http://www.karawin.fr)
 *
*******************************************************************************/

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#define TAG "VS1053"

#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp8266/spi_struct.h"
#include "esp8266/spi_register.h"
#include "driver/gpio.h"

#include "audio_player.h"
#include "buffer.h"

#include "app_main.h"
#include "eeprom.h"
#include "interface.h"
#include "vs1053.h"


#define TMAX  4096
#define CHUNK 32

#define SPI 	0
#define HSPI	1

extern void LoadUserCodes(void);

int vsVersion = -1; // the version of the chip
//	SS_VER is 0 for VS1001, 1 for VS1011, 2 for VS1002, 3 for VS1003, 4 for VS1053 and VS8053, 5 for VS1033, 7 for VS1103, and 6 for VS1063.

static SemaphoreHandle_t sSPI = NULL;

uint8_t spi_take_semaphore() {
	if(sSPI) 
		if(xSemaphoreTake(sSPI, portMAX_DELAY))
			return 1;
	return 0;
}

void spi_give_semaphore() {
	if(sSPI)
		xSemaphoreGive(sSPI);
}

bool VS1053_HW_init() {

	ESP_LOGI(TAG, "Init VS1053 pins");

	// Set CS pin as output and high
	gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
	gpio_set_level(GPIO_NUM_2, 1);

	// Set DCS pin as output and high
	gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT);
	gpio_set_level(GPIO_NUM_16, 1);

	// Set DREQ pin as input
	gpio_set_direction(GPIO_NUM_10, GPIO_MODE_INPUT);
	//gpio_set_pull_mode(GPIO_NUM_10, GPIO_PULLDOWN_ENABLE); //usefull for no vs1053 test

	ESP_LOGI(TAG, "Init VS1053 SPI");

	if(!sSPI) vSemaphoreCreateBinary(sSPI);
	spi_give_semaphore();

	WRITE_PERI_REG(PERIPHS_IO_MUX, 0x105|(0<<9)); 		//Set bit 9 if 80MHz sysclock required
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_HSPIQ_MISO); //GPIO12 is HSPI MISO pin (Master Data In)
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_HSPID_MOSI); //GPIO13 is HSPI MOSI pin (Master Data Out)
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_HSPI_CLK);	//GPIO14 is HSPI CLK pin (Clock)
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);	//GPIO2 is set as CS pin (Chip Select / Slave Select)


  	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_WR_BYTE_ORDER);	//SPI TX Byte order High to Low
  	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_RD_BYTE_ORDER);	//SPI RX Byte order High to Low

	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_CS_SETUP|SPI_CS_HOLD|SPI_USR_COMMAND);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI), SPI_FLASH_MODE);

	return true;
}

void VS1053_SPI_SpeedUp() {
	// 10MHz
	WRITE_PERI_REG(SPI_CLOCK(HSPI), 
			((1&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
			((3&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
			((1&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
			((3&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S)); //clear bit 31,set SPI clock div
}

void VS1053_SPI_SpeedDown() {
	// 2MHz
	WRITE_PERI_REG(SPI_CLOCK(HSPI), 
			((9&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
			((3&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
			((1&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
			((3&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S)); 
}

int getVsVersion() {
	return vsVersion;
}

uint8_t SPIGetChar() {
	while(READ_PERI_REG(SPI_CMD(HSPI))&SPI_USR);	//wait for SPI to be ready       

	CLEAR_PERI_REG_MASK(SPI_USER(HSPI),SPI_FLASH_MODE|SPI_USR_MOSI);
  
	SET_PERI_REG_MASK(SPI_USER(HSPI),SPI_USR_MISO);

	WRITE_PERI_REG(SPI_USER1(HSPI),(0&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S|	//number of bits to send
			(7&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S|			//number of bits to receive
			(23&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S);		//number of bits in address

	SET_PERI_REG_MASK(SPI_CMD(HSPI),SPI_USR);
	while(READ_PERI_REG(SPI_CMD(HSPI))&SPI_USR) ;

	if (READ_PERI_REG(SPI_USER(HSPI))&SPI_RD_BYTE_ORDER) {
		return READ_PERI_REG(SPI_W0(HSPI))>>24;	//assuming data in is written to MSB. TBC
	} else {
		return READ_PERI_REG(SPI_W0(HSPI));
	}
}

void SPIPutChar(uint8_t data) {
	while(READ_PERI_REG(SPI_CMD(HSPI))&SPI_USR);	//wait for SPI to be ready  

	CLEAR_PERI_REG_MASK(SPI_USER(HSPI),SPI_FLASH_MODE|SPI_USR_MISO);

	SET_PERI_REG_MASK(SPI_USER(HSPI),SPI_USR_MOSI);	//enable MOSI function in SPI module

	WRITE_PERI_REG(SPI_USER1(HSPI),((7&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S)| //len bits of data out
			((0&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S)|		//no data in
			((23&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S));		//address is 24 bits A0-A23

	if (READ_PERI_REG(SPI_USER(HSPI))&SPI_WR_BYTE_ORDER) {
		WRITE_PERI_REG(SPI_W0(HSPI),(uint32_t)data<<24);
	} else {
		WRITE_PERI_REG(SPI_W0(HSPI),(uint32_t)data);
	}
	SET_PERI_REG_MASK(SPI_CMD(HSPI),SPI_USR);
}

void ControlReset(uint8_t State) {
	gpio_set_level(GPIO_NUM_2, State);
	gpio_set_level(GPIO_NUM_16, State);
}

void SCI_ChipSelect(uint8_t State) {
	gpio_set_level(GPIO_NUM_2, State);
}

void SDI_ChipSelect(uint8_t State) {
	gpio_set_level(GPIO_NUM_16, State);
}

uint8_t CheckDREQ() {
	return gpio_get_level(GPIO_NUM_10);
}

void WaitDREQ() {
	uint16_t time_out = 0;
	while(gpio_get_level(GPIO_NUM_10) == 0 && time_out++ < TMAX)
	{
		taskYIELD();
	}
}

void VS1053_WriteRegister(uint8_t addressbyte, uint8_t highbyte, uint8_t lowbyte) {
	spi_take_semaphore();
	VS1053_SPI_SpeedDown();
	SDI_ChipSelect(RESET);
	WaitDREQ();
	SCI_ChipSelect(SET);
	SPIPutChar(VS_WRITE_COMMAND);
	SPIPutChar(addressbyte);
	SPIPutChar(highbyte);
	SPIPutChar(lowbyte);
	WaitDREQ();
	SCI_ChipSelect(RESET);
//	VS1053_SPI_SpeedUp();
	spi_give_semaphore();
}

void VS1053_WriteRegister16(uint8_t addressbyte, uint16_t value) {
	spi_take_semaphore();
	VS1053_SPI_SpeedDown();
	SDI_ChipSelect(RESET);
	WaitDREQ();
	SCI_ChipSelect(SET);
	SPIPutChar(VS_WRITE_COMMAND);
	SPIPutChar(addressbyte);
	SPIPutChar((value>>8)&0xff);
	SPIPutChar(value&0xff);
	WaitDREQ();
	SCI_ChipSelect(RESET);
//	VS1053_SPI_SpeedUp();
	spi_give_semaphore();
}

uint16_t VS1053_ReadRegister(uint8_t addressbyte) {
	spi_take_semaphore();
	VS1053_SPI_SpeedDown();
	uint16_t result;
	SDI_ChipSelect(RESET);
	WaitDREQ();
	SCI_ChipSelect(SET);
	SPIPutChar(VS_READ_COMMAND);
	SPIPutChar(addressbyte);
	result = SPIGetChar() << 8;
	result |= SPIGetChar();
	WaitDREQ();
	SCI_ChipSelect(RESET);
//	VS1053_SPI_SpeedUp();
	spi_give_semaphore();
	return result;
}

void WriteVS10xxRegister(unsigned short addr,unsigned short val) {
	VS1053_WriteRegister((uint8_t)addr&0xff, (uint8_t)((val&0xFF00)>>8), (uint8_t)(val&0xFF));
}

void VS1053_ResetChip() {
	ControlReset(SET);
	vTaskDelay(10);
	ControlReset(RESET);
	vTaskDelay(10);
	if (CheckDREQ() == 1) return;
	vTaskDelay(100);
}

uint16_t MaskAndShiftRight(uint16_t Source, uint16_t Mask, uint16_t Shift) {
	return ((Source & Mask) >> Shift);
}

void VS1053_regtest() {
	int MP3Status = VS1053_ReadRegister(SPI_STATUSVS);
	int MP3Mode = VS1053_ReadRegister(SPI_MODE);
	int MP3Clock = VS1053_ReadRegister(SPI_CLOCKF);
	ESP_LOGI(TAG,"SCI_Status  = 0x%X",MP3Status);
	ESP_LOGI(TAG,"SCI_Mode (0x4800) = 0x%X",MP3Mode);
	ESP_LOGI(TAG,"SCI_ClockF = 0x%X",MP3Clock);
	ESP_LOGI(TAG,"VS Version (VS1053 is 4) = %d",vsVersion);
	//The 1053B should respond with 4. VS1001 = 0, VS1011 = 1, VS1002 = 2, VS1003 = 3, VS1054 = 4
}

void VS1053_I2SRate(uint8_t speed) { // 0 = 48kHz, 1 = 96kHz, 2 = 128kHz
    if (speed > 2) speed = 0;
	if (vsVersion != 4) return;
	VS1053_WriteRegister16(SPI_WRAMADDR, 0xc040); //address of GPIO_ODATA is 0xC017	
	VS1053_WriteRegister16(SPI_WRAM, 0x0008|speed); //
	VS1053_WriteRegister16(SPI_WRAMADDR, 0xc040); //address of GPIO_ODATA is 0xC017	
	VS1053_WriteRegister16(SPI_WRAM, 0x000C|speed); //
	ESP_LOGI(TAG,"I2S Speed: %d",speed);
}
void VS1053_DisableAnalog() {
	// disable analog output
	VS1053_WriteRegister16(SPI_VOL,0xFFFF);
}

// reduce the chip consumption
void VS1053_LowPower() {
	VS1053_WriteRegister16(SPI_CLOCKF,0x0000); // 
}

// normal chip consumption
void VS1053_HighPower() {
   if (vsVersion == 4) // only 1053  	
		VS1053_WriteRegister16(SPI_CLOCKF,0xB800); // SC_MULT = x1, SC_ADD= x1
	else	
		VS1053_WriteRegister16(SPI_CLOCKF,0xb000);	
}

// patch if GPIO1 is not wired to GND
void VS1053_GPIO1() {
// these 4 lines makes board to run on mp3 mode, no soldering required anymore
		VS1053_WriteRegister16(SPI_WRAMADDR, 0xc017); //address of GPIO_DDR is 0xC017
		VS1053_WriteRegister16(SPI_WRAM, 0x0003); //GPIO_DDR=3
		VS1053_WriteRegister16(SPI_WRAMADDR, 0xc019); //address of GPIO_ODATA is 0xC019
		VS1053_WriteRegister16(SPI_WRAM, 0x0000); //GPIO_ODATA=0	
		ESP_LOGI(TAG,"SPI_AUDATA 1 = %x",VS1053_ReadRegister(SPI_AUDATA));
}

// First VS10xx configuration after reset
void VS1053_InitVS() {
   if (vsVersion == 4) // only 1053b  	
//		VS1053_WriteRegister(SPI_CLOCKF,0x78,0x00); // SC_MULT = x3, SC_ADD= x2
		VS1053_WriteRegister16(SPI_CLOCKF,0xB800); // SC_MULT = x1, SC_ADD= x1
//		VS1053_WriteRegister16(SPI_CLOCKF,0x8800); // SC_MULT = x3.5, SC_ADD= x1
//		VS1053_WriteRegister16(SPI_CLOCKF,0x9000); // SC_MULT = x3.5, SC_ADD= x1.5
	else	
		VS1053_WriteRegister16(SPI_CLOCKF,0xB000);
	
	VS1053_WriteRegister(SPI_MODE, (SM_SDINEW|SM_LINE1)>>8, SM_RESET);
	VS1053_WriteRegister(SPI_MODE, (SM_SDINEW|SM_LINE1)>>8, SM_LAYER12); //mode 
	WaitDREQ();
	
	VS1053_regtest();
	
	// enable I2C dac output of the vs1053
	if (vsVersion == 4) // only 1053
	{
		VS1053_WriteRegister16(SPI_WRAMADDR, 0xc017);
		VS1053_WriteRegister16(SPI_WRAM, 0x00F0);
		VS1053_I2SRate(g_device->i2sspeed);	
	}
}

void VS1053_Start() {
	ControlReset(SET);
	vTaskDelay(10);
	ControlReset(RESET);
	vTaskDelay(100);	
	if (CheckDREQ() == 0) vTaskDelay(50);	// wait a bit more
	//Check DREQ
	if (CheckDREQ() == 0)
	{
		vsVersion = 0; 
		ESP_LOGE(TAG,"NO VS1053 detected");
		return;
	} 

// patch to mp3 mode id needed
//	if (VS1053_ReadRegister(SPI_AUDATA) == 0xac45) // midi mode?
		VS1053_GPIO1();	// patch if GPIO1 is not wired to gnd
	if (VS1053_ReadRegister(SPI_AUDATA) == 0xac45) // try again
	{
		VS1053_ResetChip();
		VS1053_GPIO1();	// patch if GPIO1 is not wired to gnd
	}

	vsVersion = (VS1053_ReadRegister(SPI_STATUSVS) >> 4) & 0x000F; //Mask out only the four version bits
	//0 for VS1001, 1 for VS1011, 2 for VS1002, 3 for VS1003, 4 for VS1053 and VS8053,
	//5 for VS1033, 7 for VS1103, and 6 for VS1063	
	ESP_LOGI(TAG,"VS10xx detection. Version: %x",vsVersion);

	// plugin patch
	if ((vsVersion == 4) && ((g_device->options&T_PATCH)==0)) 
	{	
		LoadUserCodes() ;	// vs1053b patch
		ESP_LOGI(TAG,"SPI_AUDATA 2 = %x",VS1053_ReadRegister(SPI_AUDATA));
		if (VS1053_ReadRegister(SPI_AUDATA) == 0xAC45) //midi mode?
		{
			VS1053_WriteRegister(SPI_AIADDR,0x00,0x50); // reset soft but let  patch loaded				
			VS1053_GPIO1();	// patch if GPIO1 is not wired to gnd
			if (VS1053_ReadRegister(SPI_AUDATA) == 0xAC45) // in midi mode
			{	//fed up
				ESP_LOGI(TAG,"midi mode on\n");
				g_device->options |= T_PATCH; // force no patch
				saveDeviceSettings(g_device);
				esp_restart();
			}
		}
	} 
	
	VS1053_InitVS();
	// disable analog output
	VS1053_WriteRegister16(SPI_VOL,0xFFFF);
	VS1053_DisableAnalog();
	vTaskDelay(1);
	ESP_LOGI(TAG,"volume: %d",g_device->vol);
	setIvol(g_device->vol);
	VS1053_SetVolume(g_device->vol);	
	VS1053_SetTreble(g_device->treble);
	VS1053_SetBass(g_device->bass);
	VS1053_SetTrebleFreq(g_device->freqtreble);
	VS1053_SetBassFreq(g_device->freqbass);
	VS1053_SetSpatial(g_device->spacial);
}

int VS1053_SendMusicBytes(uint8_t* music, uint16_t quantity) {
	if(quantity == 0) return 0;
	spi_take_semaphore();
	int o = 0;

	while(CheckDREQ() == 0)
	{
		vTaskDelay(1);
	}

	VS1053_SPI_SpeedUp();
	SDI_ChipSelect(SET);

	while(quantity)
	{
		if(CheckDREQ()) 
		{
			int t = quantity;
			int k;
			if(t > CHUNK) t = CHUNK;				
			for (k=o; k < o+t; k++)
			{
				SPIPutChar(music[k]);
			}
			o += t;
			quantity -= t;
		} 
	}
	SDI_ChipSelect(RESET);
	VS1053_SPI_SpeedDown();
	spi_give_semaphore();
	return o;
}

// Get volume and convert it in log one
uint8_t VS1053_GetVolume() {
	uint8_t i,j;
	uint8_t value = VS1053_ReadRegister(SPI_VOL) & 0x00FF;
	for (i = 0;i< 255; i++)
	{
		j = (log10(255/((float)i+1)) * 105.54571334); // magic no?
		if (value == j)
		  return i;
	}	
	return 127;
}

// rough volume
uint8_t VS1053_GetVolumeLinear() {
	return VS1053_ReadRegister(SPI_VOL) & 0x00FF;
}

/**
 * Function sets the same volume level to both channels.
 * @param xMinusHalfdB describes damping level as a multiple
 * 		of 0.5dB. Maximum volume is 0 and silence is 0xFEFE.
 * convert the log one to rough one and set it invs1053
 */
void VS1053_SetVolume(uint8_t xMinusHalfdB) {
uint8_t value = (log10(255/((float)xMinusHalfdB+1)) * 105.54571334);	
//printf("setvol: %d\n",value);
	if (value == 255) value = 254;
//printf("xMinusHalfdB=%d  value=%d\n",xMinusHalfdB,value);
	VS1053_WriteRegister(SPI_VOL,value,value);
}

/**
 * Functions returns level of treble enhancement.
 * @return Returned value describes enhancement in multiplies
 * 		of 1.5dB. 0 value means no enhancement, 8 max (12dB).
 */
int8_t	VS1053_GetTreble() {
	int8_t treble = (VS1053_ReadRegister(SPI_BASS) & 0xF000) >> 12;
	if ((treble&0x08)) treble |= 0xF0; // negative value
	return (treble);
}

/**
 * Sets treble level.
 * @note If xOneAndHalfdB is greater than max value, sets treble
 * 		to maximum.
 * @param xOneAndHalfdB describes level of enhancement. It is a multiplier
 * 		of 1.5dB. 0 - no enhancement, -8 minimum -12dB , 7 - maximum, 10.5dB.
 * @return void
 */
void VS1053_SetTreble(int8_t xOneAndHalfdB) {
	uint16_t bassReg = VS1053_ReadRegister(SPI_BASS);
	
	if (( xOneAndHalfdB <= 7) && ( xOneAndHalfdB >=-8))
		VS1053_WriteRegister(SPI_BASS, MaskAndShiftRight(bassReg,0x0F00,8) | (xOneAndHalfdB << 4), bassReg & 0x00FF);
}

/**
 * Sets low limit frequency of treble enhancer.
 * @note new frequency is set only if argument is valid.
 * @param xkHz The lowest frequency enhanced by treble enhancer.
 * 		Values from 0 to 15 (in kHz)
 * @return void
 */
void VS1053_SetTrebleFreq(uint8_t xkHz) {
	uint16_t bassReg = VS1053_ReadRegister(SPI_BASS);
	if (xkHz <= 15)
		VS1053_WriteRegister(SPI_BASS, MaskAndShiftRight(bassReg,0xF000,8) | xkHz, bassReg & 0x00FF);
}

int8_t VS1053_GetTrebleFreq() {
	return ((VS1053_ReadRegister(SPI_BASS) & 0x0F00) >> 8);
}

/**
 * Returns level of bass boost in dB.
 * @return Value of bass enhancement from 0 (off) to 15(dB).
 */
uint8_t	VS1053_GetBass() {
	return ((VS1053_ReadRegister(SPI_BASS) & 0x00F0) >> 4);
}

/**
 * Sets bass enhancement level (in dB).
 * @note If xdB is greater than max value, bass enhancement is set to its max (15dB).
 * @param xdB Value of bass enhancement from 0 (off) to 15(dB).
 * @return void
 */
void VS1053_SetBass(uint8_t xdB) {
	uint16_t bassReg = VS1053_ReadRegister(SPI_BASS);
	if (xdB <= 15)
		VS1053_WriteRegister(SPI_BASS, (bassReg & 0xFF00) >> 8, (bassReg & 0x000F) | (xdB << 4));
	else
		VS1053_WriteRegister(SPI_BASS, (bassReg & 0xFF00) >> 8, (bassReg & 0x000F) | 0xF0);
}

/**
 * Sets low limit frequency of bass enhancer.
 * @note new frequency is set only if argument is valid.
 * @param xTenHz The lowest frequency enhanced by bass enhancer.
 * 		Values from 2 to 15 ( equal to 20 - 150 Hz).
 * @return void
 */
void VS1053_SetBassFreq(uint8_t xTenHz) {
	uint16_t bassReg = VS1053_ReadRegister(SPI_BASS);
	if (xTenHz >=2 && xTenHz <= 15)
		VS1053_WriteRegister(SPI_BASS, MaskAndShiftRight(bassReg,0xFF00,8), (bassReg & 0x00F0) | xTenHz);
}

uint8_t	VS1053_GetBassFreq() {
	return ((VS1053_ReadRegister(SPI_BASS) & 0x000F));
}

uint8_t	VS1053_GetSpatial(){
	if (vsVersion != 4) return 0;
	uint16_t spatial = (VS1053_ReadRegister(SPI_MODE) & 0x0090) >>4;
	return ((spatial&1) | ((spatial>>2) & 2));
}

void VS1053_SetSpatial(uint8_t num) {
	if (vsVersion != 4) return ;
	uint16_t spatial = VS1053_ReadRegister(SPI_MODE);
	if (num <= 3)
	{	
		num = (((num <<2)&8) | (num&1))<<4;
		VS1053_WriteRegister(SPI_MODE, MaskAndShiftRight(spatial,0xFF00,8), (spatial & 0x006F) | num);
	}	
}

uint16_t VS1053_GetDecodeTime() {
	return VS1053_ReadRegister(SPI_DECODE_TIME);
}

uint16_t VS1053_GetBitrate() {
	uint16_t bitrate = (VS1053_ReadRegister(SPI_HDAT0) & 0xf000) >> 12;
	uint8_t ID = (VS1053_ReadRegister(SPI_HDAT1) & 0x18) >> 3;
	uint16_t res;
	if (ID == 3)
	{	res = 32;
		while(bitrate>13)
		{
			res+=64;
			bitrate--;
		}
		while (bitrate>9)
		{
			res+=32;
			bitrate--;
		}
		while (bitrate>5)
		{
			res+=16;
			bitrate--;
		}
		while (bitrate>1)
		{
			res+=8;
			bitrate--;
		}
	}
	else
	{	res = 8;

		while (bitrate>8)
		{
			res+=16;
			bitrate--;
		}
		while (bitrate>1)
		{
			res+=8;
			bitrate--;
		}
	}
	return res;
}

uint16_t VS1053_GetSampleRate() {
	return (VS1053_ReadRegister(SPI_AUDATA) & 0xFFFE);
}

/* to  stop a  stream */
void VS1053_flush_cancel() {
	int8_t endFillByte ;
	int16_t y;
	uint8_t buf[33];	
	// set spimode with SM_CANCEL
	uint16_t spimode = VS1053_ReadRegister(SPI_MODE)| SM_CANCEL;
  // set CANCEL
	VS1053_WriteRegister(SPI_MODE, MaskAndShiftRight(spimode,0xFF00,8), (spimode & 0x00FF));
	// wait CANCEL
	VS1053_WriteRegister16(SPI_WRAMADDR, para_endFillByte);
	endFillByte = (int8_t) (VS1053_ReadRegister(SPI_WRAM) & 0xFF);
	for (y = 0; y < 32; y++) buf[y] = endFillByte;	 
	y = 0;
	while (VS1053_ReadRegister(SPI_MODE)& SM_CANCEL)
	{	  
		VS1053_SendMusicBytes( buf, 32); 
		if (y++ > 64) 
		{
			ESP_LOGE(TAG,"VS1053 Reset");
//			VS1053_Start();
			break;
		}		
	}	

	for (y = 0; y < 64; y++) 
		VS1053_SendMusicBytes(buf, 32); //2080 bytes
}

void vsTask(void *pvParams) { 
#define VSTASKBUF	1024
	portBASE_TYPE uxHighWaterMark;
	uint8_t  b[VSTASKBUF];
	uint16_t size ,s;

	player_t *player = pvParams;
	
	while(1) {
		// stop requested, terminate immediately
        if(player->decoder_command == CMD_STOP) {
            break;
        }	

		unsigned fsize = spiRamFifoFill();
		size = min(VSTASKBUF, fsize);
/*		if (size > 	VSTASKBUF)
		{
			ESP_LOGE(TAG, "Decoder vs1053 size: %d, fsize: %d, VSTASKBUF: %d .\n",size,fsize,VSTASKBUF );	
			size = 	VSTASKBUF;
		}
*/		
		if (size > 0)
		{
			spiRamFifoRead((char*)b, size);
			s = 0; 			
			while(s < size) 
			{
				s += VS1053_SendMusicBytes(b+s, size-s);	
			}
		} else vTaskDelay(10);
		vTaskDelay(2);		
	}

    player->decoder_status = STOPPED;
    player->decoder_command = CMD_NONE;
	spiRamFifoReset();
    ESP_LOGD(TAG, "Decoder vs1053 stopped.\n");
	uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
	ESP_LOGI(TAG,"watermark: %x  %d",uxHighWaterMark,uxHighWaterMark);	
	vTaskDelete(NULL);
}
