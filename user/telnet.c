/* (c)jp cocatrix May 2016
 *
 * Copyright 2016 karawin (http://www.karawin.fr)
 *
 * Quick and simple telnet implementation for Ka-Radio
 * minimal setup for log and commands
 *
 */

#include "esp_common.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "lwip/sockets.h"

#include "telnet.h"
#include "flash.h"
#include "interface.h"

// const char strtMALLOC1[] = {"Telnet %s malloc fails\n"};
const char strtSOCKET[] ICACHE_RODATA_ATTR STORE_ATTR = {"Telnet Socket fails %s errno: %d\n"};
const char strtWELCOME[] ICACHE_RODATA_ATTR STORE_ATTR = {"Karadio telnet\n> "};

int telnetclients[NBCLIENTT];
// set of socket descriptors
fd_set readfdst;
// reception buffer
char brec[256];
char iac[3];
bool inIac = false; // if in negociation
char *obrec;
uint16_t irec;
uint8_t iiac;
xSemaphoreHandle sTELNET = NULL;

static uint8_t telnet_take_semaphore()
{
    if (sTELNET)
        if (xSemaphoreTake(sTELNET, portMAX_DELAY))
            return 1;
    return 0;
}

static void telnet_give_semaphore()
{
    if (sTELNET)
        xSemaphoreGive(sTELNET);
}

// init some data
void telnetinit(void)
{
    int i;
    vSemaphoreCreateBinary(sTELNET);

    for (i = 0; i < NBCLIENTT; i++) {
        telnetclients[i] = -1;
    }
    memset(brec, 0, sizeof(brec));
    irec = 0;
    iiac = 0;
    inIac = false;
    obrec = malloc(2);
    if (obrec != NULL)
        obrec[0] = '\0';
}

// a socket with a websocket request. Note it and answer to the client
bool telnetnewclient(int socket)
{
    int available = -1;
    int i;
    if (socket < 0)
        return false;
    /* Scan once, but check every occupied slot before using a free one. */
    for (i = 0; i < NBCLIENTT; i++) {
        if (telnetclients[i] == socket)
            return true;
        if (telnetclients[i] == -1 && available == -1)
            available = i;
    }
    if (available == -1)
        return false;
    telnetclients[available] = socket;
    return true;
}

// remove the client in the list of clients
void telnetremoveclient(int socket)
{
    int i;
    //	printf("ws removeclient:%d\n",socket);
    for (i = 0; i < NBCLIENTT; i++)
        if (telnetclients[i] == socket) {
            telnetclients[i] = -1;
            //			printf("ws removeclient:%d removed\n",socket);
            close(socket);
            return;
        }
}

// is socket a telnet one?
bool istelnet(int socket)
{
    int i;
    for (i = 0; i < NBCLIENTT; i++)
        if ((telnetclients[i] != -1) && (telnetclients[i] == socket))
            return true;
    return false;
}

ICACHE_FLASH_ATTR bool telnetAccept(int tsocket)
{
    int32_t recbytes = 0;

    if ((!istelnet(tsocket)) && (telnetnewclient(tsocket))) {
        char *fmt = malloc(strlen(strtWELCOME) + 16);
        if (fmt != NULL) {
            flashRead(fmt, (uint32_t)strtWELCOME, strlen(strtWELCOME));
            fmt[strlen(strtWELCOME)] = 0;
            //			printf("telnet write accept\n");
            write(tsocket, fmt, strlen(strtWELCOME)); // reply to accept
            free(fmt);
            return true;
        } else
            telnetremoveclient(tsocket);
    } else
        close(tsocket);
    return false;
}

// broadcast a txt data to all clients
void telnetWrite(uint32_t lenb, const char *fmt, ...)
{
    char *buf;
    char *flashFormat = NULL;
    const char *format = fmt;
    int length;
    int i;
    va_list ap;

    if (fmt == NULL || lenb == 0 || lenb == UINT32_MAX)
        return;
    buf = malloc((size_t)lenb + 1);
    if (buf == NULL)
        return;

    /* Flash-backed formats must be copied to RAM for libc formatting. */
    if ((uintptr_t)fmt > 0x40100000UL) {
        size_t formatLength = strlen(fmt);
        flashFormat = malloc(formatLength + 16);
        if (flashFormat == NULL) {
            free(buf);
            return;
        }
        flashRead(flashFormat, (uint32_t)(uintptr_t)fmt, formatLength);
        flashFormat[formatLength] = '\0';
        format = flashFormat;
    }

    /* Bound the write itself; checking vsprintf's length afterwards was too late
     * to prevent a heap overwrite. Keep the buffer, rather than shrinking it. */
    va_start(ap, fmt);
    length = vsnprintf(buf, (size_t)lenb + 1, format, ap);
    va_end(ap);
    free(flashFormat);
    if (length < 0 || (uint32_t)length > lenb) {
        printf(PSTR("Telnet message exceeds its buffer\n"));
        free(buf);
        return;
    }

    telnet_take_semaphore();
    for (i = 0; i < NBCLIENTT; i++)
        if (telnetclients[i] >= 0)
            write(telnetclients[i], buf, length);
    telnet_give_semaphore();
    free(buf);
}

