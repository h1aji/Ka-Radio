#include "ssd1306.h"

#define ABS(x)   ((x) > 0 ? (x) : -(x))

/* SSD1306 data buffer */
static uint8_t SSD1306_Buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];

/* Private SSD1306 structure */
typedef struct {
	int16_t CurrentX;
	int16_t CurrentY;
	uint8_t Inverted;
	uint8_t Initialized;
	uint8_t Rotation;	/* SSD1306_ROTATION_t */
} SSD1306_t;

/* Private variable */
static SSD1306_t SSD1306;

/* What the panel currently shows. Only pages that differ from this copy are sent. */
static uint8_t SSD1306_Shadow[SSD1306_WIDTH * SSD1306_HEIGHT / 8];
static uint8_t SSD1306_ForcePages = 0xFF;	/* pages to send even if unchanged (after init / recovery) */

/*
 * Debug switch for hunting problems with the stream: 1 = everything runs as usual but nothing is
 * ever sent on the I2C bus. If the problem is gone with 1, the I2C traffic is the cause (CPU time
 * or electrical noise); if it stays, look at the rest of the display task.
 */
#ifndef SSD1306_I2C_DRY_RUN
#define SSD1306_I2C_DRY_RUN 0
#endif

/* Default contrast, as in esp-open-rtos. 0xFF (full) draws noticeably more current. */
#define SSD1306_CONTRAST_DEFAULT 0x9F
static uint8_t SSD1306_Contrast = SSD1306_CONTRAST_DEFAULT;

/* I2C failure handling: don't hammer a panel that does not answer */
#define SSD1306_RETRY_UPDATES 10	/* screen updates skipped after a failure before trying again */
static uint8_t SSD1306_Online = 1;
static uint8_t SSD1306_Backoff = 0;

/*
 * ssd1306_i2c.h only gives bit-banged start/stop/byte primitives, no
 * register-write helper, so start+address+control+data+stop is built here.
 */
static bool SSD1306_I2C_Write(uint8_t control, const uint8_t *data, uint16_t len) {
	uint16_t i;

#if SSD1306_I2C_DRY_RUN
	(void)control; (void)data; (void)len;
	return true;
#endif

	ssd1306_i2c_master_start();

	ssd1306_i2c_master_writeByte((SSD1306_I2C_ADDR << 1) | 0x00);
	if (!ssd1306_i2c_master_checkAck()) {
		ssd1306_i2c_master_stop();
		return false;
	}

	ssd1306_i2c_master_writeByte(control);
	if (!ssd1306_i2c_master_checkAck()) {
		ssd1306_i2c_master_stop();
		return false;
	}

	for (i = 0; i < len; i++) {
		ssd1306_i2c_master_writeByte(data[i]);
		if (!ssd1306_i2c_master_checkAck()) {
			ssd1306_i2c_master_stop();
			return false;
		}
	}

	ssd1306_i2c_master_stop();
	return true;
}

/* Failure of an I2C transfer: report once, then pause the updates for a while */
static void SSD1306_Failed(void) {
	if (SSD1306_Online)
		printf("SSD1306: display does not answer on I2C\n");
	SSD1306_Online = 0;
	SSD1306_Backoff = SSD1306_RETRY_UPDATES;
}

/* Several command bytes in one transfer (control byte 0x00 = commands) */
static void SSD1306_WriteCommands(const uint8_t *cmds, uint16_t len) {
	if (SSD1306_I2C_Write(0x00, cmds, len))
		SSD1306_Online = 1;
	else
		SSD1306_Failed();
}

/* Segment remap + COM scan direction: together they rotate the image by 180 degrees */
static void SSD1306_ApplyRotation(void) {
	uint8_t cmds[2];

	if (SSD1306.Rotation == SSD1306_ROTATE_180) {
		cmds[0] = 0xC0; //Set COM Output Scan Direction: normal (COM0 to COM63)
		cmds[1] = 0xA0; //--set segment re-map: column 0 maps to SEG0
	} else {
		cmds[0] = 0xC8; //Set COM Output Scan Direction: remapped (COM63 to COM0)
		cmds[1] = 0xA1; //--set segment re-map 0 to 127
	}
	SSD1306_WriteCommands(cmds, sizeof(cmds));
}

void SSD1306_SetRotation(SSD1306_ROTATION_t rotation) {
	SSD1306.Rotation = (rotation == SSD1306_ROTATE_180) ? SSD1306_ROTATE_180 : SSD1306_ROTATE_0;

	/* before SSD1306_Init() only remember the setting, Init() applies it */
	if (SSD1306.Initialized)
		SSD1306_ApplyRotation();
}

