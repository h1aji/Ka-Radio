#!/bin/bash

if [[ "$1" == 'erase' ]] ; then
    esptool --chip esp8266 --port /dev/ttyUSB0 erase_flash
fi

esptool \
   --chip esp8266 --port /dev/ttyUSB0 --baud 921600 --before default_reset \
   --after hard_reset write_flash -u --flash_mode dio \
   --flash_freq 40m --flash_size detect \
   0x1000 binaries/bootloader.bin \
   0x10000 binaries/KaRadio.bin \
   0x8000 binaries/partitions.bin
