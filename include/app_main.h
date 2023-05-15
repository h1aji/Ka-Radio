/******************************************************************************
 * 
 * Copyright 2018 karawin (http://www.karawin.fr)
 *
*******************************************************************************/

#include "stdint.h"
#include "stdlib.h"

#ifndef MAIN_INCLUDE_APP_MAIN_H_
#define MAIN_INCLUDE_APP_MAIN_H_

#define TIMER_DIVIDER 16 	//5000000Hz 5MHz
#define TIMER_DIVIDER1MS TIMER_BASE_CLK/10000 //10000Hz 
#define TIMER_DIVIDER1mS 8 //10000000Hz 10MHz

#define TIMERVALUE(x) (x*5000000ULL )
#define TIMERVALUE1MS(x) (x*10) 
#define TIMERVALUE1mS(x) (x*10000 )

// event for timers and encoder
#define TIMER_SLEEP   0   
#define TIMER_WAKE    1 
#define TIMER_1MS	2
#define TIMER_1mS	3


// Tasks priority
#define PRIO_MAD 		20
#define PRIO_VS1053 	(configMAX_PRIORITIES - 8)
#define PRIO_RMT		5
#define PRIO_UART		2
#define PRIO_CLIENT		10
#define PRIO_SERVER		6
#define PRIO_ADDON		4
#define PRIO_LCD		7
#define PRIO_SUBSERV	5
#define PRIO_TIMER		8
#define PRIO_OTA		5

// CPU for task
#define CPU_MAD			1  // internal decoder and vs1053
#define CPU_RMT			0
#define CPU_UART		0
#define CPU_CLIENT		0
#define CPU_SERVER		0
#define CPU_ADDON		0
#define CPU_LCD			0
#define CPU_SUBSERV		0
#define CPU_TIMER		0
#define CPU_OTA			0

#define TEMPO_SAVE_VOL	10000

typedef enum {
    I2S, DAC_BUILT_IN, PDM, VS1053
} output_mode_t;

typedef struct {
    int type;               /*!< event type */
    int i1;                 /*!< TIMER_xxx timer group */
    int i2;                 /*!< TIMER_xxx timer number */
} queue_event_t;

uint8_t getIvol();
void setIvol( uint8_t vol);
bool bigSram();

void* kmalloc(size_t memorySize);
void* kcalloc(size_t elementCount, size_t elementSize);

char* getIp();
void (*serviceAddon)(void);

#endif /* MAIN_INCLUDE_APP_MAIN_H_ */