/* The whole init sequence, three transfers instead of one transfer per command.
   Oscillator, pre-charge and VCOMH values are the ones esp-open-rtos uses with the internal charge pump. */
static void SSD1306_SendInit(void) {
	uint8_t before_rotation[] = {
		0xAE,		//display off
		0x20, 0x02,	//Set Memory Addressing Mode: 00 Horizontal, 01 Vertical, 10 Page (used here), 11 Invalid
		0xB0,		//Set Page Start Address for Page Addressing Mode,0-7
		0x00,		//---set low column address
		0x10,		//---set high column address
		0x40,		//--set start line address
		0x81, 0x00	//--set contrast control register (value filled in below)
	};
	static const uint8_t after_rotation[] = {
		0xA6,		//--set normal display
		0xA8, SSD1306_HEIGHT - 1,	//--set multiplex ratio(1 to 64): one per row
		0xA4,		//0xa4,Output follows RAM content;0xa5,Output ignores RAM content
		0xD3, 0x00,	//-set display offset: not offset
		0xD5, 0x80,	//--set display clock divide ratio/oscillator frequency (reset default, lower EMI than 0xF0)
		0xD9, 0xF1,	//--set pre-charge period (for the internal charge pump)
		0xDA, (SSD1306_HEIGHT == 64) ? 0x12 : 0x02,	//--set com pins hardware configuration: alternative for 64 rows, sequential for 32
		0xDB, 0x40,	//--set vcomh
		0x8D, 0x14,	//--set DC-DC enable
		0xAF		//--turn on SSD1306 panel
	};

	before_rotation[sizeof(before_rotation) - 1] = SSD1306_Contrast;

	SSD1306_WriteCommands(before_rotation, sizeof(before_rotation));
	SSD1306_ApplyRotation();	//--set COM scan direction and segment re-map
	SSD1306_WriteCommands(after_rotation, sizeof(after_rotation));
}

void SSD1306_SetContrast(uint8_t contrast) {
	uint8_t cmds[2] = { 0x81, 0x00 };

	SSD1306_Contrast = contrast;

	/* before SSD1306_Init() only remember the value, Init() sends it */
	if (SSD1306.Initialized && SSD1306_Online) {
		cmds[1] = contrast;
		SSD1306_WriteCommands(cmds, sizeof(cmds));
	}
}

uint8_t SSD1306_Init(void) {
	/* Init I2C bus (SDA=GPIO4, SCL=GPIO5, see ssd1306_i2c.h) */
	ssd1306_i2c_master_gpio_init();

	/* Init LCD */
	SSD1306_Online = 1;
	SSD1306_Backoff = 0;
	SSD1306_SendInit();

	/* The panel RAM content is unknown after power-up: send every page once */
	SSD1306_ForcePages = 0xFF;

	/* Clear screen */
	SSD1306_Fill(SSD1306_COLOR_BLACK);

	/* Update screen */
	SSD1306_UpdateScreen();

	/* Set default values */
	SSD1306.CurrentX = 0;
	SSD1306.CurrentY = 0;

	/* Initialized */
	SSD1306.Initialized = 1;

	/* 1 if the panel answered, 0 if it was not detected on the I2C bus */
	return SSD1306_Online;
}

bool SSD1306_UpdatePage(uint8_t page) {
	uint8_t cmds[3];
	uint8_t mask;
	uint16_t first, last;
	const uint8_t *src;
	uint8_t *dst;

	if (page >= SSD1306_PAGES)
		return false;

	/* Panel not answering: wait a few updates, then try to bring it back up */
	if (!SSD1306_Online) {
		if (page == 0 && SSD1306_Backoff > 0)
			SSD1306_Backoff--;
		if (SSD1306_Backoff > 0)
			return false;

		SSD1306_SendInit();
		if (!SSD1306_Online)
			return false;
		SSD1306_ForcePages = 0xFF;	/* it came back: its RAM content is unknown */
	}

	mask = 1 << page;
	src = &SSD1306_Buffer[SSD1306_WIDTH * page];
	dst = &SSD1306_Shadow[SSD1306_WIDTH * page];

	if (SSD1306_ForcePages & mask) {
		first = 0;
		last = SSD1306_WIDTH - 1;
	} else {
		/* only the columns between the first and the last difference are sent */
		for (first = 0; first < SSD1306_WIDTH && src[first] == dst[first]; first++)
			;
		if (first == SSD1306_WIDTH)
			return false;	/* nothing changed on this page */
		for (last = SSD1306_WIDTH - 1; src[last] == dst[last]; last--)
			;
	}

	/* page and start column in one transfer, then the data (control byte 0x40 = data).
	   In page addressing mode 0x00-0x0F set the low and 0x10-0x1F the high nibble of the column. */
	cmds[0] = 0xB0 + page;
	cmds[1] = 0x00 | (first & 0x0F);
	cmds[2] = 0x10 | (first >> 4);
	if (!SSD1306_I2C_Write(0x00, cmds, sizeof(cmds)) ||
			!SSD1306_I2C_Write(0x40, src + first, last - first + 1)) {
		SSD1306_Failed();
		return false;
	}

	memcpy(dst + first, src + first, last - first + 1);
	SSD1306_ForcePages &= ~mask;
	return true;
}

