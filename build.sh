#!/bin/bash
set -e    # stop if any build fails

cd "$(dirname "$0")"

# ---- configuration -------------------------------------------------------
APP_ENV=${APP_ENV:-./app.env}
if [ ! -f "$APP_ENV" ]; then
    echo "Config file not found: $APP_ENV"
    exit 1
fi
set -a
# shellcheck disable=SC1090
. "$APP_ENV"
set +a

: "${SDK_PATH:?SDK_PATH is not set in $APP_ENV}"
: "${BIN_PATH:?BIN_PATH is not set in $APP_ENV}"

OPTS="NAME=new BOOT=new SPI_SPEED=40 SPI_MODE=DIO SPI_SIZE_MAP=4"

# find and delete previous builds
find . -type d -name ".output" -exec rm -rf {} +

for APP in 1 2; do
    echo "Building APP$APP"
    make clean
    make $OPTS APP=$APP
done
