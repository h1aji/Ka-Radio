/*
 * OLED/LCD status display - see include/display.h for the overall design
 */

#include "esp_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display.h"
#include "ssd1306.h"
#include "interface.h"
#include "webclient.h"
#include "eeprom.h"
#include "ntp.h"

// Not declared in any header (same gap webserver.c's caller in interface.c
// works around): forward-declare it here instead of touching webserver.h.
extern uint16_t getVolume(void);

// Defined in the "Screensaver" section below, called from displayTick() above it.
static void displayScreensaverDraw(void);

/* ---------------------------------------------------------------------- 
   Panel-specific drawing. Only SSD1306 today - keep every direct call to
   ssd1306.h behind these helpers so another panel can be slotted in here
   later without touching the status model/scrolling logic below.
   ---------------------------------------------------------------------- */

#define DISPLAY_FONT Font_7x10

// Custom glyphs appended to Font_7x10 (ssd1306/fonts.c), right after '~' (0x7E)
#define DISPLAY_GLYPH_PLAY  "\x7f"
#define DISPLAY_GLYPH_PAUSE "\x80"
#define DISPLAY_GLYPH_STOP  "\x81"

// Panel brightness: normal, and dimmed while the screensaver clock is shown (less current, less OLED wear)
#define DISPLAY_CONTRAST_NORMAL 0x9F
#define DISPLAY_CONTRAST_DIM    0x20

// 1 = rotate the picture by 180 degrees (display mounted upside down), 0 = normal orientation
#define DISPLAY_ROTATE_180 1

// Splash screen shown at start-up: "Ka-Radio" / "Loading ..." / IP address
#define DISPLAY_SPLASH_WAIT_MS     30000  // max time to wait for a real address before giving up and showing it anyway
#define DISPLAY_SPLASH_MS          5000   // how long the splash stays on screen once the address is shown
#define DISPLAY_SPLASH_TITLE_SCALE 2      // title = Font_7x10 magnified 2x ("Ka-Radio" = 112 x 20 px), no extra font data in RAM

// Y position of the address line, shared with the screensaver so both put it in the same spot
#define DISPLAY_SPLASH_IP_Y(rowH)  (6 + (rowH) * DISPLAY_SPLASH_TITLE_SCALE + 8 + (rowH) + 4)

// Screensaver: big HH:MM clock, shown once nothing (track/station/volume) has changed for a while
#define DISPLAY_SCREENSAVER_IDLE_PLAYING_MS 60000  // idle time before the screensaver kicks in while playing
#define DISPLAY_SCREENSAVER_IDLE_STOPPED_MS 20000  // idle time before the screensaver kicks in while stopped
#define DISPLAY_SCREENSAVER_IDLE_PLAYING_TICKS (DISPLAY_SCREENSAVER_IDLE_PLAYING_MS / DISPLAY_TICK_MS)
#define DISPLAY_SCREENSAVER_IDLE_STOPPED_TICKS (DISPLAY_SCREENSAVER_IDLE_STOPPED_MS / DISPLAY_TICK_MS)
#define DISPLAY_SCREENSAVER_CLOCK_SCALE 3      // Font_7x10 x3 = 21 x 30 px digits, "HH:MM" is then 105 px wide

static void disp_clear(void)
{
	SSD1306_Fill(SSD1306_COLOR_BLACK);
}

static void disp_puts(uint16_t x, uint16_t y, char *str)
{
	SSD1306_GotoXY(x, y);
	SSD1306_Puts(str, &DISPLAY_FONT, SSD1306_COLOR_WHITE);
}

// Centered text, magnified by 'scale' (1 = normal size)
static void disp_puts_centered(uint16_t y, char *str, uint8_t scale)
{
	uint16_t w = strlen(str) * DISPLAY_FONT.FontWidth * scale;

	SSD1306_GotoXY((w < SSD1306_WIDTH) ? (SSD1306_WIDTH - w) / 2 : 0, y);
	SSD1306_PutsScaled(str, &DISPLAY_FONT, SSD1306_COLOR_WHITE, scale);
}

static void disp_volume_bar(uint16_t y, uint16_t vol /* 0..255 */)
{
	SSD1306_DrawRectangle(0, y, SSD1306_WIDTH - 1, 6, SSD1306_COLOR_WHITE);
	SSD1306_DrawFilledRectangle(1, y + 1, (uint16_t)((SSD1306_WIDTH - 2) * vol / 255), 4, SSD1306_COLOR_WHITE);
}

