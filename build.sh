#!/bin/bash
set -e    # stop if any build fails

OPTS="NAME=new BOOT=new SPI_SPEED=40 SPI_MODE=DIO SPI_SIZE_MAP=4"

for APP in 1 2; do
    echo "Building APP$APP"
    make clean
    make $OPTS APP=$APP
done