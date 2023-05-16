/******************************************************************************
 * 
 * Copyright 2018 karawin (http://www.karawin.fr)
 *
*******************************************************************************/

#pragma once

#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "websocket.h"

extern os_timer_t sleepTimer;
extern uint32_t sleepDelay;
extern os_timer_t wakeTimer;
extern uint32_t wakeDelay;
extern xSemaphoreHandle semclient;
extern xSemaphoreHandle semfile;

//void serverTask(void *pvParams);
extern int8_t clientOvol ;
extern uint8_t clientIvol ;
void sleepCallback(void *pArg);
void wakeCallback(void *pArg);
void startSleep(uint32_t delay);
void stopSleep();
void startWake(uint32_t delay);
void stopWake();
void serverclientTask(void *pvParams);
void playStationInt(int sid);
void websockethandle(int socket, wsopcode_t opcode, uint8_t * payload, size_t length);
uint16_t getVolume(void);
void setVolume(char* vol);
void setVolumei(int16_t vol);
void setRelVolume(int8_t vol);

uint64_t getSleep();
uint64_t getWake();

#endif
