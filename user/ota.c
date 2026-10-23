/*
 * Over-the-air firmware update.
 *
 * Sections:
 *   - flash writer:  erase and program the image slot that is not running
 *   - download:      fetch the image from the firmware server into that slot
 *   - entry point:   update_firmware()
 *
 * The functions of the flash writer and of the download run with the flash being
 * erased or written: they must stay in IRAM, do not add ICACHE_FLASH_ATTR to them.
 */

#include "esp_common.h"
#include "esp8266/eagle_soc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "spi_flash.h"
#include "upgrade.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/mem.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"

#include "ota.h"
#include "flash.h"
#include "interface.h"
#include "webclient.h"
#include "websocket.h"

/* -------------------------------------------------------------------------
    Configuration
   ------------------------------------------------------------------------- */

#define OTA_HOST "karadio.karawin.fr" // firmware server
#define OTA_PORT 80
#define OTA_URL_SIZE 512       // size of the request buffer
#define OTA_CHECK_TIMES 360000 // upgrade check retries

#define UPGRADE_DATA_SEG_LEN 1460 // the size cannot be bigger
#define UPGRADE_RETRY_TIMES 10

#define OTA_STR_(x) #x
#define OTA_STR(x) OTA_STR_(x)

/*
 * HTTP request template, kept in flash.
 *   %d = image slot to download (1 or 2)
 *   %s = release name ("new" or "prv")
 */
static const char strupd[] ICACHE_RODATA_ATTR
STORE_ATTR = {"GET /user%d.4096.%s.4.bin HTTP/1.0\r\n"
              "Host: " OTA_HOST ":" OTA_STR(OTA_PORT) "\r\n"
                                                      "Connection: keep-alive\r\n"
                                                      "Cache-Control: no-cache\r\n"
                                                      "User-Agent: Karadio 2.1 \r\n"
                                                      "Accept: */*\r\n"
                                                      "Accept-Encoding: gzip,deflate,sdch\r\n"
                                                      "\r\n"};

/* image CRC check, defined in crc32.c */
extern int upgrade_crc_check(uint16_t fw_bin_sec, unsigned int sumlength);

/* -------------------------------------------------------------------------
    Flash writer
   ------------------------------------------------------------------------- */

struct upgrade_param {
    uint32_t fw_bin_addr;      // next address to program
    uint16_t fw_bin_sec;       // first sector of the target slot
    uint16_t fw_bin_sec_num;   // number of sectors of the target slot
    uint16_t fw_bin_sec_earse; // next sector to erase
    uint8_t extra;             // bytes of save[] waiting for a full word
    uint8_t save[4];
    uint8_t *buffer;
};

static struct upgrade_param *upgrade;

// true when the sector is outside the slot we are allowed to erase
static bool out_of_range(uint16_t erase_sec)
{
    uint8_t spi_size_map = system_get_flash_size_map();
    uint16_t sec_num = 0;
    uint16_t start_sec = 0;

    if (spi_size_map == FLASH_SIZE_8M_MAP_512_512 || spi_size_map == FLASH_SIZE_16M_MAP_512_512 ||
        spi_size_map == FLASH_SIZE_32M_MAP_512_512) {
        start_sec = (system_upgrade_userbin_check() == USER_BIN2) ? 1 : 129;
        sec_num = 123;
    } else if (spi_size_map == FLASH_SIZE_16M_MAP_1024_1024 ||
               spi_size_map == FLASH_SIZE_32M_MAP_1024_1024) {
        start_sec = (system_upgrade_userbin_check() == USER_BIN2) ? 1 : 257;
        sec_num = 251;
    } else {
        start_sec = (system_upgrade_userbin_check() == USER_BIN2) ? 1 : 65;
        sec_num = 59;
    }

    return !((erase_sec >= start_sec) && (erase_sec <= (start_sec + sec_num)));
}

/******************************************************************************
 * FunctionName : system_upgrade_init
 * Description  : select the slot that is not running and reset the write state
 * Parameters   : none
 * Returns      : none
 *****************************************************************************/
