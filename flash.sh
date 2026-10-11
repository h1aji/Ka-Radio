#!/bin/bash


$HOME/esp/esp-open-sdk/esptool/esptool.py --baud 921600 --port /dev/ttyUSB0 \
  write_flash 0x00000 "./bin/boot_v1.7.bin" \
              0x01000 "./bin/upgrade/user1.4096.new.4.bin" \
              0x81000 "./bin/upgrade/user2.4096.new.4.bin" \
              --flash_mode dio --flash_freq 40m --flash_size 4MB

