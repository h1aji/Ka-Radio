/******************************************************************************
 *
 * Copyright 2016 karawin (http://www.karawin.fr)
 *
 * Simple NTP client for Ka-Radio
 * list of major public servers http://tf.nist.gov/tf-cgi/servers.cgi
 * time.nist.gov
 ******************************************************************************/

#include "esp_common.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "ntp.h"
#include "eeprom.h"
#include "interface.h"

#define NTP_HOST "pool.ntp.org" // NTP host
#define NTP_PORT "123"          // UDP port

#define NTP_RESYNC_SEC 3600U // re-sync with the server this often once the clock is set
#define NTP_RETRY_SEC 30U    // retry interval while the sync keeps failing

ICACHE_FLASH_ATTR int8_t applyTZ(struct tm *time)
{
    bool dst = false;
    struct device_settings *device;
    int8_t tzo = 0;

    device = getDeviceSettings();
    if (device != NULL) {
        tzo = device->tzoffset;
        free(device);
    }

    // apply base timezone offset
    time->tm_hour += tzo;

    // call mktime to fix up (needed if applying the offset rolled the date
    // back/forward a day); also sets yday and fixes wday if it was wrong
    // from the rtc
    mktime(time);

    // work out if we should apply dst, modify according to your local rules
    if (time->tm_mon < 2 || time->tm_mon > 9) {
        // these months are completely out of DST
    } else if (time->tm_mon > 2 && time->tm_mon < 9) {
        // these months are completely in DST
        dst = true;
    } else {
        // else we must be in one of the change months
        // work out when the last sunday was (could be today)
        int previousSunday = time->tm_mday - time->tm_wday;

        if (time->tm_mon == 2) { // march
            // was last sunday (which could be today) the last sunday in march
            if (previousSunday >= 25) {
                // are we actually on the last sunday today
                if (time->tm_wday == 0) {
                    // if so are we at/past 2am gmt
                    int s = (time->tm_hour * 3600) + (time->tm_min * 60) + time->tm_sec;
                    if (s >= 7200)
                        dst = true;
                } else {
                    dst = true;
                }
            }
        } else if (time->tm_mon == 9) {
            // was last sunday (which could be today) the last sunday in october
            if (previousSunday >= 25) {
                // we have reached/passed it, so is it today?
                if (time->tm_wday == 0) {
                    // change day, so are we before 1am gmt (2am localtime)
                    int s = (time->tm_hour * 3600) + (time->tm_min * 60) + time->tm_sec;
                    if (s < 3600)
                        dst = true;
                }
            } else {
                // not reached the last sunday yet
                dst = true;
            }
        }
    }

    if (dst) {
        // add the dst hour
        time->tm_hour += 1;
        // mktime will fix up the time/date if adding an hour has taken us to the next day
        mktime(time);
        // don't rely on isdst returned by mktime, it doesn't know about timezones and tends to
        // reset this to 0
        time->tm_isdst = 1;
    } else {
        time->tm_isdst = 0;
    }
    return tzo;
}

// Does one live NTP network round trip and returns the result as a raw
// UTC Unix timestamp in *out. ntp_get_time() and ntp_get_time_cached()
// both build on this - it's the only place that actually talks to the
// network.
static bool ntp_fetch(time_t *out)
{
    struct timeval timeout;
    int sockfd = -1;
    ntp_t *ntp;
    char *msg;
    int rv;
    char service[] = {NTP_PORT};
    char node[] = {NTP_HOST};
    struct addrinfo hints, *servinfo = NULL, *p = NULL;
    struct sockaddr_in servaddr; // address actually used to send the request
    struct sockaddr_in fromaddr; // address the reply claims to be from
    socklen_t fromlen;
    uint32_t secs1900;

    timeout.tv_usec = 0;
    timeout.tv_sec = 5000; // recvfrom() timeout: this SDK's lwIP reads tv_sec as milliseconds ("bug
                           // *1000"), so 5000 = 5s

    msg = zalloc(sizeof(ntp_t));
    if (msg == NULL) {
        kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "msg", 0);
        return false;
    }

    // build the message to send
    ntp = (ntp_t *)msg;
    ntp->options = 0x1B; // 3 first flags set in binary: 00 001 011  LI=0 VN=3 MODE=CLIENT
    ntp->stratum = 0;
    ntp->poll = 6;
    ntp->precision = 1;
    // ntp->ref_id = 0x4C4F434C; // LOCL

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // this SDK's lwIP is IPv4 only, same as the rest of this project
    hints.ai_socktype = SOCK_DGRAM; // Use UDP
    if ((rv = getaddrinfo(node, service, &hints, &servinfo)) != 0) {
        kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "getaddrinfo", rv);
        free(msg);
        return false;
    }

    // loop in result socket
    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "sockfd", sockfd);
            continue;
        }
        break;
    }
    if (p == NULL) {
        kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "sockfd", -1);
        free(msg);
        freeaddrinfo(servinfo);
        return false;
    }

    // remember who we asked: recvfrom() below doesn't filter by sender on
    // its own, so any reply has to be checked against this before it's trusted
    memset(&servaddr, 0, sizeof(servaddr));
    memcpy(&servaddr, p->ai_addr, p->ai_addrlen);

    // set a timeout for recvfrom
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
        kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "setsockopt", 0);
        free(msg);
        freeaddrinfo(servinfo);
        close(sockfd);
        return false;
    }

    // send the request
    if ((rv = sendto(sockfd, msg, sizeof(ntp_t), 0, p->ai_addr, p->ai_addrlen)) == -1) {
        kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "sendto", rv);
        free(msg);
        freeaddrinfo(servinfo);
        close(sockfd);
        return false;
    }
    freeaddrinfo(servinfo);

    fromlen = sizeof(fromaddr);
    if ((rv = recvfrom(sockfd, msg, sizeof(ntp_t), 0, (struct sockaddr *)&fromaddr, &fromlen)) <=
        0) {
        kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "recvfrom", rv);
        free(msg);
        close(sockfd);
        return false;
    }
    if (rv != sizeof(ntp_t)) { // truncated/garbage reply
        kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "short reply", rv);
        free(msg);
        close(sockfd);
        return false;
    }
    if (fromaddr.sin_addr.s_addr != servaddr.sin_addr.s_addr) { // not from the server we asked
        kprintf(PSTR("##SYS.DATE#: ntp fails on %s %d\n"), "wrong sender", 0);
        free(msg);
        close(sockfd);
        return false;
    }
    close(sockfd);

    // extract time
    ntp = (ntp_t *)msg;
    // cast to uint32_t before shifting: trans_time[0] << 24 overflows a
    // signed int for every timestamp since 1968, which is undefined behavior
    secs1900 = (uint32_t)ntp->trans_time[0] << 24 | (uint32_t)ntp->trans_time[1] << 16 |
               (uint32_t)ntp->trans_time[2] << 8 | (uint32_t)ntp->trans_time[3];
    // convert to unix time (secs1900 is always well above 2208988800 for any real date)
    *out = (time_t)(secs1900 - 2208988800UL);
    free(msg);
    return true;
}

