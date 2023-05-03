#ifndef __INTERFACE_H__
#define __INTERFACE_H__

#include "esp_log.h"
#include "telnet.h"

#undef PSTR
#undef PROGMEM
//#define PROGMEM   ICACHE_RODATA_ATTR

#define STORE_ATTR          __attribute__((aligned(4)))

#define PROGMEM  __attribute__((section(".irom.text")))
#define PSTR(s) (__extension__({ static const char __c[] STORE_ATTR __attribute__((section(".irom.text"))) = {s}; &__c[0];}))
#define MAXDATAT 256


#define RELEASE "3.0"
#define REVISION "1"


extern unsigned short adcdiv;
extern char* getIp() ;
void switchCommand(void );
void checkCommand(int size, char* s);
void wifiConnectMem();
char* webInfo();
char* webList(int id);
void setHostname(char* s);

int kasprintf(char *str, const char *format, ...);

#define kprintf(fmt, ...) do {    \
		telnetWrite(printf(fmt, ##__VA_ARGS__),fmt, ##__VA_ARGS__); \
	} while (0)

#endif
