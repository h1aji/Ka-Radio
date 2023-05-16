#ifndef _SPIRAM_H_
#define _SPIRAM_H_

#define SPI 	0
#define HSPI	1

//for a 23LC1024 chip
#define SPIRAMSIZE (128*1024)

void spiRamInit();
void spiRamRead(int addr, char *buff, int len);
void spiRamWrite(int addr, char *buff, int len);
int  spiRamTest();

#endif