void SSD1306_UpdateScreen(void) {
	uint8_t m;

	for (m = 0; m < SSD1306_PAGES; m++)
		SSD1306_UpdatePage(m);
}

void SSD1306_ToggleInvert(void) {
	uint16_t i;

	/* Toggle invert */
	SSD1306.Inverted = !SSD1306.Inverted;

	/* Do memory toggle */
	for (i = 0; i < sizeof(SSD1306_Buffer); i++) {
		SSD1306_Buffer[i] = ~SSD1306_Buffer[i];
	}
}

void SSD1306_Fill(SSD1306_COLOR_t color) {
	/* Set memory */
	memset(SSD1306_Buffer, (color == SSD1306_COLOR_BLACK) ? 0x00 : 0xFF, sizeof(SSD1306_Buffer));
}

void SSD1306_DrawPixel(int16_t x, int16_t y, SSD1306_COLOR_t color) {
	if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT || x < 0 || y < 0) {
		/* Error */
		return;
	}

	/* Check if pixels are inverted */
	if (SSD1306.Inverted) {
		color = (SSD1306_COLOR_t)!color;
	}

	/* Set color */
	if (color == SSD1306_COLOR_WHITE) {
		SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] |= 1 << (y % 8);
	} else {
		SSD1306_Buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
	}
}

void SSD1306_GotoXY(int16_t x, int16_t y) {
	/* Set write pointers */
	SSD1306.CurrentX = x;
	SSD1306.CurrentY = y;
}

char SSD1306_Putc(char ch, FontDef_t* Font, SSD1306_COLOR_t color) {
	uint32_t i, b, j;

	/* Check available space in LCD */
	if (SSD1306_WIDTH <= SSD1306.CurrentX || SSD1306_HEIGHT <= SSD1306.CurrentY) {
		/* Error */
		return 0;
	}

	/* Go through font */
	for (i = 0; i < Font->FontHeight; i++) {
		b = Font->data[((uint8_t)ch - 32) * Font->FontHeight + i];
		for (j = 0; j < Font->FontWidth; j++) {
			if ((b << j) & 0x8000) {
				SSD1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR_t) color);
			} else {
				SSD1306_DrawPixel(SSD1306.CurrentX + j, (SSD1306.CurrentY + i), (SSD1306_COLOR_t)!color);
			}
		}
	}

	/* Increase pointer */
	SSD1306.CurrentX += Font->FontWidth;

	/* Return character written */
	return ch;
}

char SSD1306_Puts(char* str, FontDef_t* Font, SSD1306_COLOR_t color) {
	/* Write characters */
	while (*str) {
		/* Write character by character */
		if (SSD1306_Putc(*str, Font, color) != *str) {
			/* Return error */
			return *str;
		}

		/* Increase string pointer */
		str++;
	}

	/* Everything OK, zero should be returned */
	return *str;
}


char SSD1306_PutcScaled(char ch, FontDef_t* Font, SSD1306_COLOR_t color, uint8_t scale) {
	uint32_t i, b, j;
	uint8_t sx, sy;
	SSD1306_COLOR_t c;

	if (scale < 1) {
		scale = 1;
	}

	/* Check available space in LCD */
	if (SSD1306_WIDTH <= SSD1306.CurrentX || SSD1306_HEIGHT <= SSD1306.CurrentY) {
		/* Error */
		return 0;
	}

	/* Go through font, every font pixel becomes a scale x scale block */
	for (i = 0; i < Font->FontHeight; i++) {
		b = Font->data[((uint8_t)ch - 32) * Font->FontHeight + i];
		for (j = 0; j < Font->FontWidth; j++) {
			c = ((b << j) & 0x8000) ? color : (SSD1306_COLOR_t)!color;
			for (sy = 0; sy < scale; sy++) {
				for (sx = 0; sx < scale; sx++) {
					SSD1306_DrawPixel(SSD1306.CurrentX + j * scale + sx, SSD1306.CurrentY + i * scale + sy, c);
				}
			}
		}
	}

	/* Increase pointer */
	SSD1306.CurrentX += Font->FontWidth * scale;

	/* Return character written */
	return ch;
}

