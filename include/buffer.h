/*
 * Copyright 2016 Piotr Sperka (http://www.piotrsperka.info)
 */

#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "c_types.h"

#define BIGMEMORY 131072

extern bool externram;
extern uint32_t BUFFER_SIZE;

uint32_t getBufferFree();
bool getBufferEmpty();
uint32_t getBufferFilled();
uint32_t bufferWrite(uint8_t *data, uint32_t size);
uint32_t bufferRead(uint8_t *data, uint32_t size);
void bufferReset();

#endif