void system_upgrade_init(void)
{
    uint32_t user_bin2_start, user_bin1_start;
    uint8_t spi_size_map = system_get_flash_size_map();

    if (upgrade == NULL) {
        upgrade = (struct upgrade_param *)zalloc(sizeof(struct upgrade_param));
    }

    user_bin1_start = 1;
    if (spi_size_map == FLASH_SIZE_8M_MAP_512_512 || spi_size_map == FLASH_SIZE_16M_MAP_512_512 ||
        spi_size_map == FLASH_SIZE_32M_MAP_512_512) {
        user_bin2_start = 129;
        upgrade->fw_bin_sec_num = 123;
    } else if (spi_size_map == FLASH_SIZE_16M_MAP_1024_1024 ||
               spi_size_map == FLASH_SIZE_32M_MAP_1024_1024) {
        user_bin2_start = 257;
        upgrade->fw_bin_sec_num = 251;
    } else {
        user_bin2_start = 65;
        upgrade->fw_bin_sec_num = 59;
    }

    upgrade->fw_bin_sec =
        (system_upgrade_userbin_check() == USER_BIN1) ? user_bin2_start : user_bin1_start;
    upgrade->fw_bin_addr = upgrade->fw_bin_sec * SPI_FLASH_SEC_SIZE;
    upgrade->fw_bin_sec_earse = upgrade->fw_bin_sec;
}

/******************************************************************************
 * FunctionName : system_upgrade_deinit
 * Description  : release the write state
 * Parameters   : none
 * Returns      : none
 *****************************************************************************/
void system_upgrade_deinit(void)
{
    if (upgrade != NULL) {
        free(upgrade);
        upgrade = NULL;
    }
}

/******************************************************************************
 * FunctionName : system_upgrade
 * Description  : a chunk longer than a flash sector is the total image length:
 *                erase all the sectors of the image. Any other chunk is
 *                programmed, whole 32 bit words at a time.
 * Parameters   : data - image data, or the first bytes after the HTTP header
 *                len  - length of data, or the total length of the image
 * Returns      : true on success
 *****************************************************************************/
bool system_upgrade(uint8_t *data, uint32_t len)
{
    bool ret = false;
    uint16_t secnm = 0;

    if (data == NULL || len == 0) {
        return true;
    }

    /* got the sumlength, erase all upgrade sectors */
    if (len > SPI_FLASH_SEC_SIZE) {
        upgrade->fw_bin_sec_earse = upgrade->fw_bin_sec;

        secnm = ((upgrade->fw_bin_addr + len) >> 12) + (len & 0xfff ? 1 : 0);
        while (upgrade->fw_bin_sec_earse != secnm) {
            if (out_of_range(upgrade->fw_bin_sec_earse)) {
                printf("fw_bin_sec_earse:%d, Out of range\n", upgrade->fw_bin_sec_earse);
                break;
            }
            taskENTER_CRITICAL();
            WRITE_PERI_REG(0x60000914, 0x73);
            spi_flash_erase_sector(upgrade->fw_bin_sec_earse);
            taskEXIT_CRITICAL();
            upgrade->fw_bin_sec_earse++;
            vTaskDelay(10 / portTICK_RATE_MS);
        }
        printf(PSTR("flash erase over\n"));
        return true;
    }

    upgrade->buffer = (uint8_t *)zalloc(len + upgrade->extra);

    /* the bytes left from the previous chunk go first */
    memcpy(upgrade->buffer, upgrade->save, upgrade->extra);
    memcpy(upgrade->buffer + upgrade->extra, data, len);

    len += upgrade->extra;
    upgrade->extra = len & 0x03;
    len -= upgrade->extra;
    memcpy(upgrade->save, upgrade->buffer + len, upgrade->extra);

    do {
        if (upgrade->fw_bin_addr + len >=
            (upgrade->fw_bin_sec + upgrade->fw_bin_sec_num) * SPI_FLASH_SEC_SIZE) {
            break;
        }

        if (spi_flash_write(upgrade->fw_bin_addr, (uint32_t *)upgrade->buffer, len) !=
            SPI_FLASH_RESULT_OK) {
            break;
        }

        ret = true;
        upgrade->fw_bin_addr += len;
    } while (0);

    free(upgrade->buffer);
    upgrade->buffer = NULL;
    return ret;
}