// Sends only the pages that changed, one at a time. Between pages the task sleeps for a tick,
// so no I2C burst blocks the other tasks (or the Wi-Fi stack) for more than a few milliseconds.
static void disp_update(void)
{
	uint8_t page;

	for (page = 0; page < SSD1306_PAGES; page++)
		if (SSD1306_UpdatePage(page))
			vTaskDelay(1);
}

/* ---------------------------------------------------------------------- 
   Status model: one entry per scrolling text line (station, genre, the
   two halves of the current title), plus volume/clock.
   ---------------------------------------------------------------------- */

#define DISPLAY_TICK_MS            500   // 2 Hz: scrolling and redraws
#define DISPLAY_TICKS_PER_SEC      (1000 / DISPLAY_TICK_MS)
#define DISPLAY_SCROLL_PAUSE_TICKS (1500 / DISPLAY_TICK_MS)   // ~1.5s pause before a scrolled line loops back

typedef struct {
	char text[64];
	uint16_t offset;	// first visible character, for scrolling
	uint16_t pause;		// ticks left to wait once the line reaches its end
} display_line_t;

enum {
	LINE_STATION,
	LINE_GENRE,
	LINE_TITLE1,
	LINE_TITLE2,
	LINE_COUNT
};

static display_line_t lines[LINE_COUNT];
static uint16_t dispVolume;
static char dispTime[24];
static uint16_t cachedStationIdx;
static char localStationName[64];
static bool redrawPending;	    // force one full redraw (e.g. right after the splash screen)
static char dispClockHHMM[6];	// "HH:MM", kept in sync with dispTime, shown by the screensaver
static uint16_t idleTicks;	    // ticks since the last station/track/volume change
static bool screensaverActive;

// Copies text into a line if it changed, resetting its scroll position.
// Returns true if the line changed (i.e. the screen needs a redraw).
static bool displayLineSet(display_line_t *ln, const char *text)
{
	if (text == NULL)
		text = "";
	if (strcmp(ln->text, text) == 0)
		return false;

	strncpy(ln->text, text, sizeof(ln->text) - 1);
	ln->text[sizeof(ln->text) - 1] = 0;
	ln->offset = 0;
	ln->pause = 0;
	return true;
}

// Advances a line's scroll position by one tick. Returns true if the
// visible text changed (i.e. the screen needs a redraw).
static bool displayLineScroll(display_line_t *ln)
{
	uint16_t maxChars = SSD1306_WIDTH / DISPLAY_FONT.FontWidth;
	uint16_t len = strlen(ln->text);

	if (len <= maxChars) {
		if (ln->offset != 0) {
			ln->offset = 0;
			return true;
		}
		return false;
	}

	if (ln->pause > 0) {
		ln->pause--;
		if (ln->pause == 0) {
			ln->offset = 0;
			return true;
		}
		return false;
	}

	ln->offset++;
	if (ln->offset + maxChars >= len)
		ln->pause = DISPLAY_SCROLL_PAUSE_TICKS;

	return true;
}

