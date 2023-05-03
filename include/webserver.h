/*
 * Copyright 2016 karawin (http://www.karawin.fr)
 */

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
void stopWake();
void serverclientTask(void *pvParams);

#endif