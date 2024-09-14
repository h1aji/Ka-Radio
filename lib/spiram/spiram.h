#ifndef _SPIRAM_H_
#define _SPIRAM_H_

extern int spiramEnabled;

void spiRamInit();
void spiRamRead(int addr, char *buff, int len);
void spiRamWrite(int addr, char *buff, int len);
int  spiRamTest();

#endif
