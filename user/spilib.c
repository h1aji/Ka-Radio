/******************************************************************************
 * 
 * Copyright 2026 h1aji (https://github.com/h1aji/Ka-Radio)
 *
*******************************************************************************/


#include "esp_common.h"
#include "esp8266/eagle_soc.h"
#include "esp8266/spi_register.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "spilib.h"
#include "spiburst.h"

xSemaphoreHandle sSPI = NULL;

/* Bounded wait for the SPI engine's USR bit to clear. The transfer completes
 * in a few microseconds; the cap only prevents a hard lockup if the SPI
 * peripheral ever wedges, and is far too high to trip in normal operation. */
#define SPI_WAIT_READY() do { \
  uint32_t _g = 0; \
  while ((READ_PERI_REG(SPI_CMD(SPI_BUS)) & SPI_USR) && (++_g < 1000000)); \
} while (0)

/* Must run before any task touches the bus. Until then take/give are no-ops and
 * the VS1053, the SPIRAM and the flash helpers would interleave on the same HSPI. */
ICACHE_FLASH_ATTR void spi_init_semaphore() {
  if (!sSPI) sSPI = xSemaphoreCreateMutex();
}

uint8_t spi_take_semaphore() {
  if (sSPI)
    if (xSemaphoreTake(sSPI, portMAX_DELAY)) return 1;
  return 0;
}

void spi_give_semaphore() {
  if (sSPI) xSemaphoreGive(sSPI);
}

