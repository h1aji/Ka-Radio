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

/* ---------------------------------------------------------------------- */
/* Panel-specific drawing. Only SSD1306 today - keep every direct call to */
/* ssd1306.h behind these helpers so another panel can be slotted in here */
/* later without touching the status model/scrolling logic below.        */
/* ---------------------------------------------------------------------- */

#define DISPLAY_FONT Font_7x10

static void disp_clear(void)
{
	SSD1306_Fill(SSD1306_COLOR_BLACK);
}

static void disp_puts(uint16_t x, uint16_t y, char *str)
{
	SSD1306_GotoXY(x, y);
	SSD1306_Puts(str, &DISPLAY_FONT, SSD1306_COLOR_WHITE);
}

static void disp_volume_bar(uint16_t y, uint16_t vol /* 0..255 */)
{
	SSD1306_DrawRectangle(0, y, SSD1306_WIDTH - 1, 6, SSD1306_COLOR_WHITE);
	SSD1306_DrawFilledRectangle(1, y + 1, (uint16_t)((SSD1306_WIDTH - 2) * vol / 255), 4, SSD1306_COLOR_WHITE);
}

static void disp_update(void)
{
	SSD1306_UpdateScreen();
}

/* ---------------------------------------------------------------------- */
/* Status model: one entry per scrolling text line (station, genre, the   */
/* two halves of the current title), plus volume/clock.                   */
/* ---------------------------------------------------------------------- */

#define DISPLAY_TICK_MS            250   // 4 Hz, same cadence as the Arduino's Timer1
#define DISPLAY_TICKS_PER_SEC      (1000 / DISPLAY_TICK_MS)
#define DISPLAY_SCROLL_PAUSE_TICKS 6     // ~1.5s pause before a scrolled line loops back

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

	snprintf(status, sizeof(status), "%s %s", getState() ? ">" : "||", dispTime);
	disp_puts(0, SSD1306_HEIGHT - rowH, status);

	disp_update();
}

static void displayTick(void)
{
	static uint8_t secTick = 0;
	struct icyHeader *icy = clientGetHeader();
	bool dirty = false;
	uint16_t vol;
	char t1[64], t2[64];
	int i;

	displayRefreshStationName();

	{
		const char *icyName = (icy != NULL) ? icy->members.single.name : NULL;
		const char *name = (icyName != NULL && icyName[0] != 0) ? icyName : localStationName;
		dirty |= displayLineSet(&lines[LINE_STATION], name);
	}

	dirty |= displayLineSet(&lines[LINE_GENRE], (icy != NULL) ? icy->members.single.genre : NULL);

	displaySplitTitle(getState() ? getMeta() : "STOPPED", t1, sizeof(t1), t2, sizeof(t2));
	dirty |= displayLineSet(&lines[LINE_TITLE1], t1);
	dirty |= displayLineSet(&lines[LINE_TITLE2], t2);

	vol = getVolume();
	if (vol != dispVolume) {
		dispVolume = vol;
		dirty = true;
	}

	if (++secTick >= DISPLAY_TICKS_PER_SEC) {
		struct tm *dt;

		secTick = 0;
		if (ntp_get_time_cached(&dt)) {
			char timeStr[24];

			snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d",
				dt->tm_year + 1900, dt->tm_mon + 1, dt->tm_mday,
				dt->tm_hour, dt->tm_min, dt->tm_sec);
			if (strcmp(timeStr, dispTime) != 0) {
				strcpy(dispTime, timeStr);
				dirty = true;
			}
		}
	}

	for (i = 0; i < LINE_COUNT; i++)
		dirty |= displayLineScroll(&lines[i]);

	if (dirty)
		displayDraw();
}

static void displayTask(void *pvParams)
{
	for (;;) {
		displayTick();
		vTaskDelay(DISPLAY_TICK_MS / portTICK_RATE_MS);
	}
}

void ICACHE_FLASH_ATTR displayInit(void)
{
	int i;

	SSD1306_Init();

	disp_clear();
	disp_puts(0, 0, "KaRadio");
	disp_puts(0, DISPLAY_FONT.FontHeight, getIp());
	disp_update();

	for (i = 0; i < LINE_COUNT; i++)
		memset(&lines[i], 0, sizeof(lines[i]));
	dispVolume = 0;
	dispTime[0] = 0;
	cachedStationIdx = (uint16_t)-1;
	localStationName[0] = 0;

	xTaskCreate(displayTask, "display", 512, NULL, 2, NULL);
}