// Splits "artist - title" metadata into two lines, trimming stray spaces,
// same as the Arduino sketch's separator().
static void displaySplitTitle(const char *meta, char *out1, size_t sz1, char *out2, size_t sz2)
{
	char buf[128];
	char *p = buf;
	char *sep;
	size_t l;

	if (meta == NULL)	// getMeta() is NULL until the first metadata arrives
		meta = "";

	strncpy(buf, meta, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;

	while (*p == ' ')
		p++;
	l = strlen(p);
	while (l > 0 && p[l - 1] == ' ')
		p[--l] = 0;

	sep = strstr(p, " - ");
	if (sep != NULL) {
		*sep = 0;
		strncpy(out1, p, sz1 - 1);
		out1[sz1 - 1] = 0;
		strncpy(out2, sep + 3, sz2 - 1);
		out2[sz2 - 1] = 0;
	} else {
		strncpy(out1, p, sz1 - 1);
		out1[sz1 - 1] = 0;
		out2[0] = 0;
	}
}

// getStation() reads flash, so only call it when the station actually
// changed instead of on every tick.
static void displayRefreshStationName(void)
{
	struct shoutcast_info *si;

	if (currentStation == cachedStationIdx)
		return;

	si = getStation(currentStation);
	strncpy(localStationName, (si != NULL) ? si->name : "", sizeof(localStationName) - 1);
	localStationName[sizeof(localStationName) - 1] = 0;
	if (si != NULL)
		free(si);	// getStation() returns a malloc'ed copy
	cachedStationIdx = currentStation;
}

static void displayDraw(void)
{
	uint16_t rowH = DISPLAY_FONT.FontHeight;
	char status[32];

	disp_clear();

	disp_puts(0, 0 * rowH, lines[LINE_STATION].text + lines[LINE_STATION].offset);
	disp_puts(0, 1 * rowH, lines[LINE_GENRE].text + lines[LINE_GENRE].offset);
	disp_puts(0, 2 * rowH, lines[LINE_TITLE1].text + lines[LINE_TITLE1].offset);
	disp_puts(0, 3 * rowH, lines[LINE_TITLE2].text + lines[LINE_TITLE2].offset);

	disp_volume_bar(SSD1306_HEIGHT - 2 * rowH, dispVolume);

	snprintf(status, sizeof(status), "%s %s", getState() ? DISPLAY_GLYPH_PLAY : DISPLAY_GLYPH_STOP, dispTime);
	disp_puts(0, SSD1306_HEIGHT - rowH, status);

	disp_update();
}

static void displayTick(void)
{
	static uint8_t secTick = 0;
	struct icyHeader *icy = clientGetHeader();
	bool dirty = false;
	bool activity = false;	// a real station/track/volume change, as opposed to a scroll/clock tick - resets the screensaver's idle timer
	bool changed;
	uint16_t vol;
	char t1[64], t2[64];
	int i;

	if (redrawPending) {
		redrawPending = false;
		dirty = true;
	}

	displayRefreshStationName();

	{
		const char *icyName = (icy != NULL) ? icy->members.single.name : NULL;
		const char *name = (icyName != NULL && icyName[0] != 0) ? icyName : localStationName;
		changed = displayLineSet(&lines[LINE_STATION], name);
		dirty |= changed;
		activity |= changed;
	}

	changed = displayLineSet(&lines[LINE_GENRE], (icy != NULL) ? icy->members.single.genre : NULL);
	dirty |= changed;
	activity |= changed;

	displaySplitTitle(getState() ? getMeta() : "STOPPED", t1, sizeof(t1), t2, sizeof(t2));
	changed = displayLineSet(&lines[LINE_TITLE1], t1);
	dirty |= changed;
	activity |= changed;
	changed = displayLineSet(&lines[LINE_TITLE2], t2);
	dirty |= changed;
	activity |= changed;

	if (++secTick >= DISPLAY_TICKS_PER_SEC) {
		struct tm *dt;

		secTick = 0;

		// getVolume() may read the VS1053 over SPI, so poll it only once a second
		vol = getVolume();
		if (vol != dispVolume) {
			dispVolume = vol;
			dirty = true;
			activity = true;
		}
		if (ntp_get_time_cached(&dt)) {
			char timeStr[24];

			snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d",
				dt->tm_year + 1900, dt->tm_mon + 1, dt->tm_mday,
				dt->tm_hour, dt->tm_min, dt->tm_sec);
			if (strcmp(timeStr, dispTime) != 0) {
				strcpy(dispTime, timeStr);
				snprintf(dispClockHHMM, sizeof(dispClockHHMM), "%02d:%02d", dt->tm_hour, dt->tm_min);
				dirty = true;
			}
		}
	}

	for (i = 0; i < LINE_COUNT; i++)
		dirty |= displayLineScroll(&lines[i]);

	{
		uint16_t idleThreshold = getState() ? DISPLAY_SCREENSAVER_IDLE_PLAYING_TICKS : DISPLAY_SCREENSAVER_IDLE_STOPPED_TICKS;

		if (activity)
			idleTicks = 0;
		else if (idleTicks < idleThreshold)
			idleTicks++;

		if ((idleTicks >= idleThreshold) != screensaverActive) {
			screensaverActive = !screensaverActive;
			SSD1306_SetContrast(screensaverActive ? DISPLAY_CONTRAST_DIM : DISPLAY_CONTRAST_NORMAL);
			dirty = true;
		}
	}

	if (dirty) {
		if (screensaverActive)
			displayScreensaverDraw();
		else
			displayDraw();
	}
}

