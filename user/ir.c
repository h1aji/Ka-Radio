/*
 * Infrared remote control, NEC protocol.
 *
 * The receiver module (idle high, low while the 38 kHz carrier is received) is wired to
 * GPIO2. Every edge of the pin raises an interrupt: the interrupt handler times the
 * edges and decodes the frame, then sends the key code to a queue. The task of this file
 * executes the keys with the commands of the UART interface.
 *
 * The key codes are the ones of the IRremote library (ir.arduino): the 32 bits are
 * shifted in as they arrive, the first bit received being the most significant, so a
 * key sent as address 0x00 / command 0x45 reads 0xFFA25D. A NEC repeat frame (key held)
 * is reported as 0xFFFFFFFF.
 */

#include "esp_common.h"
#include "esp8266/eagle_soc.h"
#include "esp8266/pin_mux_register.h"
#include "esp_system.h"
#include "gpio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "ir.h"
#include "eeprom.h"
#include "interface.h"
#include "vs1053.h"
#include "webclient.h"
#include "webserver.h"

/* ------------------------------------------------------------------------- */
/*  Keys of the remote (codes of ir.arduino)                                 */
/* ------------------------------------------------------------------------- */

#define IR_KEY_REPEAT   0xFFFFFFFF

#define IR_KEY_1        0xFFA25D
#define IR_KEY_2        0xFF629D
#define IR_KEY_3        0xFFE21D
#define IR_KEY_4        0xFF22DD
#define IR_KEY_5        0xFF02FD
#define IR_KEY_6        0xFFC23D
#define IR_KEY_7        0xFFE01F
#define IR_KEY_8        0xFFA857
#define IR_KEY_9        0xFF906F
#define IR_KEY_0        0xFF9867
#define IR_KEY_PREV     0xFF10EF    /* |<< */
#define IR_KEY_NEXT     0xFF5AA5    /* >>| */
#define IR_KEY_MUTE     0xFF38C7
#define IR_KEY_MINUS    0xFF4AB5    /* -   */
#define IR_KEY_PLUS     0xFF18E7    /* +   */
#define IR_KEY_PLAY     0xFF6897
#define IR_KEY_STOP     0xFFB04F

/* ------------------------------------------------------------------------- */
/*  Receiver                                                                 */
/* ------------------------------------------------------------------------- */

#define IR_PIN_MASK     (1 << IR_PIN)

#ifndef GPIO_PIN_ADDR
#define GPIO_PIN_ADDR(i)    (0x28 + 4 * (i))    /* offset of the GPIO_PINx register */
#endif
#define IR_INT_TYPE_MASK    0x00000380          /* interrupt type field of GPIO_PINx */
#define IR_INT_ANYEDGE      (3 << 7)

#ifndef ETS_GPIO_INUM
#define ETS_GPIO_INUM       4
#endif

/* free running microsecond counter, the one of system_get_time() but readable from the handler */
#define IR_NOW()            READ_PERI_REG(0x3FF20C00)

/* NEC timings in microseconds: [min, max] of every element */
#define IN_RANGE(v, lo, hi) ((v) >= (lo) && (v) <= (hi))
#define NEC_LEAD_MARK_MIN   7500
#define NEC_LEAD_MARK_MAX   10500
#define NEC_LEAD_SPACE_MIN  3500
#define NEC_LEAD_SPACE_MAX  5500
#define NEC_REPEAT_SPACE_MIN 1700
#define NEC_REPEAT_SPACE_MAX 2800
#define NEC_MARK_MIN        300
#define NEC_MARK_MAX        900
#define NEC_SPACE0_MIN      300
#define NEC_SPACE0_MAX      900
#define NEC_SPACE1_MIN      1200
#define NEC_SPACE1_MAX      2200

enum irState {
	IR_IDLE,
	IR_LEAD_MARK,       /* the 9 ms mark is running                    */
	IR_LEAD_SPACE,      /* the 4.5 ms (frame) or 2.25 ms (repeat) space */
	IR_BIT_MARK,        /* the 560 us mark that starts a bit            */
	IR_BIT_SPACE,       /* the space that tells 0 or 1                  */
	IR_STOP_MARK,       /* the mark that ends the frame                 */
	IR_REPEAT_MARK      /* the mark that ends a repeat frame            */
};

