/*
 * Burst transfers on the SPI bus (see spilib.c): up to 64 bytes per transaction through the
 * SPI hardware data buffer, instead of one transaction per byte.
 *
 * All of them expect the caller to hold the bus (spi_take_semaphore) and to handle the chip
 * selects, exactly like spi_put_char()/spi_get_char().
 */
#ifndef __SPIBURST_H__
#define __SPIBURST_H__

#include <stdint.h>

#define SPI_BURST_MAX 64	/* size of the SPI hardware data buffer in bytes */

/* plain data out, any length (sent as bursts of up to SPI_BURST_MAX bytes) */
void spi_write_buf(const uint8_t *data, uint32_t len);

/* 8 bit command + 24 bit address + data out / data in, 1..SPI_BURST_MAX bytes of data */
void spi_cmd_write(uint8_t cmd, uint32_t addr, const uint8_t *data, uint32_t len);
void spi_cmd_read(uint8_t cmd, uint32_t addr, uint8_t *data, uint32_t len);

#endif