// Gets the time from the NTP server and returns an allocated tm struct
// (UTC). Always does a live network round trip - use this for an
// on-demand "check right now" query (e.g. the "date" console command).
// For anything that needs the time regularly, call ntp_get_time_cached()
// instead so the network isn't hit on every call.
bool ICACHE_FLASH_ATTR ntp_get_time(struct tm **dt)
{
    time_t t;

    if (!ntp_fetch(&t))
        return false;
    *dt = gmtime(&t);
    return true;
}

// Returns the current UTC time without hitting the network on every call.
// The NTP server is queried right away the first time (as soon as the station has
// an IP address), then every NTP_RESYNC_SEC after a success, or every NTP_RETRY_SEC
// while the sync keeps failing. Between syncs the clock is advanced locally from the
// FreeRTOS tick counter. Meant for frequent/periodic callers such as a clock display.
bool ICACHE_FLASH_ATTR ntp_get_time_cached(struct tm **dt)
{
    static time_t syncedTime = 0;       // UTC time at the last successful sync, 0 = never synced
    static portTickType syncedTick = 0; // tick count at that sync
    static portTickType lastAttemptTick = 0;
    static portTickType waitTicks = 0; // how long to wait before the next attempt
    static bool everAttempted = false;
    const portTickType ticksPerSec = 1000 / portTICK_RATE_MS;
    portTickType now = xTaskGetTickCount();
    time_t result;

    if ((!everAttempted || (now - lastAttemptTick) >= waitTicks) &&
        wifi_station_get_connect_status() == STATION_GOT_IP) {
        time_t fresh;

        everAttempted = true;
        lastAttemptTick = now;
        if (ntp_fetch(&fresh)) {
            syncedTime = fresh;
            syncedTick = xTaskGetTickCount(); // the fetch can take seconds
            waitTicks = NTP_RESYNC_SEC * ticksPerSec;
        } else {
            waitTicks = NTP_RETRY_SEC * ticksPerSec;
        }
        now = xTaskGetTickCount();
    }

    if (syncedTime == 0)
        return false; // never had a successful sync yet

    result = syncedTime + (time_t)((now - syncedTick) / ticksPerSec);
    *dt = gmtime(&result);
    return true;
}

// print date time in ISO-8601 local time format
void ICACHE_FLASH_ATTR ntp_print_time()
{
    struct tm *dt;
    int8_t tz;
    char msg[30];

    if (ntp_get_time(&dt)) {
        tz = applyTZ(dt);
        strftime(msg, sizeof(msg), "%Y-%m-%dT%H:%M:%S", dt);
        // ISO-8601 local time   https://www.w3.org/TR/NOTE-datetime
        // YYYY-MM-DDThh:mm:ssTZD (eg 1997-07-16T19:20:30+01:00)
        if (tz >= 0)
            kprintf(PSTR("##SYS.DATE#: %s+%02d:00\n"), msg, tz);
        else
            kprintf(PSTR("##SYS.DATE#: %s%03d:00\n"), msg, tz);
    }
}