static xQueueHandle irQueue;
static volatile uint32_t irLast;
static volatile uint32_t irValue;
static volatile uint8_t irBits;
static volatile uint8_t irState = IR_IDLE;

/* send a decoded key to the task. The command byte is sent twice (inverted): drop noise */
static void irPublish(uint32_t key)
{
	portBASE_TYPE woken = pdFALSE;
	if (key != IR_KEY_REPEAT && ((((key >> 8) ^ key) & 0xFF) != 0xFF))
		return;
	xQueueSendFromISR(irQueue, &key, &woken);
	portEND_SWITCHING_ISR(woken);
}

/* interrupt handler of the GPIO: any edge of the receiver output */
static void ir_isr(void *arg)
{
	uint32_t status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	uint32_t now, dt;
	uint8_t bit;

	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, status);
	if (!(status & IR_PIN_MASK))
		return;

	now = IR_NOW();
	dt = now - irLast;		// length of the mark or space that just ended
	irLast = now;

	if (!(GPIO_REG_READ(GPIO_IN_ADDRESS) & IR_PIN_MASK))
	{
		/* falling edge: a space ended, a mark starts */
		switch (irState)
		{
		case IR_LEAD_SPACE:
			if (IN_RANGE(dt, NEC_LEAD_SPACE_MIN, NEC_LEAD_SPACE_MAX))
			{
				irValue = 0;
				irBits = 0;
				irState = IR_BIT_MARK;
			}
			else if (IN_RANGE(dt, NEC_REPEAT_SPACE_MIN, NEC_REPEAT_SPACE_MAX))
				irState = IR_REPEAT_MARK;
			else
				irState = IR_LEAD_MARK;
			break;
		case IR_BIT_SPACE:
			if (IN_RANGE(dt, NEC_SPACE0_MIN, NEC_SPACE0_MAX))
				bit = 0;
			else if (IN_RANGE(dt, NEC_SPACE1_MIN, NEC_SPACE1_MAX))
				bit = 1;
			else
			{
				irState = IR_LEAD_MARK;
				break;
			}
			irValue = (irValue << 1) | bit;
			irState = (++irBits == 32) ? IR_STOP_MARK : IR_BIT_MARK;
			break;
		default:			// idle or lost: this edge may start a frame
			irState = IR_LEAD_MARK;
			break;
		}
	}
	else
	{
		/* rising edge: a mark ended */
		switch (irState)
		{
		case IR_LEAD_MARK:
			irState = IN_RANGE(dt, NEC_LEAD_MARK_MIN, NEC_LEAD_MARK_MAX) ? IR_LEAD_SPACE : IR_IDLE;
			break;
		case IR_BIT_MARK:
			irState = IN_RANGE(dt, NEC_MARK_MIN, NEC_MARK_MAX) ? IR_BIT_SPACE : IR_IDLE;
			break;
		case IR_STOP_MARK:
			irPublish(irValue);
			irState = IR_IDLE;
			break;
		case IR_REPEAT_MARK:
			irPublish(IR_KEY_REPEAT);
			irState = IR_IDLE;
			break;
		default:
			irState = IR_IDLE;
			break;
		}
	}
}

/* ------------------------------------------------------------------------- */
/*  Keys -> commands                                                         */
/* ------------------------------------------------------------------------- */

#define IR_DIGITS_MAX       3       /* a station number is 0..254                 */
#define IR_DIGITS_WAIT_MS   1500    /* time after the last digit before playing   */

static bool irMuted = false;
static char irDigits[IR_DIGITS_MAX + 1];
static uint8_t irNbDigits = 0;
static uint32_t irDigitTime = 0;

static uint32_t irMillis(void)
{
	return system_get_time() / 1000;
}

/* execute a command of the UART interface */
static void irCommand(const char *cmd)
{
	checkCommand(strlen(cmd), (char *)cmd);
}

static void irUnmute(void)
{
	char cmd[24];
	if (!irMuted)
		return;
	irMuted = false;
	snprintf(cmd, sizeof(cmd), "cli.vol(\"%d\")", clientIvol);
	irCommand(cmd);
}

