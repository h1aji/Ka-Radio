/*
 * Stream buffer with optional external SPIRAM (23LC1024)
 *
 */

#include "esp_common.h"
#include "esp8266/pin_mux_register.h"
#include "esp8266/eagle_soc.h"
#include "gpio.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buffer.h"
#include "interface.h"
#include "spilib.h"
#include "spiburst.h"

/* Direct register access: avoids the SDK's gpio_output_conf(), which is not linked in this project */
#define SPIRAM_CS_LOW()  GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, (1 << SPIRAM_CS))
#define SPIRAM_CS_HIGH() GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, (1 << SPIRAM_CS))

bool spiram; // true when an external SPIRAM chip was detected at boot
uint32_t BUFFER_SIZE;
uint8_t *buffer;
/* One stream task produces data; one decoder task consumes it. Payload transfers
 * stay outside critical sections; only complete transfers publish a new cursor. */
volatile uint32_t wptr = 0;
volatile uint32_t rptr = 0;
volatile uint8_t bempty = 1;

/* =====================================================================
    SPIRAM access
   ===================================================================== */

/*
 * Measure the real SPIRAM capacity. Chips ignore the address bits they do not
 * implement, so writing at address n aliases back onto address 0 once n reaches the
 * chip size. Returns 0 when nothing answers on the bus.
 */
static uint32_t spiramProbeSize()
{
    uint8_t pa[8] = {0xA5, 0x5A, 0xC3, 0x3C, 0x96, 0x69, 0x0F, 0xF0};
    uint8_t pb[8] = {0x5A, 0xA5, 0x3C, 0xC3, 0x69, 0x96, 0xF0, 0x0F}; // pa inverted
    uint8_t rd[8];
    uint32_t n;

    spiramWrite(8, 0, pa);
    spiramRead(8, 0, rd);
    if (memcmp(rd, pa, 8) != 0)
        return 0;
    for (n = 256; n < (1UL << 24); n <<= 1) {
        spiramWrite(8, n, pb);
        spiramRead(8, 0, rd);
        if (memcmp(rd, pb, 8) == 0) // address 0 changed: n wrapped onto it
            return n;
        spiramRead(8, n, rd);
        if (memcmp(rd, pb, 8) != 0) // address n does not hold data: stop here
            return n;
    }
    return n;
}

void spiramInit()
{
    char test[17] = {"FFFFFFFF"};
    char testram[17] = {"01234567"};

    /* ===== Configure GPIO ===== */
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15); // route GPIO15 to GPIO first
    SPIRAM_CS_HIGH(); // CS high (deselected) before enabling the output
    GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, (1 << SPIRAM_CS)); // enable output driver

    spiram = false;
    spi_speed_2mhz();
    spiramWrite(strlen(test), 0, testram);
    spiramRead(strlen(test), 0, test);
    if (memcmp(test, testram, 16) == 0) {
        spiram = true;
        printf(PSTR("\nSPIRAM detected%c"), 0x0d);
        printf(PSTR("\nSPIRAM installed size: %d bytes%c"), spiramProbeSize(), 0x0d);
    } else {
        printf(PSTR("\nSPIRAM not detected%c"), 0x0d);
    }
}

/* Up to SPI_BURST_MAX bytes per transaction (command + address + data in one go). The chip select goes
   high between the bursts, every burst starts with its own command and address. */
uint32_t spiramRead(uint32_t size, uint32_t address, uint8_t *dst) {
    uint32_t done = 0, n;
    spi_take_semaphore();
    spi_speed_16mhz(); // 16MHz

    while (done < size) {
		n = size - done;
		if (n > SPI_BURST_MAX) n = SPI_BURST_MAX;
		SPIRAM_CS_LOW();
		spi_cmd_read(0x03, address + done, dst + done, n);
		SPIRAM_CS_HIGH();
		done += n;
	}

    spi_give_semaphore();
    return done;
}

