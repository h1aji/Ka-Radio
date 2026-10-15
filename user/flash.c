/******************************************************************************
 * 
 * Copyright 2017 karawin (http://www.karawin.fr)
 *
*******************************************************************************/

#include "esp_common.h"
#include "esp8266/eagle_soc.h"
#include "spi_flash.h"

#include "flash.h"
#include "spilib.h"


ICACHE_FLASH_ATTR uint32_t flashRead(void *to, uint32_t fromaddr, uint32_t size)
{
  uint32_t ret;
  fromaddr -= INTERNAL_FLASH_START_ADDRESS;
// int r;
// printf("flasRead from %x, size: %d  TO:%x\n",fromaddr,size,to);
  WRITE_PERI_REG(0x60000914, 0x73);
  spi_take_semaphore();
  spi_speed_2mhz(); //2MHz
  ret = spi_flash_read(fromaddr, (uint32_t *)to, size);
  spi_give_semaphore();
  return ret;
}

ICACHE_FLASH_ATTR uint32_t flashWrite(void *data, uint32_t fromaddr, uint32_t size)
{
  uint32_t ret;
  fromaddr -= INTERNAL_FLASH_START_ADDRESS;
// printf("flasWrite from %x, size: %d  Data: %x\n",fromaddr,size,data);
// int r;
  WRITE_PERI_REG(0x60000914, 0x73);
  spi_take_semaphore();
  spi_speed_2mhz(); //2MHz
  ret = spi_flash_write(fromaddr, (uint32_t *)data, size);
  spi_give_semaphore();
  return ret;
}
