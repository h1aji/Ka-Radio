/******************************************************************************
 *
 * Copyright 2017 karawin (http://www.karawin.fr)
 *
*******************************************************************************/

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#define TAG "Addon"

#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_sleep.h"

#include "addon.h"
#include "flash.h"
#include "main.h"
#include "eeprom.h"
#include "interface.h"
#include "ntp.h"
#include "webclient.h"
#include "webserver.h"

static void evtClearScreen();
// second before time display in stop state
#define DTIDLE  60

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

struct tm* getDt()
{
	return dt;
}

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
	if (lcd_type == LCD_NONE) return;
}

void lcd_init(uint8_t Type)
{	
	lcd_type = Type;

	if (lcd_type == LCD_NONE) 
	{
		return;
	} 
	else
	{
		//lcd_init
	}
	vTaskDelay(1);
}

void in_welcome(const char* ip,const char*state,int y,char* Version)
{
	if (lcd_type == LCD_NONE) return;
}

void lcd_welcome(const char* ip,const char*state)
{
	char Version[20];
	sprintf(Version,"Version %s R%s\n",RELEASE,REVISION);
	if (lcd_type == LCD_NONE) return;
	if ((strlen(ip)==0)&&(strlen(state)==0)) ClearBuffer();
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
		if (timerLcdOut >0) timerLcdOut--;
		timein++;
		if ((timestamp % (10*DTIDLE))==0) {
			itAskTime=true;
		} // synchronise with ntp every x*DTIDLE

		if (((timein % DTIDLE)==0)&&(!state)) {
			{ itAskStime=true; timein = 0; } // start the time display when paused
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
void Screen(typeScreen st)
{
//printf("Screen: st: %d, stateScreen: %d, mTscreen: %d, default: %d\n",st,stateScreen,mTscreen,defaultStateScreen);
	if (stateScreen != st)
	{
		mTscreen = MTNEW;
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
//	vTaskDelay(1);
}



// draw all lines
void drawFrame()
{
	dt=localtime(&timestamp);
	if (lcd_type == LCD_NONE) return;
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
  char sNum[7] ; 
  char* ddot;
  char* ptl ;
  struct shoutcast_info* si;

 //ClearBuffer();
	
  do {
	si = getStation(futurNum);
	sprintf(sNum,"%d",futurNum);
	ddot = si->name;    
	ptl = ddot;
	while ( *ptl == 0x20){ddot++;ptl++;}
	if (strlen(ddot)==0) // don't start an undefined station
	{
		playable = false; 
		free(si);
		if (currentValue < 0) {
			futurNum--; 
			if (futurNum <0) futurNum = 254;
		}
		else {
			futurNum++;
			if (futurNum > 254) futurNum = 0;
		}
	}	
	else 
		playable = true;                      
  } while (playable == false); 
	
  //drawTTitle(ststr); 
//printf ("drawStation: %s\n",sNum  );
  if (lcd_type == LCD_NONE) return;
  free (si);
}

// draw the volume screen
void drawVolume()
{
	if (lcd_type == LCD_NONE) return;
}

void drawTime()
{
	dt=localtime(&timestamp);
	if (lcd_type == LCD_NONE) return;
}

// Display a screen on the lcd
void drawScreen()
{
//  if (lcd_type == LCD_NONE) return;
//  ESP_LOGD(TAG,"stateScreen: %d,defaultStateScreen: %d, mTscreen: %d, itLcdOut: %d",stateScreen,defaultStateScreen,mTscreen,itLcdOut);
  if ((mTscreen != MTNODISPLAY)&&(!itLcdOut))
  {
	switch (stateScreen)
	{
    case smain:  // 
     drawFrame();
      break;
    case svolume:
      drawVolume();
      break;
    case sstation:
      drawStation(); 
      break; 
    case stime:
      drawTime(); 
      break;     
    case snumber:   
      drawNumber();     
      break;
    default: 
	  Screen(defaultStateScreen); 
//	  drawFrame();	  
	} 
//	if (mTscreen == MTREFRESH)
		mTscreen = MTNODISPLAY;
  }   
}

void stopStation()
{
//	irStr[0] = 0;
	clientDisconnect("addon stop");
}
void startStation()
{
//	irStr[0] = 0;
	playStationInt(futurNum);
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
		uint32_t evtir = ((evt.addr)<<8)|(evt.cmd&0xFF);
		ESP_LOGI(TAG,"IR event: Channel: %x, ADDR: %x, CMD: %x = %X, REPEAT: %d",evt.channel,evt.addr,evt.cmd, evtir,evt.repeat_flag );

		if (isCustomKey) {
			if (irCustom(evtir,evt.repeat_flag)) continue;
		} else { // no predefined keys
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
			case 0xF70801: /*(" 1");*/ if (!evt.repeat_flag) nbStation('1');
			break;
			case 0xDF2010:
			case 0xFF0019:
			case 0xF70802: /*(" 2");*/ if (!evt.repeat_flag) nbStation('2');
			break;
			case 0xDF2011:
			case 0xFF000D:
			case 0xF70803: /*(" 3");*/ if (!evt.repeat_flag) nbStation('3');
			break;
			case 0xDF2013:
			case 0xFF000C:
			case 0xF70804: /*(" 4");*/ if (!evt.repeat_flag) nbStation('4');
			break;
			case 0xDF2014:
			case 0xFF0018:
			case 0xF70805: /*(" 5");*/ if (!evt.repeat_flag) nbStation('5');
			break;
			case 0xDF2015:
			case 0xFF005E:
			case 0xF70806: /*(" 6");*/ if (!evt.repeat_flag) nbStation('6');
			break;
			case 0xDF2017:
			case 0xFF0008:
			case 0xF70807: /*(" 7");*/ if (!evt.repeat_flag) nbStation('7');
			break;
			case 0xDF2018:
			case 0xFF001C:
			case 0xF70808: /*(" 8");*/ if (!evt.repeat_flag) nbStation('8');
			break;
			case 0xDF2019:
			case 0xFF005A:
			case 0xF70809: /*(" 9");*/ if (!evt.repeat_flag) nbStation('9');
			break;
			case 0xDF2045:
			case 0xFF0042:
			case 0xF70817: /*(" *");*/   if (!evt.repeat_flag) playStationInt(futurNum);
			break;
			case 0xDF201B:
			case 0xFF0052:
			case 0xF70800: /*(" 0");*/ if (!evt.repeat_flag) nbStation('0');
			break;
			case 0xDF205B:
			case 0xFF004A:
			case 0xF7081D: /*(" #");*/ if (!evt.repeat_flag)  stopStation();
			break;
			case 0xDF2007: /*(" Info")*/ if (!evt.repeat_flag) toggletime();
			break;
			default:;
			}
		}
	}
}

// custom ir code init from hardware nvs partition
#define hardware "hardware"
void customKeyInit()
{
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

	// Closing partition
	close_partition(handle,hardware);
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
	event_lcd_t evt ; // lcd event	
	event_lcd_t evt1 ; // lcd event	
	ESP_LOGD(TAG, "task_lcd Started, LCD Type %d",lcd_type);
	defaultStateScreen = (g_device->options32&T_TOGGLETIME)? stime:smain;
	if (lcd_type != LCD_NONE)  drawFrame();

	while (1)
	{	
		if (timerScroll >= 500) //500 ms
		{
			if (lcd_type != LCD_NONE) 
			{
				if (stateScreen == smain)
				{
					scroll(); 
				}
				if ((stateScreen == stime)||(stateScreen == smain)) {mTscreen = MTREFRESH; } // display time
	
				drawScreen();
			}
			timerScroll = 0;
		}  		
		if (event_lcd != NULL)
		while (xQueueReceive(event_lcd, &evt, 0))
		{ 
//			if (lcd_type == LCD_NONE) continue;
			if (evt.lcmd != lmeta)
				ESP_LOGV(TAG,"event_lcd: %x, %d, mTscreen: %d",(int)evt.lcmd,(int)evt.lline,mTscreen);
			else
				ESP_LOGV(TAG,"event_lcd: %x  %s, mTscreen: %d",(int)evt.lcmd,evt.lline,mTscreen); 
			switch(evt.lcmd)
			{
				case lmeta:
					Screen(smain);
					break;
				case licy4:
					break;
				case licy0: 					
					break;
				case lstop:
					Screen(smain);
					break;
				case lnameset:
					Screen(smain); 
					break;
				case lplay:
					break;
				case lvol:
					// ignore it if the next is a lvol
					if(xQueuePeek(event_lcd, &evt1, 0))
						if (evt1.lcmd == lvol) break;
					if (dvolume)
					{	Screen(svolume); 
					}
					dvolume = true;														
					break;
				case lovol:
					dvolume = false; // don't show volume on start station
					break;
				case estation:
					if(xQueuePeek(event_lcd, &evt1, 0))
						if (evt1.lcmd == estation) {evt.lline = NULL;break;}
					ESP_LOGD(TAG,"estation val: %d",(uint32_t)evt.lline);
					changeStation((uint32_t)evt.lline);	
					Screen(sstation);
					evt.lline = NULL;	// just a number			
					break;
				case eclrs:
					break;
				case escreen: 
					Screen((uint32_t)evt.lline);
					evt.lline = NULL;	// just a number Don't free					
					break;
				case etoggle:
					defaultStateScreen = (stateScreen==smain)?stime:smain;
					(stateScreen==smain)?Screen(stime):Screen(smain);
					g_device->options32 = (defaultStateScreen== smain)?g_device->options32&NT_TOGGLETIME:g_device->options32|T_TOGGLETIME; 
//					saveDeviceSettings(g_device);
					break;
				default:;
			}
			if (evt.lline != NULL) free(evt.lline);
			vTaskDelay(4);  			
		 }
		 if ((event_lcd)&&(!uxQueueMessagesWaiting(event_lcd))) vTaskDelay(10);
		vTaskDelay(4);	
	}
	vTaskDelete( NULL ); 	
}

//------------------- 
// Main task of addon
//------------------- 
void task_addon(void *pvParams)
{
	customKeyInit();
	
	serviceAddon = &multiService;		; // connect the 1ms interruption
	futurNum = getCurrentStation();
	
	//ir
	// queue for events of the IR nec rx
	event_ir = xQueueCreate(5, sizeof(event_ir_t));
	ESP_LOGD(TAG,"event_ir: %x",(int)event_ir);

	if (g_device->lcd_type!=LCD_NONE)
	{
		// queue for events of the lcd
		event_lcd = xQueueCreate(10, sizeof(event_lcd_t));
		ESP_LOGD(TAG,"event_lcd: %x",(int)event_lcd);	

		xTaskCreate(task_lcd, "task_lcd", 2300, NULL, PRIO_LCD, &pxTaskLcd);
		ESP_LOGI(TAG, "%s task: %x","task_lcd",(unsigned int)pxTaskLcd);

		//getTaskLcd(&pxTaskLcd); // give the handle to xpt
	}
	
	while (1)
	{
		irLoop();  // compute the ir		
		if (itAskTime) // time to ntp. Don't do that in interrupt.
		{			
			if (ntp_get_time(&dt) )
			{	
				applyTZ(dt);
				timestamp = mktime(dt); 
				syncTime = true;				
			} 
			itAskTime = false;
		}	
		
		if (timerScreen >= 3) //  sec timeout transient screen
		{
//			if ((stateScreen != smain)&&(stateScreen != stime)&&(stateScreen != snull))
//printf("timerScreen: %d, stateScreen: %d, defaultStateScreen: %d\n",timerScreen,stateScreen,defaultStateScreen);	
			timerScreen = 0;				
			if ((stateScreen != defaultStateScreen)&&(stateScreen != snull))
			{
				// Play the changed station on return to main screen
				// if a number is entered, play it.
				if (strlen(irStr) >0){
					futurNum = atoi (irStr);
					if (futurNum>254) futurNum = 0;
					playable = true;
					// clear the number       
					irStr[0] = 0;
				}
				if (!itAskStime)
				{
					if ((defaultStateScreen == stime) && (stateScreen != smain))evtScreen(smain);
					else
					if ((defaultStateScreen == stime) && (stateScreen == smain))evtScreen(stime);	
					else 
					if 	(stateScreen != defaultStateScreen)
					evtScreen(defaultStateScreen); //Back to the old screen
				}
			}
			if (itAskStime&&(stateScreen != stime)) // time start the time display. Don't do that in interrupt.  
				evtScreen(stime);			
		}
		vTaskDelay(10);
	}	
	vTaskDelete( NULL ); 
}

// force a new dt ntp fetch
void addonDt() { 
	itAskTime = true;
}

// parse the karadio received line and do the job
void addonParse(const char *fmt, ...) {
	event_lcd_t evt;
	char *line = NULL;
//	char* lfmt;
	int rlen;
	line = (char *)malloc(1024);
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
		evt.lline = malloc(strlen(ici)+1);
		strcpy(evt.lline,ici);
   } else
 ////// ICY4 Description  ##CLI.ICY4#:
	if ((ici=strstr(line,"ICY4#: ")) != NULL)
	{
		evt.lcmd = licy4;
		evt.lline = malloc(strlen(ici)+1);
		strcpy(evt.lline,ici);
	} else
 ////// ICY0 station name   ##CLI.ICY0#:
	if ((ici=strstr(line,"ICY0#: ")) != NULL)
	{
		evt.lcmd = licy0;
		evt.lline = malloc(strlen(ici)+1);
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
		evt.lline = malloc(strlen(ici)+1);
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
  //////Volume offset   ##CLI.OVOLSET#:
	if ((ici=strstr(line,"OVOLSET#:")) != NULL)
	{
		evt.lcmd = lovol;
		evt.lline = NULL;
	}

	if (evt.lcmd != -1 && lcd_type !=LCD_NONE)
	{
		xQueueSend(event_lcd,&evt, 0);
	}
	free (line);
}
