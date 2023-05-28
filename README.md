# Ka-Radio

Web radio based on ESP8266 and VS1053 \
This is a fork of https://github.com/karawin/Ka-Radio32 \
The goal of this project is to migrate it to ESP8266-RTOS-SDK 3.4 \

Please note that wiring is slightly different from original project

## Wiring

|GPIO |ESP8266  |VS1053  |23LC1024  |
|-----|------|-----------|----------|
|16   |D0    |XDCS |        |
|05   |D1    |     |        |
|04   |D2    |     |        |
|00   |D3    |     |        |
|02   |D4    |XCS  |        |
|14   |D5    |SCK  |SCK     |
|12   |D6    |MISO |SO/SIO1 |
|13   |D7    |MOSI |SI/SIO0 |
|15   |D8    |     |CS      |
|10   |SDD3  |DREQ |        |

## Issues
 - TIMERG0 in ota.c (line 260)
 - SSL functionality not ported

## Roadmap
 - Implement control via IR (GPIO0)
 - Add support for I2C LCD2004 or SSD1306 (GPIO4 and GPIO5)
 - Spiram auto detect