uint32_t spiramWrite(uint32_t size, uint32_t address, uint8_t *data)
{
    uint32_t done = 0, n;
    spi_take_semaphore();
    spi_speed_16mhz();

	while (done < size) {
		n = size - done;
		if (n > SPI_BURST_MAX) n = SPI_BURST_MAX;
		SPIRAM_CS_LOW();
		spi_cmd_write(0x02, address + done, data + done, n);
		SPIRAM_CS_HIGH();
		done += n;
	}

    spi_give_semaphore();
    return done;
}

/* =====================================================================
    Ring buffer (internal RAM, or SPIRAM when detected)
   ===================================================================== */

void bufferInit()
{
    if (spiram) {
        BUFFER_SIZE = SPIRAM_SIZE;
    } else {
        BUFFER_SIZE = 20000;
        buffer = malloc(BUFFER_SIZE);
        if (buffer == NULL) {
            /* Report an unavailable buffer instead of writing through NULL. */
            BUFFER_SIZE = 0;
            printf(PSTR("Stream buffer allocation failed\n"));
        }
    }
    bufferReset();
}

uint32_t getBufferFree()
{
    uint32_t write;
    uint32_t read;
    uint8_t empty;

    /* A consistent snapshot distinguishes a full ring from an empty one when
     * both cursors coincide. Do not disable scheduling during SPI transfers. */
    taskENTER_CRITICAL();
    write = wptr;
    read = rptr;
    empty = bempty;
    taskEXIT_CRITICAL();

    if (write > read)
        return BUFFER_SIZE - write + read;
    if (write < read)
        return read - write;
    if (empty)
        return BUFFER_SIZE;
    return 0;
}

bool getBufferEmpty()
{
    return bempty;
}

ICACHE_FLASH_ATTR uint32_t getBufferFilled()
{
    return BUFFER_SIZE - getBufferFree();
}

ICACHE_FLASH_ATTR uint32_t bufferWrite(uint8_t *data, uint32_t size)
{
    uint32_t available = getBufferFree();
    uint32_t position = wptr;
    uint32_t first;
    uint32_t next;

    if (data == NULL || size == 0 || available == 0)
        return 0;
    if (size > available)
        size = available;

    /* At most two contiguous transfers replace a byte-at-a-time loop. Split
     * SPIRAM transfers explicitly too; do not rely on the chip's address wrap. */
    first = BUFFER_SIZE - position;
    if (first > size)
        first = size;
    if (spiram) {
        spiramWrite(first, position, data);
        if (size > first)
            spiramWrite(size - first, 0, data + first);
    } else {
        memcpy(buffer + position, data, first);
        if (size > first)
            memcpy(buffer, data + first, size - first);
    }

    next = position + size;
    if (next >= BUFFER_SIZE)
        next -= BUFFER_SIZE;
    taskENTER_CRITICAL();
    wptr = next;
    bempty = 0;
    taskEXIT_CRITICAL();
    return size;
}

uint32_t bufferRead(uint8_t *data, uint32_t size)
{
    uint32_t available = getBufferFilled();
    uint32_t position = rptr;
    uint32_t first;
    uint32_t next;

    if (data == NULL || size == 0 || available == 0)
        return 0;
    if (size > available)
        size = available;

    first = BUFFER_SIZE - position;
    if (first > size)
        first = size;
    if (spiram) {
        spiramRead(first, position, data);
        if (size > first)
            spiramRead(size - first, 0, data + first);
    } else {
        memcpy(data, buffer + position, first);
        if (size > first)
            memcpy(data + first, buffer, size - first);
    }

    next = position + size;
    if (next >= BUFFER_SIZE)
        next -= BUFFER_SIZE;
    taskENTER_CRITICAL();
    rptr = next;
    bempty = (next == wptr);
    taskEXIT_CRITICAL();
    return size;
}

void bufferReset()
{
    /* Caller must stop/drain playback first; reset does not cancel a transfer. */
    taskENTER_CRITICAL();
    wptr = 0;
    rptr = 0;
    bempty = 1;
    taskEXIT_CRITICAL();
}
