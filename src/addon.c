/******************************************************************************
 * 
 * Copyright 2017 karawin (http://www.karawin.fr)
 *
*******************************************************************************/

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#define TAG  "Addon"

#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include "esp_sleep.h"
#include "nvs_flash.h"

#include "addon.h"
#include "main.h"
#include "eeprom.h"
#include "interface.h"
#include "ntp.h"
#include "webclient.h"
#include "webserver.h"

static void evtClearScreen();
// second before time display in stop state
#define DTIDLE  60

#define isColor (lcd_type&LCD_COLOR)
const char *stopped = "STOPPED";

char irStr[4];
xQueueHandle event_ir = NULL;
xQueueHandle event_lcd = NULL;

static uint8_t lcd_type;
static xTaskHandle  pxTaskLcd;
// list of screen
typedef  enum typeScreen {smain,svolume,sstation,snumber,stime,snull} typeScreen ;
static typeScreen stateScreen = snull;
static typeScreen defaultStateScreen = smain;
// state of the transient screen
static uint8_t mTscreen = MTNEW; // 0 dont display, 1 display full, 2 display variable part

static bool playable = true;
static uint16_t volume;
static int16_t futurNum = 0; // the number of the wanted station

static unsigned timerScreen = 0;
static unsigned timerScroll = 0;
static unsigned timerLcdOut = 0;
static unsigned timer1s = 0;

static unsigned timein = 0;
static struct tm *dt;
time_t timestamp = 0;
static bool syncTime = false;
static bool itAskTime = true; // update time with ntp if true
static bool itAskStime = false; // start the time display
static uint8_t itLcdOut = 0;
//static bool itAskSsecond = false; // start the time display
static bool state = false; // start stop on Ok key

static int16_t currentValue = 0;
static bool dvolume = true; // display volume screen
 
// custom ir code init from hardware nvs
typedef enum {KEY_UP,KEY_LEFT,KEY_OK,KEY_RIGHT,KEY_DOWN,
		KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,
		KEY_STAR,KEY_DIESE,KEY_INFO, KEY_MAX} customKey_t;
		
static uint32_t customKey[KEY_MAX][2]; 
static bool isCustomKey = false;

//backlight value
static int blv = 100;

void Screen(typeScreen st); 
void drawScreen();
static void evtScreen(typelcmd value);

struct tm* getDt() { return dt;}

void setBlv(int val)
{
	blv = val;
}

int getBlv()
{
	return blv;
}

static void ClearBuffer()
{
}

void lcd_init(uint8_t Type)
{
}

void in_welcome(const char* ip,const char*state,int y,char* Version)
{
}		

void lcd_welcome(const char* ip,const char*state)
{
}

 // ----------------------------------------------------------------------------
// call this every 1 millisecond via timer ISR
//
void (*serviceAddon)() = NULL;

IRAM_ATTR void ServiceAddon(void)
{
	timer1s++;
	timerScroll++;
	if (timer1s >=1000)
	{
		// Time compute
		timestamp++;  // time update  
		if (timerLcdOut >0) timerLcdOut--; // 
		timein++;
		if ((timestamp % (10*DTIDLE))==0){ itAskTime=true;} // synchronise with ntp every x*DTIDLE
		 
		if (((timein % DTIDLE)==0)&&(!state))
		{
			{itAskStime=true;timein = 0;} // start the time display when paused
		}
		if (timerLcdOut == 1) itLcdOut = 1; // ask to go to sleep
		if (!syncTime) itAskTime=true; // first synchro if not done

		timer1s = 0;
		// Other slow timers
		timerScreen++;
	}
}

// futurNum
void setFuturNum(int16_t new)
{
	futurNum = new;
}
int16_t getFuturNum()
{
	return futurNum;
}

// scroll each line
void scroll()
{	
}



// Change the current screen
void Screen(typeScreen st){
//printf("Screen: st: %d, stateScreen: %d, mTscreen: %d, default: %d\n",st,stateScreen,mTscreen,defaultStateScreen);
  if (stateScreen != st)
  {
	mTscreen = MTNEW;
//	wakeLcd();
  }
  else
  {
	if (mTscreen == MTNODISPLAY) mTscreen = MTREFRESH;
  }

//  printf("Screenout: st: %d, stateScreen: %d, mTscreen: %d, default: %d, timerScreen: %d \n",st,stateScreen,mTscreen,defaultStateScreen,timerScreen);

  stateScreen = st;  
  timein = 0;
  timerScreen = 0;  
  drawScreen();
//printf("Screendis: st: %d, stateScreen: %d, mTscreen: %d, default: %d\n",st,stateScreen,mTscreen,defaultStateScreen);  
//  vTaskDelay(1);
}