/* -------------------------------------------------------------------------
    Download
   ------------------------------------------------------------------------- */

static uint32_t totallength = 0;
static uint32_t sumlength = 0;
static BOOL flash_erased = 0;
static BOOL giveup = 0;

static char *precv_buf = NULL;
static os_timer_t upgrade_timer;

/******************************************************************************
 * FunctionName : wsUpgrade
 * Description  : send the OTA feedback to the websockets
 * Parameters   : str   - message to send, or "" to send a progress
 *                count - bytes written
 *                total - bytes of the image
 * Returns      : none
 *****************************************************************************/
static void wsUpgrade(const char *str, int count, int total)
{
    char answer[80];
    memset(answer, 0, 80);
    if (strlen(str) != 0) {
        kasprintf(answer, PSTR("{\"upgrade\":\"%s\"}"), str);
        kprintf(answer);
        kprintf(PSTR("\n"));
    } else {
        uint32_t value = count * 100 / total;
        if (value == 0) {
            strcpy(answer, "{\"upgrade\":\"Started.\"}");
            kprintf(answer);
            kprintf(PSTR("\n"));
        } else {
            sprintf(answer, "{\"upgrade\":\"%d / 100\"}", value);
            kprintf(PSTR("Written  %d  of  %d\n"), count, total);
        }
    }
    websocketbroadcast(answer, strlen(answer));
    vTaskDelay(5);
}

static void upgrade_deinit(void)
{
    if (system_upgrade_flag_check() != UPGRADE_FLAG_START) {
        system_upgrade_deinit();
    }
}

/******************************************************************************
 * FunctionName : upgrade_data_load
 * Description  : parse the data from the server, send the firmware data to the
 *                flash writer
 * Parameters   : pusrdata - data from the server
 *                length   - length of pusrdata
 * Returns      : false when the data cannot be used
 *
 * first data from server:
 * HTTP/1.1 200 OK
 * Server: nginx/1.6.2
 * Date: Tue, 14 Jul 2015 09:15:51 GMT
 * Content-Type: application/octet-stream
 * Content-Length: 282448
 * Connection: keep-alive
 * Content-Disposition: attachment;filename=user2.bin
 * Vary: Cookie
 * X-RateLimit-Remaining: 3599
 * X-RateLimit-Limit: 3600
 * X-RateLimit-Reset: 1436866251
 *****************************************************************************/
static BOOL upgrade_data_load(char *pusrdata, unsigned short length)
{
    char *ptr = NULL;
    char *ptmp2 = NULL;
    char lengthbuffer[32];

    if (totallength == 0 && (ptr = (char *)strstr(pusrdata, "\r\n\r\n")) != NULL &&
        (ptr = (char *)strstr(pusrdata, "Content-Length")) != NULL) {

        ptr = (char *)strstr(pusrdata, "Content-Length: ");
        if (ptr != NULL) {
            ptr += 16;
            ptmp2 = (char *)strstr(ptr, "\r\n");

            if (ptmp2 != NULL) {
                memset(lengthbuffer, 0, sizeof(lengthbuffer));

                if ((ptmp2 - ptr) <= 32)
                    memcpy(lengthbuffer, ptr, ptmp2 - ptr);
                else
                    kprintf(PSTR("ERR1:arr_overflow,%u,%d\n"), __LINE__, (ptmp2 - ptr));

                sumlength = atoi(lengthbuffer);
                kprintf(PSTR("userbin sumlength:%d \n"), sumlength);

                ptr = (char *)strstr(pusrdata, "\r\n\r\n");
                length -= ptr - pusrdata;
                length -= 4;
                totallength += length;

                /* at the begining of the upgrade, we get the sumlength and erase
                 * all the target flash sectors, return false to close the
                 * connection, and start upgrade again. */
                if (FALSE == flash_erased) {
                    flash_erased = system_upgrade((uint8_t *)(ptr + 4), sumlength);
                    kprintf(PSTR("userbin sumlength:%d  flash:%d\n"), sumlength, flash_erased);
                    return flash_erased;
                } else {
                    system_upgrade((uint8_t *)(ptr + 4), length);
                }
            } else {
                kprintf(PSTR("ERROR:Get sumlength failed%c"), 0x0d);
                return false;
            }
        } else {
            kprintf(PSTR("ERROR:Get Content-Length failed%c"), 0x0d);
            return false;
        }

    } else {
        if (totallength != 0) {
            totallength += length;

            if (totallength > sumlength) {
                kprintf(PSTR("strip the 400 error mesg%c"), 0x0d);
                length = length - (totallength - sumlength);
            }

            system_upgrade((uint8_t *)pusrdata, length);

        } else {
            kprintf(PSTR("server response with something else,check it!%c"), 0x0d);
            return false;
        }
    }

    return true;
}

