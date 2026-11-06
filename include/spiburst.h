/*
 * Fast transfers on the SPI bus (see spilib.c).
 *
 * All of them expect the caller to hold the bus (spi_take_semaphore) and to handle the chip
 * selects, exactly like spi_put_char()/spi_get_char().
 */
#ifndef __SPIBURST_H__
#define __SPIBURST_H__

#include <stdint.h>

#define SPI_BURST_MAX 64	/* size of the SPI hardware data buffer in bytes */

/* 1 = transfers of up to 64 bytes through the SPI data buffer are allowed (used for the SPIRAM).
   0 = everything is done byte by byte, the way it was before. */
#ifndef SPI_USE_BURST
#define SPI_USE_BURST 1
#endif

/* Runtime switch. The start-up self-test (spiramInit) compares the burst transfers with the byte by
   byte ones, using the SPIRAM as the other end, and clears this flag if they do not agree on this
   hardware. The burst functions then fall back to byte by byte transfers by themselves. */
extern uint8_t spi_burst_enabled;

/* How the music data goes to the VS1053 (spi_sdi_write):
     0 = spi_put_char() for every byte (the original code)
     1 = byte by byte, but the register setup is done once per chunk: the same pattern on the wire as
         the original (a short pause between the bytes), at about a third of the CPU time
     2 = one burst per chunk, continuous clock */
#ifndef SDI_MODE
#define SDI_MODE 1
#endif

/* data out in bursts of up to SPI_BURST_MAX bytes, any length */
void spi_write_buf(const uint8_t *data, uint32_t len);

/* 8 bit command + 24 bit address + data out / data in, 1..SPI_BURST_MAX bytes of data */
void spi_cmd_write(uint8_t cmd, uint32_t addr, const uint8_t *data, uint32_t len);
void spi_cmd_read(uint8_t cmd, uint32_t addr, uint8_t *data, uint32_t len);

/* data out byte by byte with the register setup done once */
void spi_write_bytes(const uint8_t *data, uint32_t len);

/* music data to the VS1053, as selected by SDI_MODE */
void spi_sdi_write(const uint8_t *data, uint32_t len);

#endif
