#pragma once

#include <stdint.h>

// define SPI interface
#define SPI_BUS       1 //HSPI_HOST

//private functions
void spi_init_semaphore();
uint8_t spi_take_semaphore();
void spi_give_semaphore();
void spi_put_char(uint8_t outB);
uint8_t spi_get_char();
void spi_speed_up();
void spi_speed_down();