void spi_speed_16mhz() {
  // Set SPI clock to 16MHz
  WRITE_PERI_REG(SPI_CLOCK(SPI_BUS),
		((0&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
		((4&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
		((2&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
		((4&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S));
}

void spi_speed_10mhz() {
  // Set SPI clock to 10MHz
  WRITE_PERI_REG(SPI_CLOCK(SPI_BUS),
		((1&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
		((3&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
		((1&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
		((3&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S));
}

void spi_speed_2mhz() {
  // Set SPI clock to 2MHz
  WRITE_PERI_REG(SPI_CLOCK(SPI_BUS),
		((9&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
		((3&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
		((1&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
		((3&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S));
}

void spi_put_char(uint8_t data) {
  SPI_WAIT_READY();

  CLEAR_PERI_REG_MASK(SPI_USER(SPI_BUS),SPI_USR_MOSI|SPI_USR_MISO|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_DUMMY);

  WRITE_PERI_REG(SPI_USER1(SPI_BUS), SPI_USR_ADDR_BITLEN<<SPI_USR_ADDR_BITLEN_S |		//Number of bits in Address
					(7&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S |	  //Number of bits to Send
					SPI_USR_MISO_BITLEN<<SPI_USR_MISO_BITLEN_S |		    //Number of bits to Receive
					SPI_USR_DUMMY_CYCLELEN<<SPI_USR_DUMMY_CYCLELEN_S);	//Number of Dummy bits to insert

  SET_PERI_REG_MASK(SPI_USER(SPI_BUS),SPI_USR_MOSI); //enable MOSI function in SPI module

  if (READ_PERI_REG(SPI_USER(SPI_BUS))&SPI_WR_BYTE_ORDER) {
	  WRITE_PERI_REG(SPI_W0(SPI_BUS),(uint32_t)data<<24);
  } else {
	  WRITE_PERI_REG(SPI_W0(SPI_BUS),(uint32_t)data);
  }

  SET_PERI_REG_MASK(SPI_CMD(SPI_BUS),SPI_USR);
  SPI_WAIT_READY();
}

uint8_t spi_get_char() {
  SPI_WAIT_READY();

  CLEAR_PERI_REG_MASK(SPI_USER(SPI_BUS),SPI_USR_MOSI|SPI_USR_MISO|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_DUMMY);
  SET_PERI_REG_MASK(SPI_USER(SPI_BUS),SPI_USR_MISO);

  WRITE_PERI_REG(SPI_USER1(SPI_BUS),SPI_USR_ADDR_BITLEN<<SPI_USR_ADDR_BITLEN_S |		//Number of bits in Address
					SPI_USR_MOSI_BITLEN<<SPI_USR_MOSI_BITLEN_S |		    //Number of bits to Send
					(7&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S |	  //Number of bits to Receive
					SPI_USR_DUMMY_CYCLELEN<<SPI_USR_DUMMY_CYCLELEN_S);	//Number of Dummy bits to insert

  SET_PERI_REG_MASK(SPI_CMD(SPI_BUS),SPI_USR);
  SPI_WAIT_READY();
  if (READ_PERI_REG(SPI_USER(SPI_BUS))&SPI_RD_BYTE_ORDER) {
	  return READ_PERI_REG(SPI_W0(SPI_BUS))>>24;	//assuming data in is written to MSB. TBC
  } else {
	  return READ_PERI_REG(SPI_W0(SPI_BUS));
  }
}

/* ======================================================================
   Byte by byte reference versions. They are what the bursts are checked against, and what
   everything falls back to when a burst is not allowed.
   ====================================================================== */

uint8_t spi_burst_enabled = 1;

static void spi_bytewise_out(const uint8_t *data, uint32_t len)
{
	while (len--)
		spi_put_char(*data++);
}

static void spi_bytewise_cmd(uint8_t cmd, uint32_t addr)
{
	spi_put_char(cmd);
	spi_put_char((addr >> 16) & 0xFF);
	spi_put_char((addr >> 8) & 0xFF);
	spi_put_char(addr & 0xFF);
}

/* Data out byte by byte, but the register setup (which bits are used, how many) is done once instead
   of before every byte: about a third of the work of spi_put_char(), the same pattern on the wire. */
void spi_write_bytes(const uint8_t *data, uint32_t len)
{
	uint32_t big_endian;

	if (len == 0)
		return;

	SPI_WAIT_READY();
	CLEAR_PERI_REG_MASK(SPI_USER(SPI_BUS), SPI_USR_MISO | SPI_USR_COMMAND | SPI_USR_ADDR | SPI_USR_DUMMY);
	SET_PERI_REG_MASK(SPI_USER(SPI_BUS), SPI_USR_MOSI);
	WRITE_PERI_REG(SPI_USER1(SPI_BUS), SPI_USR_ADDR_BITLEN << SPI_USR_ADDR_BITLEN_S |
					(7 & SPI_USR_MOSI_BITLEN) << SPI_USR_MOSI_BITLEN_S |
					SPI_USR_MISO_BITLEN << SPI_USR_MISO_BITLEN_S |
					SPI_USR_DUMMY_CYCLELEN << SPI_USR_DUMMY_CYCLELEN_S);	/* same fields as spi_put_char() */
	big_endian = READ_PERI_REG(SPI_USER(SPI_BUS)) & SPI_WR_BYTE_ORDER;

	while (len--) {
		SPI_WAIT_READY();	/* the previous byte is out (immediately true for the first one) */
		WRITE_PERI_REG(SPI_W0(SPI_BUS), big_endian ? ((uint32_t)*data << 24) : (uint32_t)*data);
		SET_PERI_REG_MASK(SPI_CMD(SPI_BUS), SPI_USR);
		data++;
	}
	SPI_WAIT_READY();
}

void spi_sdi_write(const uint8_t *data, uint32_t len)
{
#if SDI_MODE == 2
	spi_write_buf(data, len);
#elif SDI_MODE == 1
	spi_write_bytes(data, len);
#else
	spi_bytewise_out(data, len);
#endif
}

#if SPI_USE_BURST

/* ======================================================================
   Burst transfers
   The data goes through the 16 word (64 byte) SPI data buffer, so one transaction moves up to 64
   bytes where spi_put_char()/spi_get_char() need a full register setup for every single byte.

   The word <-> byte packing below is the little endian one (first byte in the lowest byte of a
   word) that works with the SPI byte order bits cleared. They are set for the single byte
   functions (VS1053_HW_init), so every burst clears them and puts them back afterwards.
   ====================================================================== */

#define SPI_BYTE_ORDER_BITS  (SPI_WR_BYTE_ORDER | SPI_RD_BYTE_ORDER)
#define SPI_W_REG(j)         (SPI_W0(SPI_BUS) + ((j) * 4))

static void spi_load_words(const uint8_t *data, uint32_t len)
{
	uint32_t i, j, w;

	for (i = 0; i + 4 <= len; i += 4) {
		w = (uint32_t)data[i] | ((uint32_t)data[i + 1] << 8) | ((uint32_t)data[i + 2] << 16) | ((uint32_t)data[i + 3] << 24);
		WRITE_PERI_REG(SPI_W_REG(i >> 2), w);
	}
	if (i < len) {	/* last, partial word: the bytes that are not sent stay 0 */
		w = 0;
		for (j = 0; i + j < len; j++)
			w |= (uint32_t)data[i + j] << (8 * j);
		WRITE_PERI_REG(SPI_W_REG(i >> 2), w);
	}
}

static void spi_unload_words(uint8_t *data, uint32_t len)
{
	uint32_t i, j, w;

	for (i = 0; i < len; i += 4) {
		w = READ_PERI_REG(SPI_W_REG(i >> 2));
		for (j = 0; j < 4 && i + j < len; j++)
			data[i + j] = (uint8_t)(w >> (8 * j));
	}
}

/* one plain data-out burst, 1..SPI_BURST_MAX bytes */
static void spi_burst_out(const uint8_t *data, uint32_t n)
{
	uint32_t order;

	SPI_WAIT_READY();
	order = READ_PERI_REG(SPI_USER(SPI_BUS)) & SPI_BYTE_ORDER_BITS;
	CLEAR_PERI_REG_MASK(SPI_USER(SPI_BUS), SPI_BYTE_ORDER_BITS | SPI_USR_COMMAND | SPI_USR_ADDR | SPI_USR_DUMMY | SPI_USR_MISO);
	SET_PERI_REG_MASK(SPI_USER(SPI_BUS), SPI_USR_MOSI);

	WRITE_PERI_REG(SPI_USER1(SPI_BUS), (((n * 8) - 1) & SPI_USR_MOSI_BITLEN) << SPI_USR_MOSI_BITLEN_S);
	spi_load_words(data, n);

	SET_PERI_REG_MASK(SPI_CMD(SPI_BUS), SPI_USR);
	SPI_WAIT_READY();
	SET_PERI_REG_MASK(SPI_USER(SPI_BUS), order);
}

void spi_write_buf(const uint8_t *data, uint32_t len)
{
	uint32_t n;

	if (!spi_burst_enabled) {
		spi_bytewise_out(data, len);
		return;
	}

	while (len > 0) {
		n = (len > SPI_BURST_MAX) ? SPI_BURST_MAX : len;
		spi_burst_out(data, n);
		data += n;
		len -= n;
	}
}

void spi_cmd_write(uint8_t cmd, uint32_t addr, const uint8_t *data, uint32_t len)
{
	uint32_t order;

	if (len == 0 || len > SPI_BURST_MAX)
		return;

	if (!spi_burst_enabled) {
		spi_bytewise_cmd(cmd, addr);
		spi_bytewise_out(data, len);
		return;
	}

	SPI_WAIT_READY();
	order = READ_PERI_REG(SPI_USER(SPI_BUS)) & SPI_BYTE_ORDER_BITS;
	CLEAR_PERI_REG_MASK(SPI_USER(SPI_BUS), SPI_BYTE_ORDER_BITS | SPI_USR_DUMMY | SPI_USR_MISO);
	SET_PERI_REG_MASK(SPI_USER(SPI_BUS), SPI_USR_COMMAND | SPI_USR_ADDR | SPI_USR_MOSI);

	WRITE_PERI_REG(SPI_USER1(SPI_BUS),
		((((len * 8) - 1) & SPI_USR_MOSI_BITLEN) << SPI_USR_MOSI_BITLEN_S) |
		((23 & SPI_USR_ADDR_BITLEN) << SPI_USR_ADDR_BITLEN_S));		/* 24 bit address */
	WRITE_PERI_REG(SPI_ADDR(SPI_BUS), addr << 8);
	WRITE_PERI_REG(SPI_USER2(SPI_BUS), ((7 & SPI_USR_COMMAND_BITLEN) << SPI_USR_COMMAND_BITLEN_S) | cmd);
	spi_load_words(data, len);

	SET_PERI_REG_MASK(SPI_CMD(SPI_BUS), SPI_USR);
	SPI_WAIT_READY();
	SET_PERI_REG_MASK(SPI_USER(SPI_BUS), order);
}

void spi_cmd_read(uint8_t cmd, uint32_t addr, uint8_t *data, uint32_t len)
{
	uint32_t order, i;

	if (len == 0 || len > SPI_BURST_MAX)
		return;

	if (!spi_burst_enabled) {
		spi_bytewise_cmd(cmd, addr);
		for (i = 0; i < len; i++)
			data[i] = spi_get_char();
		return;
	}

	SPI_WAIT_READY();
	order = READ_PERI_REG(SPI_USER(SPI_BUS)) & SPI_BYTE_ORDER_BITS;
	CLEAR_PERI_REG_MASK(SPI_USER(SPI_BUS), SPI_BYTE_ORDER_BITS | SPI_USR_DUMMY | SPI_USR_MOSI);
	SET_PERI_REG_MASK(SPI_USER(SPI_BUS), SPI_USR_COMMAND | SPI_USR_ADDR | SPI_USR_MISO);

	WRITE_PERI_REG(SPI_USER1(SPI_BUS),
		((((len * 8) - 1) & SPI_USR_MISO_BITLEN) << SPI_USR_MISO_BITLEN_S) |
		((23 & SPI_USR_ADDR_BITLEN) << SPI_USR_ADDR_BITLEN_S));		/* 24 bit address */
	WRITE_PERI_REG(SPI_ADDR(SPI_BUS), addr << 8);
	WRITE_PERI_REG(SPI_USER2(SPI_BUS), ((7 & SPI_USR_COMMAND_BITLEN) << SPI_USR_COMMAND_BITLEN_S) | cmd);

	SET_PERI_REG_MASK(SPI_CMD(SPI_BUS), SPI_USR);
	SPI_WAIT_READY();
	spi_unload_words(data, len);
	SET_PERI_REG_MASK(SPI_USER(SPI_BUS), order);
}

#else /* SPI_USE_BURST == 0: everything byte by byte */

void spi_write_buf(const uint8_t *data, uint32_t len)
{
	spi_bytewise_out(data, len);
}

void spi_cmd_write(uint8_t cmd, uint32_t addr, const uint8_t *data, uint32_t len)
{
	spi_bytewise_cmd(cmd, addr);
	spi_bytewise_out(data, len);
}

void spi_cmd_read(uint8_t cmd, uint32_t addr, uint8_t *data, uint32_t len)
{
	uint32_t i;

	spi_bytewise_cmd(cmd, addr);
	for (i = 0; i < len; i++)
		data[i] = spi_get_char();
}

#endif
