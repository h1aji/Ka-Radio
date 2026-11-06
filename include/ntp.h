/*
 * Copyright 2016 karawin (http://www.karawin.fr)
*/

#ifndef __NTP_H__
#define __NTP_H__

#include <time.h>

#include "c_types.h"

typedef struct {
    uint8_t options;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;
    uint32_t root_delay;
    uint32_t root_disp;
    uint32_t ref_id;
    uint8_t ref_time[8];
    uint8_t orig_time[8];
    uint8_t recv_time[8];
    uint8_t trans_time[8];
} ntp_t;

// Does a live NTP network query. For an on-demand "check right now" call
// (e.g. the "date" console command).
bool ntp_get_time(struct tm **dt);

// Returns the current UTC time, re-syncing over the network at most once
// an hour. Use this instead of ntp_get_time() for anything that needs the
// time regularly (e.g. a clock display).
bool ntp_get_local_time(struct tm **dt);

// print locale date time in ISO-8601 local time
void ntp_print_time();

// Shift a UTC tm in place to local time: adds the saved timezone offset and,
// if applicable, DST. Returns the base timezone offset (without DST) in hours.
int8_t applyTZ(struct tm *time);

// Advances ntp_get_local_time()'s real-time clock by 10ms. Call this once every
// real 10ms from a hardware-timer callback (e.g. housekeepingTimer in user_main.c),
// NOT from anything measured in FreeRTOS scheduler ticks: unlike xTaskGetTickCount(),
// a hardware timer's period doesn't depend on portTICK_RATE_MS actually matching the
// RTOS's configured tick rate, a mismatch which otherwise throws the time-of-day
// clock off by a fixed ratio between NTP syncs.
void ntp_tick10ms(void);

#endif
