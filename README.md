# Le-Radio

Web radio based on ESP8266 and VS1053 \
This is a fork of https://github.com/karawin/Ka-Radio \
The goal of this project is to migrate it to SDK 3 

## Wiring

|GPIO |ESP8266  |VS1053  |23LC1024  |
|-----|------|-----------|----------|
|16   |D0    |-    |CS      |
|05   |D1    |XDCS |        |
|04   |D2    |DREQ |        |
|00   |D3    |XRST |        |
|02   |D4    |-    |        |
|14   |D5    |SCK  |SCK     |
|12   |D6    |MISO |SO/SIO1 |
|13   |D7    |MOSI |SI/SIO0 |
|15   |D8    |XCS  |        |

## Issues
 - Get rid of c_types.h
 - Figure our what ledCallback is doing in main.c and get rid of it
 - fix includes