/******************************************************************************
 * FunctionName : upgrade_task
 * Description  : task to connect with the target server and get the firmware data
 * Parameters   : pvParameters - the upgrade_server_info: server address, port,
 *                request, and the callback that tells the application the
 *                result of the upgrade
 * Returns      : none
 *****************************************************************************/
static void upgrade_task(void *pvParameters)
{
    int recbytes;
    int sta_socket;
    int retry_count = 0;
    struct ip_info ipconfig;

    struct upgrade_server_info *server = pvParameters;

    flash_erased = FALSE;
    precv_buf = (char *)malloc(UPGRADE_DATA_SEG_LEN);
    if (NULL == precv_buf) {
        kprintf(PSTR("upgrade_task,memory exhausted, check it%c"), 0x0d);
    }

    while (retry_count++ < UPGRADE_RETRY_TIMES) {

        if (giveup) {
            kprintf(PSTR("giveup !\r%c"), 0x0d);
            break;
        }

        wifi_get_ip_info(STATION_IF, &ipconfig);

        /* check the ip address or net connection state */
        while (ipconfig.ip.addr == 0) {
            vTaskDelay(1000 / portTICK_RATE_MS);
            wifi_get_ip_info(STATION_IF, &ipconfig);
        }

        sta_socket = socket(PF_INET, SOCK_STREAM, 0);
        if (-1 == sta_socket) {
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            kprintf(PSTR("socket fail !\r%c"), 0x0d);
            continue;
        }

        if (0 != connect(sta_socket, (struct sockaddr *)(&server->sockaddrin),
                         sizeof(struct sockaddr))) {
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            wsUpgrade("Connect to server failed!", 0, 100);
            continue;
        }
        kprintf(PSTR("Connect ok!%c"), 0x0d);

        system_upgrade_init();
        system_upgrade_flag_set(UPGRADE_FLAG_START);

        if (write(sta_socket, server->url, strlen((char *)server->url)) < 0) {
            close(sta_socket);
            vTaskDelay(1000 / portTICK_RATE_MS);
            kprintf(PSTR("send fail%c"), 0x0d);
            continue;
        }
        kprintf(PSTR("Request send success%c"), 0x0d);

        while ((recbytes = read(sta_socket, precv_buf, UPGRADE_DATA_SEG_LEN)) > 0) {
            if (FALSE == flash_erased) {
                close(sta_socket);
                kprintf(PSTR("pre erase flash!%c"), 0x0d);
                if (false == upgrade_data_load(precv_buf, recbytes)) {
                    kprintf(PSTR("upgrade data error!%c"), 0x0d);
                    close(sta_socket);
                    flash_erased = FALSE;
                    vTaskDelay(1000 / portTICK_RATE_MS);
                }
                break;
            }

            if (false == upgrade_data_load(precv_buf, recbytes)) {
                kprintf(PSTR("upgrade data error!%c"), 0x0d);
                close(sta_socket);
                flash_erased = FALSE;
                vTaskDelay(1000 / portTICK_RATE_MS);
                break;
            }
            wsUpgrade("", totallength, sumlength);
            /* this two length data should be equal, if totallength is bigger,
             * maybe data wrong or server send extra info, drop it anyway */
            if (totallength >= sumlength) {
                wsUpgrade("upgrade load ok.", 0, 100);
                close(sta_socket);
                goto finish;
            }
        }

        if (recbytes <= 0) {
            close(sta_socket);
            flash_erased = FALSE;
            vTaskDelay(1000 / portTICK_RATE_MS);
            wsUpgrade("read data fail!", 0, 100);
        }

        totallength = 0;
        sumlength = 0;
    }

finish:

    os_timer_disarm(&upgrade_timer);

    if (upgrade_crc_check(upgrade ? upgrade->fw_bin_sec : 0, sumlength) != 0) {
        kprintf(PSTR("upgrade crc check failed !%c"), 0x0d);
        server->upgrade_flag = false;
        system_upgrade_flag_set(UPGRADE_FLAG_IDLE);
    }

    if (NULL != precv_buf) {
        free(precv_buf);
    }

    totallength = 0;
    sumlength = 0;
    flash_erased = FALSE;

    if (retry_count == UPGRADE_RETRY_TIMES) {
        /* retry too many times, fail */
        server->upgrade_flag = false;
        system_upgrade_flag_set(UPGRADE_FLAG_IDLE);

    } else {
        if (server->upgrade_flag == true)
            system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
    }

    upgrade_deinit();

    kprintf(PSTR("\n Exit upgrade task.%c"), 0x0d);
    if (server->check_cb != NULL) {
        server->check_cb(server);
    }
    vTaskDelay(100 / portTICK_RATE_MS);
    vTaskDelete(NULL);
}

