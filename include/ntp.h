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
bool ntp_get_time_cached(struct tm **dt);

// print locale date time in ISO-8601 local time
void ntp_print_time();

#endif