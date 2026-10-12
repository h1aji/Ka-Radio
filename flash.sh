#!/bin/bash
#
# Flash the radio firmware (4 MB flash, size map 4, DIO mode).
#
#   ./flash.sh         boot loader + both application slots
#   ./flash.sh full    also writes esp_init_data_default.bin and blank.bin
#                      (first flash of a new module, or to reset the SDK system parameters)
#
# SDK_PATH and BIN_PATH are read from app.env (next to this script), e.g.:
#   SDK_PATH=$HOME/esp/ESP8266_RTOS_SDK
#   BIN_PATH=$HOME/esp/ESP8266_RTOS_SDK/Ka-Radio/bin
# Optional environment variables: PORT, BAUD, ESPTOOL, APP_ENV (path of another env file)

set -e
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

PORT=${PORT:-/dev/ttyUSB0}
BAUD=${BAUD:-921600}
ESPTOOL=${ESPTOOL:-$HOME/esp/esp-open-sdk/esptool/esptool.py}

# ---- images --------------------------------------------------------------
# boot loader: BIN_PATH first, then the SDK's own bin folder
BOOT=$BIN_PATH/boot_v1.7.bin
[ -f "$BOOT" ] || BOOT=$SDK_PATH/bin/boot_v1.7.bin
APP1=$BIN_PATH/upgrade/user1.4096.new.4.bin
APP2=$BIN_PATH/upgrade/user2.4096.new.4.bin

# addresses must stay in ascending order
IMAGES=(0x00000 "$BOOT" 0x01000 "$APP1")
FILES=("$BOOT" "$APP1" "$APP2")

if [ "$1" = "full" ]; then
    INIT=$SDK_PATH/bin/esp_init_data_default.bin
    BLANK=$SDK_PATH/bin/blank.bin
    FILES+=("$INIT" "$BLANK")
    IMAGES+=(0x7E000 "$BLANK")
fi

IMAGES+=(0x81000 "$APP2")

if [ "$1" = "full" ]; then
    IMAGES+=(0x3FC000 "$INIT" 0x3FE000 "$BLANK")
fi

for f in "${FILES[@]}"; do
    [ -f "$f" ] || { echo "Missing file: $f"; exit 1; }
done

echo "Flashing to $PORT at $BAUD baud"
"$ESPTOOL" --baud "$BAUD" --port "$PORT" \
    write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
    "${IMAGES[@]}"
