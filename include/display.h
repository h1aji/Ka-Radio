/*
 * OLED/LCD status display for Ka-Radio (user/display.c)
 *
 * Native port of the companion Arduino sketch (ssd1306/ssd1306.arduino),
 * which used u8glib and a serial text protocol pushed from the ESP8266 to
 * draw station/title/volume/clock status on a small screen. Here the
 * ESP8266 drives the panel itself, polling the firmware's own state
 * (getMeta(), getState(), getVolume(), getIp(), getStation(),
 * clientGetHeader(), ntp_get_time()) instead of parsing a UART stream, so
 * no change to any of those modules is required.
 *
 * Only the SSD1306 driver (ssd1306/ssd1306.h) is wired in today. All the
 * panel-specific drawing calls live behind the disp_* helpers at the top
 * of display.c, so a different panel can be added later without touching
 * the status model / scrolling logic that sits above them.
 */
#ifndef __DISPLAY_H__
#define __DISPLAY_H__

// Starts the status display: initialises the panel and launches the
// background task that polls Ka-Radio state and redraws the screen.
void displayInit(void);

#endif
