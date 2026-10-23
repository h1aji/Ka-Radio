/*
 * Copyright 2016 karawin (http://www.karawin.fr)
 *
 * Stream buffer with optional external SPIRAM (23LC1024)
 *
 */

#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "c_types.h"

#define SPIRAM_SIZE 131072
#define SPIRAM_CS   15        // GPIO used as chip select of the SPIRAM

extern bool spiram;          // true when an external SPIRAM chip was detected at boot
extern uint32_t BUFFER_SIZE;

/* SPIRAM access */
void spiramInit();
uint32_t spiramRead(uint32_t size, uint32_t address, uint8_t *dst);
uint32_t spiramWrite(uint32_t size, uint32_t address, uint8_t *data);

/* ring buffer */
void bufferInit();
uint32_t getBufferFree();
bool getBufferEmpty();
uint32_t getBufferFilled();
uint32_t bufferWrite(uint8_t *data, uint32_t size);
uint32_t bufferRead(uint8_t *data, uint32_t size);
void bufferReset();

#endif