ICACHE_FLASH_ATTR int telnetNego(int tsocket)
{
    const uint8_t NONEG[2] = {0xFF, 0xFC}; // WON't

    if (iiac == 2) {
        // refuse all
        if ((uint8_t)iac[0] == 251) {
            write(tsocket, NONEG, 2);
            write(tsocket, iac + 1, 1);
        }
    } else {
        if ((uint8_t)iac[0] == 246)
            write(tsocket, "\n>", 2); // are you there
    }
    return 0;
}

ICACHE_FLASH_ATTR int telnetCommand(int tsocket)
{
    uint16_t length = irec;
    char *history;
    if (irec == 0)
        return 0;
    // printf(PSTR("%sHEAPd0: %d #\n"),"##SYS.",xPortGetFreeHeapSize( ));
    brec[irec] = 0x0;
    write(tsocket, "\n> ", 1);
    // printf("%s\n",brec);
    /* Preserve the previous command if resizing fails; command execution does
     * not depend on history being available. */
    history = realloc(obrec, irec + 1);
    if (history != NULL) {
        obrec = history;
        memcpy(obrec, brec, irec + 1);
    }
    checkCommand(irec, brec);
    write(tsocket, "> ", 2);
    irec = 0;
    return length;
}

ICACHE_FLASH_ATTR int telnetRead(int tsocket)
{
    char *buf;
    int32_t recbytes = 0;
    int i;
    buf = (char *)malloc(MAXDATAT);
    if (buf == NULL) {
        vTaskDelay(100); // wait a while and retry
        buf = (char *)malloc(MAXDATAT);
    }
    if (buf != NULL) {
        recbytes = read(tsocket, buf, MAXDATAT);

        if (recbytes <= 0) {
            if ((errno != EAGAIN) && (errno != ENOTCONN) && (errno != 0)) {
                if (errno != ECONNRESET) {
                    printf(strtSOCKET, "read", errno);
                }
            }
            free(buf);
            return 0; // free the socket
        }

        /* The read buffer is already large enough. No resize or trailing NUL
         * is needed because every byte is consumed using recbytes. */
        {
            for (i = 0; i < recbytes; i++) {
                // printf("%x ",buf[i]);
                if (!inIac)
                    switch ((uint8_t)buf[i]) {
                        case '\r':
                        case '\n':
                            telnetCommand(tsocket);
                            break;
                        case 0x08: // backspace
                        case 0x7F: // delete
                            if (irec > 0)
                                --irec;
                            break;
                        case 0x1B:
                            if (i + 2 < recbytes) {
                                if ((buf[i + 1] == '[') && (buf[i + 2] == 'A')) // arrow up
                                {
                                    if (obrec != NULL) {
                                        strcpy(brec, obrec);
                                        write(tsocket, "\r", 1);
                                        write(tsocket, brec, strlen(brec));
                                        irec = strlen(brec);
                                        vTaskDelay(2);
                                        telnetCommand(tsocket);
                                    }
                                }
                                i = recbytes; // exit for
                            }
                            break;
                        case 0xff: // iac
                            inIac = true;
                            iiac = 0;
                            break;
                        default:
                            /* Reserve one byte for telnetCommand's terminator.
                             * Ignore overflow instead of wrapping to byte zero. */
                            if (irec < sizeof(brec) - 1)
                                brec[irec++] = buf[i];
                    }
                else // in iac
                {
                    iac[iiac++] = buf[i];
                    if (iiac == 2) {
                        telnetNego(tsocket);
                        inIac = false;
                        iiac = 0;
                    }
                }
            }
            free(buf);
        }
    }
    return recbytes;
}
