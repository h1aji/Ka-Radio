#!/bin/bash
set -e    # stop if any build fails

cd "$(dirname "$0")"

# ---- load configuration ----
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


# ---- build webpage ----
if  [[ $1 = "--with-webpage" ]]; then
    echo "Building webpage"
    cd webpage
    python3 ./build.py
    cd ..
else
    echo "If you want to build webpage, then run it:"
    echo "./build.sh --with-webpage"
fi

# find and delete previous builds
find . -type d -name ".output" -exec rm -rf {} +


OPTS="NAME=new BOOT=new SPI_SPEED=40 SPI_MODE=DIO SPI_SIZE_MAP=4"

for APP in 1 2; do
    echo "Building APP$APP"
    make clean
    make $OPTS APP=$APP
done