// draw all lines
void drawFrame()
{
}

void drawTTitle(char* ttitle)
{ 
}

// draw the number entered from IR
void drawNumber()
{
}

// draw the station screen
void drawStation()
{
}

// draw the volume screen
void drawVolume()
{
}

void drawTime()
{
}

// Display a screen on the lcd
void drawScreen()
{
}

void stopStation()
{
//	irStr[0] = 0;
	clientDisconnect("addon stop");
}
void startStation()
{
//	irStr[0] = 0;
	playStationInt(futurNum); ; 
}

void startStop()
{
	ESP_LOGD(TAG,"START/STOP State: %d",state);
	state?stopStation():startStation();
}

void stationOk()
{
	ESP_LOGD(TAG,"STATION OK");
		if (strlen(irStr) >0)
		{
			futurNum = atoi(irStr);
			playStationInt(futurNum);
		}
		else
		{
			startStop();
		}
		irStr[0] = 0;
}

void changeStation(int16_t value)
{
	currentValue = value;
	ESP_LOGD(TAG,"changeStation val: %d, futurnum: %d",value,futurNum);
	if (value > 0) futurNum++;
	if (futurNum > 254) futurNum = 0;
	else if (value < 0) futurNum--;
	if (futurNum <0) futurNum = 254;
	ESP_LOGD(TAG,"futurnum: %d",futurNum);
	//else if (value != 0) mTscreen = MTREFRESH;
}

// IR 
// a number of station in progress...
void nbStation(char nb)
{
	if (strlen(irStr)>=3) irStr[0] = 0;
	uint8_t id = strlen(irStr);
	irStr[id] = nb;
	irStr[id+1] = 0;
	evtScreen(snumber);
}

static void evtClearScreen()
{
	event_lcd_t evt;
	evt.lcmd = eclrs;	
	evt.lline = NULL;
	if (lcd_type != LCD_NONE) xQueueSend(event_lcd,&evt, 0);	
}

static void evtScreen(typelcmd value)
{
	event_lcd_t evt;
	evt.lcmd = escreen;	
	evt.lline = (char*)((uint32_t)value);
	if (lcd_type != LCD_NONE) xQueueSend(event_lcd,&evt, 0);
	
}

static void evtStation(int16_t value)
{ // value +1 or -1
	event_lcd_t evt; 
	evt.lcmd = estation;
	evt.lline = (char*)((uint32_t)value);
	if (lcd_type != LCD_NONE) xQueueSend(event_lcd,&evt, 0);			
}

// toggle main / time
static void toggletime()
{
	event_lcd_t evt;
	evt.lcmd = etoggle;	
	evt.lline = NULL;
	if (lcd_type != LCD_NONE) xQueueSend(event_lcd,&evt, 0);	
}

// compute custom IR
bool irCustom(uint32_t evtir, bool repeat)
{
	int i;
	for (i=KEY_UP;i < KEY_MAX;i++)
	{
		if ((evtir == customKey[i][0])||(evtir == customKey[i][1])) break;
	}
	if (i<KEY_MAX)
	{
		switch (i)
		{
			case KEY_UP: evtStation(+1);  break;
			case KEY_LEFT: setRelVolume(-5);  break;
			case KEY_OK: if (!repeat ) stationOk();  break;
			case KEY_RIGHT: setRelVolume(+5);  break;
			case KEY_DOWN: evtStation(-1);  break;
			case KEY_0: if (!repeat ) nbStation('0');  break;
			case KEY_1: if (!repeat ) nbStation('1');  break;
			case KEY_2: if (!repeat ) nbStation('2');  break;
			case KEY_3: if (!repeat ) nbStation('3');  break;
			case KEY_4: if (!repeat ) nbStation('4');  break;
			case KEY_5: if (!repeat ) nbStation('5');  break;
			case KEY_6: if (!repeat ) nbStation('6');  break;
			case KEY_7: if (!repeat ) nbStation('7');  break;
			case KEY_8: if (!repeat ) nbStation('8');  break;
			case KEY_9: if (!repeat ) nbStation('9');  break;
			case KEY_STAR: if (!repeat ) playStationInt(futurNum);  break;
			case KEY_DIESE: if (!repeat )  stopStation();  break;
			case KEY_INFO: if (!repeat ) toggletime();  break;
			default: ;
		}
		ESP_LOGV(TAG,"irCustom success, evtir %x, i: %d",evtir,i);
		return true;
	}
	return false;
}

 //-----------------------
 // Compute the ir code
 //----------------------
 