/* ---------------------------------------------------------------------- */
/* Splash screen                                                           */
/* ---------------------------------------------------------------------- */

// Address of the device: the station interface address, or the soft-AP address
// when the radio runs as an access point. "0.0.0.0" until an address is assigned.
static void displayGetIp(char *buf, size_t size)
{
	struct ip_info info;
	uint32_t ip = 0;

	if (wifi_get_ip_info((wifi_get_opmode() == SOFTAP_MODE) ? SOFTAP_IF : STATION_IF, &info))
		ip = info.ip.addr;

	snprintf(buf, size, "%d.%d.%d.%d", (int)(ip & 0xff), (int)((ip >> 8) & 0xff),
		(int)((ip >> 16) & 0xff), (int)((ip >> 24) & 0xff));
}

/* ----------------------------------------------------------------------
   Screensaver
   ---------------------------------------------------------------------- */

static void displayScreensaverDraw(void)
{
	uint16_t rowH = DISPLAY_FONT.FontHeight;
	char ip[20];

	displayGetIp(ip, sizeof(ip));

	disp_clear();
	disp_puts_centered((SSD1306_HEIGHT - rowH * DISPLAY_SCREENSAVER_CLOCK_SCALE) / 2, dispClockHHMM, DISPLAY_SCREENSAVER_CLOCK_SCALE);
	disp_puts_centered(DISPLAY_SPLASH_IP_Y(rowH), ip, 1);
	disp_update();
}

static void displaySplashDraw(char *ip)
{
	uint16_t rowH = DISPLAY_FONT.FontHeight;

	disp_clear();
	disp_puts_centered(6, "Ka-Radio", DISPLAY_SPLASH_TITLE_SCALE);
	disp_puts_centered(6 + rowH * DISPLAY_SPLASH_TITLE_SCALE + 8, "Loading ...", 1);
	disp_puts_centered(DISPLAY_SPLASH_IP_Y(rowH), ip, 1);
	disp_update();
}

// Shows the splash screen. First waits (up to DISPLAY_SPLASH_WAIT_MS) for a real
// address, since the radio is still associating/DHCP'ing at this point, then keeps
// the splash up for DISPLAY_SPLASH_MS once an address is on screen.
static void displaySplash(void)
{
	char ip[20], newIp[20];
	portTickType start = xTaskGetTickCount();

	displayGetIp(ip, sizeof(ip));
	displaySplashDraw(ip);

	while (strcmp(ip, "0.0.0.0") == 0 &&
			(xTaskGetTickCount() - start) < (DISPLAY_SPLASH_WAIT_MS / portTICK_RATE_MS)) {
		vTaskDelay(DISPLAY_TICK_MS / portTICK_RATE_MS);
		displayGetIp(ip, sizeof(ip));
	}
	displaySplashDraw(ip);

	start = xTaskGetTickCount();
	while ((xTaskGetTickCount() - start) < (DISPLAY_SPLASH_MS / portTICK_RATE_MS)) {
		vTaskDelay(DISPLAY_TICK_MS / portTICK_RATE_MS);

		displayGetIp(newIp, sizeof(newIp));
		if (strcmp(ip, newIp) != 0) {
			strcpy(ip, newIp);
			displaySplashDraw(ip);
		}
	}
}

static void displayTask(void *pvParams)
{
	displaySplash();

	for (;;) {
		displayTick();
		vTaskDelay(DISPLAY_TICK_MS / portTICK_RATE_MS);
	}
}

void ICACHE_FLASH_ATTR displayInit(void)
{
	int i;

	SSD1306_SetRotation(DISPLAY_ROTATE_180 ? SSD1306_ROTATE_180 : SSD1306_ROTATE_0);
	SSD1306_SetContrast(DISPLAY_CONTRAST_NORMAL);
	SSD1306_Init();

	for (i = 0; i < LINE_COUNT; i++)
		memset(&lines[i], 0, sizeof(lines[i]));
	dispVolume = 0;
	dispTime[0] = 0;
	dispClockHHMM[0] = 0;
	cachedStationIdx = (uint16_t)-1;
	localStationName[0] = 0;
	redrawPending = true;	// the task draws the splash screen first, then the normal screen
	idleTicks = 0;
	screensaverActive = false;

	xTaskCreate(displayTask, "display", 640, NULL, 1, NULL);	// lowest user priority: never in the way of the stream or Wi-Fi
}
