#!/bin/bash

#make COMPILE=gcc BOOT=new SPI_SIZE_MAP=6 APP=1

echo "Building APP1"
make NAME=new BOOT=new APP=1 SPI_SPEED=40 SPI_MODE=DIO SPI_SIZE_MAP=4

echo "Building APP2"
echo "!!!"
sleep 3

make NAME=new BOOT=new APP=2 SPI_SPEED=40 SPI_MODE=DIO SPI_SIZE_MAP=4