void irLoop()
{
// IR
event_ir_t evt;
	while (xQueueReceive(event_ir, &evt, 0))
	{
		wakeLcd();
		uint32_t evtir = ((evt.addr)<<8)|(evt.cmd&0xFF);
		ESP_LOGI(TAG,"IR event: Channel: %x, ADDR: %x, CMD: %x = %X, REPEAT: %d",evt.channel,evt.addr,evt.cmd, evtir,evt.repeat_flag );
		
		if (isCustomKey){
			if (irCustom(evtir,evt.repeat_flag)) continue;
		}
		else{ // no predefined keys
		switch(evtir)
		{
		case 0xDF2047:
		case 0xDF2002:
		case 0xFF0046: 
		case 0xF70812:  /*(" UP");*/  evtStation(+1);  
		break;
		case 0xDF2049:
		case 0xDF2041:
		case 0xFF0044:
		case 0xF70842:
		case 0xF70815: /*(" LEFT");*/  setRelVolume(-5);  
		break;
		case 0xDF204A:
		case 0xFF0040:
		case 0xF7081E: /*(" OK");*/ if (!evt.repeat_flag ) stationOk();
		break;
		case 0xDF204B:
		case 0xDF2003:
		case 0xFF0043:
		case 0xF70841:
		case 0xF70814: /*(" RIGHT");*/ setRelVolume(+5);
		break; 
		case 0xDF204D:
		case 0xDF2009:
		case 0xFF0015:
		case 0xF70813: /*(" DOWN");*/ evtStation(-1);
		break;
		case 0xDF2000:
		case 0xFF0016:
		case 0xF70801: /*(" 1");*/ if (!evt.repeat_flag ) nbStation('1');   
		break;
		case 0xDF2010:
		case 0xFF0019:
		case 0xF70802: /*(" 2");*/ if (!evt.repeat_flag ) nbStation('2');   
		break;
		case 0xDF2011:
		case 0xFF000D:
		case 0xF70803: /*(" 3");*/ if (!evt.repeat_flag ) nbStation('3');   
		break;
		case 0xDF2013:
		case 0xFF000C:
		case 0xF70804: /*(" 4");*/ if (!evt.repeat_flag ) nbStation('4');   
		break;
		case 0xDF2014:
		case 0xFF0018:
		case 0xF70805: /*(" 5");*/ if (!evt.repeat_flag ) nbStation('5');   
		break;
		case 0xDF2015:
		case 0xFF005E:
		case 0xF70806: /*(" 6");*/ if (!evt.repeat_flag ) nbStation('6');   
		break;
		case 0xDF2017:
		case 0xFF0008:
		case 0xF70807: /*(" 7");*/ if (!evt.repeat_flag ) nbStation('7');   
		break;
		case 0xDF2018:
		case 0xFF001C:
		case 0xF70808: /*(" 8");*/ if (!evt.repeat_flag ) nbStation('8');   
		break;
		case 0xDF2019:
		case 0xFF005A:
		case 0xF70809: /*(" 9");*/ if (!evt.repeat_flag ) nbStation('9');   
		break;
		case 0xDF2045:
		case 0xFF0042:
		case 0xF70817: /*(" *");*/   if (!evt.repeat_flag ) playStationInt(futurNum);
		break;
		case 0xDF201B:
		case 0xFF0052:
		case 0xF70800: /*(" 0");*/ if (!evt.repeat_flag ) nbStation('0');
		break;
		case 0xDF205B:
		case 0xFF004A:
		case 0xF7081D: /*(" #");*/ if (!evt.repeat_flag )  stopStation();
		break;
		case 0xDF2007: /*(" Info")*/ if (!evt.repeat_flag ) toggletime();
		break;
		default:;
		/*SERIALX.println(F(" other button   "));*/
		}// End Case
		}
	}
}