/******************************************************************************
 * FunctionName : upgrade_check
 * Description  : timer callback: the upgrade did not finish in time, give up
 * Parameters   : server - the upgrade_server_info of this upgrade
 * Returns      : none
 *****************************************************************************/
static void upgrade_check(struct upgrade_server_info *server)
{
    /* network not stable, upgrade data lost, this may be called */
    giveup = true;
    os_timer_disarm(&upgrade_timer);

    if (NULL != precv_buf) {
        free(precv_buf);
    }

    totallength = 0;
    sumlength = 0;
    flash_erased = FALSE;

    /* take too long to finish, fail */
    server->upgrade_flag = false;
    system_upgrade_flag_set(UPGRADE_FLAG_IDLE);

    upgrade_deinit();

    wsUpgrade("upgrade failed!", 0, 100);
    if (server->check_cb != NULL) {
        server->check_cb(server);
    }
}

/******************************************************************************
 * FunctionName : system_upgrade_start
 * Description  : start the task that connects to the server and gets the
 *                firmware data
 * Parameters   : server - server address, port, request and result callback
 * Returns      : true if the task was created
 *****************************************************************************/
BOOL system_upgrade_start(struct upgrade_server_info *server)
{
    portBASE_TYPE ret;

    giveup = false; /* a previous timeout must not stop this upgrade */
    wsUpgrade("", 0, 100);
    ret = xTaskCreate(upgrade_task, "upgrade_task", 360, server, 8, NULL);

    if (pdPASS == ret) {
        os_timer_disarm(&upgrade_timer);
        os_timer_setfn(&upgrade_timer, (os_timer_func_t *)upgrade_check, server);
        os_timer_arm(&upgrade_timer, server->check_times, 0);
    }

    return (pdPASS == ret);
}

/******************************************************************************
 * FunctionName : user_esp_upgrade_rsp
 * Description  : upgrade check call back function
 * Parameters   : arg - the upgrade_server_info passed to system_upgrade_start()
 * Returns      : none
 *****************************************************************************/
static void user_esp_upgrade_rsp(void *arg)
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

    server->check_cb = user_esp_upgrade_rsp; /* upgrade check call back */
    server->check_times = OTA_CHECK_TIMES;
    server->upgrade_flag = true;

    server->sockaddrin.sin_family = AF_INET;
    server->sockaddrin.sin_port = htons(OTA_PORT);
    memcpy(&server->sockaddrin.sin_addr, host->h_addr_list[0], sizeof(struct in_addr));

    if (system_upgrade_start(server) == false) {
        kprintf(PSTR("upgrade error%c"), 0x0d);
        /* the callback will never run, so release the memory here */
        free(server->url);
        free(server);
    }
}
