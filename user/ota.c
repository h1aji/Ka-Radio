/*
 * ESPRSSIF MIT License
 *
 * Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "esp_common.h"
#include "esp_system.h"
#include "upgrade.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "interface.h"
#include "ota.h"

/* ------------------------------------------------------------------------- */
/*  Configuration                                                            */
/* ------------------------------------------------------------------------- */

#define OTA_HOST          "karadio.karawin.fr"   /* firmware server               */
#define OTA_PORT          80
#define OTA_URL_SIZE      512                    /* size of the request buffer    */
#define OTA_CHECK_TIMES   360000                 /* upgrade check retries         */

#define OTA_STR_(x)       #x
#define OTA_STR(x)        OTA_STR_(x)

/*
 * HTTP request template, kept in flash.
 *   %d = image slot to download (1 or 2)
 *   %s = release name ("new" or "prv")
 */
const char strupd[] ICACHE_RODATA_ATTR STORE_ATTR = {
    "GET /user%d.4096.%s.4.bin HTTP/1.0\r\n"
    "Host: " OTA_HOST ":" OTA_STR(OTA_PORT) "\r\n"
    "Connection: keep-alive\r\n"
    "Cache-Control: no-cache\r\n"
    "User-Agent: Karadio 1.5 \r\n"
    "Accept: */*\r\n"
    "Accept-Encoding: gzip,deflate,sdch\r\n"
    "\r\n"
};

extern void wsUpgrade(const char *str, int count, int total);

/* ------------------------------------------------------------------------- */
/*  Upgrade result callback                                                  */
/* ------------------------------------------------------------------------- */

/******************************************************************************
 * FunctionName : user_esp_upgrade_rsp
 * Description  : upgrade check call back function
 * Parameters   : arg - the upgrade_server_info passed to system_upgrade_start()
 * Returns      : none
 *****************************************************************************/
void user_esp_upgrade_rsp(void *arg)
{
    struct upgrade_server_info *server = (struct upgrade_server_info *)arg;

    if (server->upgrade_flag == true) {
        wsUpgrade("FW OK Refresh the page", 0, 100);
        system_upgrade_reboot();
    } else {
        wsUpgrade("FW upgrade failed.", 0, 100);
    }

    /* the callback owns the server info and its url buffer */
    free(server->url);
    free(server);
}

/* ------------------------------------------------------------------------- */
/*  Start an upgrade                                                         */
/* ------------------------------------------------------------------------- */

/******************************************************************************
 * FunctionName : update_firmware
 * Description  : configure the remote server and start the download
 * Parameters   : fname - release to fetch: "new" or "prv"
 * Returns      : none
 *****************************************************************************/
ICACHE_FLASH_ATTR void update_firmware(char *fname)
{
    struct upgrade_server_info *server;
    struct hostent *host;
    char *fmt;
    size_t fmt_len;
    uint8_t current_id;

    kprintf(PSTR("update  firmware \r%c"), 0x0d);

    current_id = system_upgrade_userbin_check();
    clientDisconnect(PSTR("Update"));

    kprintf(PSTR("flash size  %d\n"), (int)system_get_flash_size_map());

    /* download into the slot that is NOT running now */
    current_id = (current_id == 0) ? 2 : 1;

    /* resolve the server first, so nothing is allocated if the lookup fails */
    host = gethostbyname(OTA_HOST);
    if (host == NULL || host->h_addr_list[0] == NULL) {
        kprintf(PSTR("upgrade error: DNS lookup failed%c"), 0x0d);
        return;
    }

    server = (struct upgrade_server_info *)malloc(sizeof(struct upgrade_server_info));
    if (server == NULL) {
        kprintf(PSTR("upgrade error: out of memory%c"), 0x0d);
        return;
    }
    memset(server, 0, sizeof(struct upgrade_server_info));

    server->url = (uint8_t *)malloc(OTA_URL_SIZE);

    /* copy the request template from flash to RAM */
    fmt_len = strlen(strupd);
    fmt = (char *)malloc(fmt_len + 1);

    if (server->url == NULL || fmt == NULL) {
        kprintf(PSTR("upgrade error: out of memory%c"), 0x0d);
        free(fmt);
        free(server->url);
        free(server);
        return;
    }

    flashRead(fmt, (int)strupd, fmt_len);
    fmt[fmt_len] = 0;
    snprintf((char *)server->url, OTA_URL_SIZE, fmt, current_id, fname);
    free(fmt);

    server->check_cb      = user_esp_upgrade_rsp;   /* upgrade check call back */
    server->check_times   = OTA_CHECK_TIMES;
    server->upgrade_flag  = true;

    server->sockaddrin.sin_family = AF_INET;
    server->sockaddrin.sin_port   = htons(OTA_PORT);
    memcpy(&server->sockaddrin.sin_addr, host->h_addr_list[0], sizeof(struct in_addr));

    if (system_upgrade_start(server) == false) {
        kprintf(PSTR("upgrade error%c"), 0x0d);
        /* the callback will never run, so release the memory here */
        free(server->url);
        free(server);
    }
}