// custom ir code init from hardware nvs partition
#define hardware "hardware"
void customKeyInit()
{ /*
	customKey_t index;
	nvs_handle handle;
	const char *klab[] = {"K_UP","K_LEFT","K_OK","K_RIGHT","K_DOWN","K_0","K_1","K_2","K_3","K_4","K_5","K_6","K_7","K_8","K_9","K_STAR","K_DIESE","K_INFO"};
	
	memset(&customKey,0,sizeof(uint32_t)*2*KEY_MAX); // clear custom
	if (open_partition(hardware, "custom_ir_space",NVS_READONLY,&handle)!= ESP_OK) return;
		
	for (index = KEY_UP; index < KEY_MAX;index++)
	{
		// get the key in the nvs
		isCustomKey |= gpio_get_ir_key(handle,klab[index],(uint32_t*)&(customKey[index][0]),(uint32_t*)&(customKey[index][1]));
		ESP_LOGV(TAG," isCustomKey is %d for %d",isCustomKey,index);
		taskYIELD();
	}
	close_partition(handle,hardware);	*/
}


static uint8_t divide = 0;
// indirect call to service
IRAM_ATTR void multiService()  // every 1ms
{
//	ServiceAddon();
	if (divide++ == 10) // only every 10ms
	{
		divide = 0;
	}
}

// LCD display task
void task_lcd(void *pvParams)
{
}
 
// Main task of addon
extern void rmt_nec_rx_task();

void task_addon(void *pvParams)
{
}

// force a new dt ntp fetch
void addonDt() { itAskTime = true; }

// parse the karadio received line and do the job
void addonParse(const char *fmt, ...)
{
	event_lcd_t evt;
	char *line = NULL;
//	char* lfmt;
	int rlen;
	line = (char *)kmalloc(1024);
	if (line == NULL) return;
	line[0] = 0;
	strcpy(line,"ok\n");

	va_list ap;
	va_start(ap, fmt);
	rlen = vsprintf(line,fmt, ap);
	va_end(ap);
	line = realloc(line,rlen+1);
	if (line == NULL) return;	
	ESP_LOGV(TAG,"LINE: %s",line);
	evt.lcmd = -1;
	char* ici;
 
 ////// Meta title  ##CLI.META#: 
	if ((ici=strstr(line,"META#: ")) != NULL)
	{
		evt.lcmd = lmeta;
		evt.lline = kmalloc(strlen(ici)+1);
		strcpy(evt.lline,ici);
   } else 
 ////// ICY4 Description  ##CLI.ICY4#:
	if ((ici=strstr(line,"ICY4#: ")) != NULL)
	{
		evt.lcmd = licy4;
		evt.lline = kmalloc(strlen(ici)+1);
		strcpy(evt.lline,ici);
	} else 
 ////// ICY0 station name   ##CLI.ICY0#:
	if ((ici=strstr(line,"ICY0#: ")) != NULL)
	{
		evt.lcmd = licy0;
		evt.lline = kmalloc(strlen(ici)+1);
		strcpy(evt.lline,ici);
	} else
 ////// STOPPED   ##CLI.STOPPED#
	if (((ici=strstr(line,"STOPPED")) != NULL)&&(strstr(line,"C_HDER") == NULL)&&(strstr(line,"C_PLIST") == NULL))
	{
		state = false;	  
 		evt.lcmd = lstop;
		evt.lline = NULL;
	}
	else
 //////Nameset   ##CLI.NAMESET#:
	if ((ici=strstr(line,"MESET#: ")) != NULL)
	{
		evt.lcmd = lnameset;
		evt.lline = kmalloc(strlen(ici)+1);
		strcpy(evt.lline,ici);
	} else
 //////Playing   ##CLI.PLAYING#
	if ((ici=strstr(line,"YING#")) != NULL)
	{
		state = true;
		itAskStime = false;
 		evt.lcmd = lplay;
		evt.lline = NULL;
	} else
	//////Volume   ##CLI.VOL#:
	if ((ici=strstr(line,"VOL#:")) != NULL)
	{
		if (*(ici+6) != 'x') // ignore help display.
		{
			volume = atoi(ici+6);
 			evt.lcmd = lvol;
			evt.lline = NULL;//atoi(ici+6);
		}
	} else
	// Volume offset   ##CLI.OVOLSET#:
	if ((ici=strstr(line,"OVOLSET#:")) != NULL)
	{
		evt.lcmd = lovol;
		evt.lline = NULL;
	}
	if (evt.lcmd != -1 && lcd_type !=LCD_NONE) xQueueSend(event_lcd,&evt, 0);
	free (line);
}