/* play the station typed with the digits, if it exists */
static void irPlayDigits(void)
{
	struct shoutcast_info *si;
	char cmd[24];
	int station;

	if (irNbDigits == 0)
		return;
	irDigits[irNbDigits] = 0;
	irNbDigits = 0;
	station = atoi(irDigits);
	if (station > 254)
		return;
	si = getStation(station);
	if (si == NULL)
		return;
	if (si->port != 0)
	{
		snprintf(cmd, sizeof(cmd), "cli.play(\"%d\")", station);
		irCommand(cmd);
	}
	free(si);
}

static void irDigit(char digit)
{
	irDigits[irNbDigits++] = digit;
	irDigitTime = irMillis();
	if (irNbDigits == IR_DIGITS_MAX)
		irPlayDigits();
}

/* key: the code of the key, repeat: it is the key held down */
static void irKey(uint32_t key, bool repeat)
{
	if (repeat && key != IR_KEY_PLUS && key != IR_KEY_MINUS)
		return;		// only the volume keys repeat

	switch (key)
	{
	case IR_KEY_0: irDigit('0'); return;
	case IR_KEY_1: irDigit('1'); return;
	case IR_KEY_2: irDigit('2'); return;
	case IR_KEY_3: irDigit('3'); return;
	case IR_KEY_4: irDigit('4'); return;
	case IR_KEY_5: irDigit('5'); return;
	case IR_KEY_6: irDigit('6'); return;
	case IR_KEY_7: irDigit('7'); return;
	case IR_KEY_8: irDigit('8'); return;
	case IR_KEY_9: irDigit('9'); return;
	}

	irNbDigits = 0;		// any other key cancels a station number being typed

	switch (key)
	{
	case IR_KEY_PREV:
		irCommand("cli.previous");
		break;
	case IR_KEY_NEXT:
		irCommand("cli.next");
		break;
	case IR_KEY_PLAY:
		if (!getState())	// already playing: nothing to do
			irCommand("cli.start");
		break;
	case IR_KEY_STOP:
		irCommand("cli.stop");
		break;
	case IR_KEY_PLUS:
		irMuted = false;	// the volume commands set the volume again
		irCommand("cli.vol+");
		break;
	case IR_KEY_MINUS:
		irMuted = false;
		irCommand("cli.vol-");
		break;
	case IR_KEY_MUTE:
		if (irMuted)
			irUnmute();
		else
		{
			irMuted = true;
			VS1053_SetVolume(0);
		}
		break;
	default:
		kprintf(PSTR("##IR.KEY#: unknown %x%c"), key, 0x0d);
		break;
	}
}

static void irTask(void *pvParams)
{
	uint32_t key;
	uint32_t last = 0;

	for (;;)
	{
		if (xQueueReceive(irQueue, &key, 100 / portTICK_RATE_MS) == pdTRUE)
		{
			if (key == IR_KEY_REPEAT)
				irKey(last, true);
			else
			{
				last = key;
				irKey(key, false);
			}
		}
		else if ((irNbDigits != 0) && ((irMillis() - irDigitTime) >= IR_DIGITS_WAIT_MS))
			irPlayDigits();
	}
}

/* ------------------------------------------------------------------------- */
/*  Initialisation                                                           */
/* ------------------------------------------------------------------------- */

#define IR_QUEUE_SIZE   8

void ICACHE_FLASH_ATTR irInit(void)
{
	irQueue = xQueueCreate(IR_QUEUE_SIZE, sizeof(uint32_t));
	if (irQueue == NULL)
	{
		kprintf(PSTR("IR: no memory%c"), 0x0d);
		return;
	}
	xTaskCreate(irTask, "ir", 320, NULL, 3, NULL);

	/* GPIO2 as an input with the pull-up, interrupt on both edges */
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);
	GPIO_REG_WRITE(GPIO_ENABLE_W1TC_ADDRESS, IR_PIN_MASK);
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, IR_PIN_MASK);
	GPIO_REG_WRITE(GPIO_PIN_ADDR(IR_PIN),
		(GPIO_REG_READ(GPIO_PIN_ADDR(IR_PIN)) & ~IR_INT_TYPE_MASK) | IR_INT_ANYEDGE);

	irLast = IR_NOW();
	_xt_isr_attach(ETS_GPIO_INUM, ir_isr, NULL);
	_xt_isr_unmask(1 << ETS_GPIO_INUM);
}