char SSD1306_PutsScaled(char* str, FontDef_t* Font, SSD1306_COLOR_t color, uint8_t scale) {
	while (*str) {
		if (SSD1306_PutcScaled(*str, Font, color, scale) != *str) {
			/* Return error */
			return *str;
		}
		str++;
	}

	return *str;
}


void SSD1306_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, SSD1306_COLOR_t c) {
	int16_t dx, dy, sx, sy, err, e2, i, tmp;

	/* Check for overflow */
	if (x0 >= SSD1306_WIDTH) {
		x0 = SSD1306_WIDTH - 1;
	}
	if (x1 >= SSD1306_WIDTH) {
		x1 = SSD1306_WIDTH - 1;
	}
	if (y0 >= SSD1306_HEIGHT) {
		y0 = SSD1306_HEIGHT - 1;
	}
	if (y1 >= SSD1306_HEIGHT) {
		y1 = SSD1306_HEIGHT - 1;
	}

	dx = (x0 < x1) ? (x1 - x0) : (x0 - x1);
	dy = (y0 < y1) ? (y1 - y0) : (y0 - y1);
	sx = (x0 < x1) ? 1 : -1;
	sy = (y0 < y1) ? 1 : -1;
	err = ((dx > dy) ? dx : -dy) / 2;

	if (dx == 0) {
		if (y1 < y0) {
			tmp = y1;
			y1 = y0;
			y0 = tmp;
		}

		if (x1 < x0) {
			tmp = x1;
			x1 = x0;
			x0 = tmp;
		}

		/* Vertical line */
		for (i = y0; i <= y1; i++) {
			SSD1306_DrawPixel(x0, i, c);
		}

		/* Return from function */
		return;
	}

	if (dy == 0) {
		if (y1 < y0) {
			tmp = y1;
			y1 = y0;
			y0 = tmp;
		}

		if (x1 < x0) {
			tmp = x1;
			x1 = x0;
			x0 = tmp;
		}

		/* Horizontal line */
		for (i = x0; i <= x1; i++) {
			SSD1306_DrawPixel(i, y0, c);
		}

		/* Return from function */
		return;
	}

	while (1) {
		SSD1306_DrawPixel(x0, y0, c);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		e2 = err;
		if (e2 > -dx) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dy) {
			err += dx;
			y0 += sy;
		}
	}
}

void SSD1306_DrawRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, SSD1306_COLOR_t c) {
	/* Check input parameters */
	if (
		x >= SSD1306_WIDTH ||
		y >= SSD1306_HEIGHT
	) {
		/* Return error */
		return;
	}

	/* Check width and height */
	if ((x + w) >= SSD1306_WIDTH) {
		w = SSD1306_WIDTH - x;
	}
	if ((y + h) >= SSD1306_HEIGHT) {
		h = SSD1306_HEIGHT - y;
	}

	/* Draw 4 lines */
	SSD1306_DrawLine(x, y, x + w, y, c);         /* Top line */
	SSD1306_DrawLine(x, y + h, x + w, y + h, c); /* Bottom line */
	SSD1306_DrawLine(x, y, x, y + h, c);         /* Left line */
	SSD1306_DrawLine(x + w, y, x + w, y + h, c); /* Right line */
}

void SSD1306_DrawFilledRectangle(uint16_t x, uint16_t y, uint16_t w, uint16_t h, SSD1306_COLOR_t c) {
	uint8_t i;

	/* Check input parameters */
	if (
		x >= SSD1306_WIDTH ||
		y >= SSD1306_HEIGHT
	) {
		/* Return error */
		return;
	}

	/* Check width and height */
	if ((x + w) >= SSD1306_WIDTH) {
		w = SSD1306_WIDTH - x;
	}
	if ((y + h) >= SSD1306_HEIGHT) {
		h = SSD1306_HEIGHT - y;
	}

	/* Draw lines */
	for (i = 0; i <= h; i++) {
		/* Draw lines */
		SSD1306_DrawLine(x, y + i, x + w, y + i, c);
	}
}

void SSD1306_DrawTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, SSD1306_COLOR_t color) {
	/* Draw lines */
	SSD1306_DrawLine(x1, y1, x2, y2, color);
	SSD1306_DrawLine(x2, y2, x3, y3, color);
	SSD1306_DrawLine(x3, y3, x1, y1, color);
}


void SSD1306_DrawFilledTriangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t x3, uint16_t y3, SSD1306_COLOR_t color) {
	int16_t deltax = 0, deltay = 0, x = 0, y = 0, xinc1 = 0, xinc2 = 0,
	yinc1 = 0, yinc2 = 0, den = 0, num = 0, numadd = 0, numpixels = 0,
	curpixel = 0;

	deltax = ABS(x2 - x1);
	deltay = ABS(y2 - y1);
	x = x1;
	y = y1;

	if (x2 >= x1) {
		xinc1 = 1;
		xinc2 = 1;
	} else {
		xinc1 = -1;
		xinc2 = -1;
	}

	if (y2 >= y1) {
		yinc1 = 1;
		yinc2 = 1;
	} else {
		yinc1 = -1;
		yinc2 = -1;
	}

	if (deltax >= deltay){
		xinc1 = 0;
		yinc2 = 0;
		den = deltax;
		num = deltax / 2;
		numadd = deltay;
		numpixels = deltax;
	} else {
		xinc2 = 0;
		yinc1 = 0;
		den = deltay;
		num = deltay / 2;
		numadd = deltax;
		numpixels = deltay;
	}

	for (curpixel = 0; curpixel <= numpixels; curpixel++) {
		SSD1306_DrawLine(x, y, x3, y3, color);

		num += numadd;
		if (num >= den) {
			num -= den;
			x += xinc1;
			y += yinc1;
		}
		x += xinc2;
		y += yinc2;
	}
}

void SSD1306_DrawCircle(int16_t x0, int16_t y0, int16_t r, SSD1306_COLOR_t c) {
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

    SSD1306_DrawPixel(x0, y0 + r, c);
    SSD1306_DrawPixel(x0, y0 - r, c);
    SSD1306_DrawPixel(x0 + r, y0, c);
    SSD1306_DrawPixel(x0 - r, y0, c);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        SSD1306_DrawPixel(x0 + x, y0 + y, c);
        SSD1306_DrawPixel(x0 - x, y0 + y, c);
        SSD1306_DrawPixel(x0 + x, y0 - y, c);
        SSD1306_DrawPixel(x0 - x, y0 - y, c);

        SSD1306_DrawPixel(x0 + y, y0 + x, c);
        SSD1306_DrawPixel(x0 - y, y0 + x, c);
        SSD1306_DrawPixel(x0 + y, y0 - x, c);
        SSD1306_DrawPixel(x0 - y, y0 - x, c);
    }
}

void SSD1306_DrawFilledCircle(int16_t x0, int16_t y0, int16_t r, SSD1306_COLOR_t c) {
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

    SSD1306_DrawPixel(x0, y0 + r, c);
    SSD1306_DrawPixel(x0, y0 - r, c);
    SSD1306_DrawPixel(x0 + r, y0, c);
    SSD1306_DrawPixel(x0 - r, y0, c);
    SSD1306_DrawLine(x0 - r, y0, x0 + r, y0, c);

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;

        SSD1306_DrawLine(x0 - x, y0 + y, x0 + x, y0 + y, c);
        SSD1306_DrawLine(x0 + x, y0 - y, x0 - x, y0 - y, c);

        SSD1306_DrawLine(x0 + y, y0 + x, x0 - y, y0 + x, c);
        SSD1306_DrawLine(x0 + y, y0 - x, x0 - y, y0 - x, c);
    }
}

void SSD1306_ON(void) {
	SSD1306_WRITECOMMAND(0x8D);
	SSD1306_WRITECOMMAND(0x14);
	SSD1306_WRITECOMMAND(0xAF);
}
void SSD1306_OFF(void) {
	SSD1306_WRITECOMMAND(0x8D);
	SSD1306_WRITECOMMAND(0x10);
	SSD1306_WRITECOMMAND(0xAE);
}

void SSD1306_WRITECOMMAND(uint8_t command)
{
	// control byte 0x00 = command
	SSD1306_WriteCommands(&command, 1);
}
