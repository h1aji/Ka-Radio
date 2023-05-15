/******************************************************************************
  KaRadio 32
  A WiFi webradio player

  Copyright (C) 2017  KaraWin

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*******************************************************************************/

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#define TAG "Main"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <nvs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
//#include "esp_heap_trace.h"
#include "nvs_flash.h"
#include "driver/i2s.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/hw_timer.h"

#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "mdns.h"

#include "audio_player.h"

#include "buffer.h"
#include "spiram.h"

#include "app_main.h"

#include "addon.h"
#include "eeprom.h"

#include "gpio.h"
#include "servers.h"
#include "webclient.h"
#include "webserver.h"
#include "interface.h"
#include "vs1053.h"


/* The event group allows multiple bits for each event*/
//   are we connected  to the AP with an IP? */
const int CONNECTED_BIT = 0x00000001;
//
const int CONNECTED_AP  = 0x00000010;

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "esp_log.h"

//Priorities of the reader and the decoder thread. bigger number = higher prio
#define PRIO_READER configMAX_PRIORITIES -3
#define PRIO_MQTT configMAX_PRIORITIES - 3
#define PRIO_CONNECT configMAX_PRIORITIES -1
#define striWATERMARK  "watermark: %d  heap: %d"

void start_network();
void autoPlay();

static bool wifiInitDone = false;
static EventGroupHandle_t wifi_event_group ;
xQueueHandle event_queue;

//xSemaphoreHandle print_mux;
static uint16_t FlashOn = 5,FlashOff = 5;

bool logTel; // true = log also on telnet
player_t *player_config;
uint8_t clientIvol = 0;
//ip
static char localIp[20];
// 4MB sram?
static bool bigRam = false;
// timeout to save volume in flash
//static uint32_t ctimeVol = 0;
static uint32_t ctimeMs = 0;	
static bool divide = false;

IRAM_ATTR char* getIp() {return (localIp);}
IRAM_ATTR uint8_t getIvol() {return clientIvol;}
IRAM_ATTR void setIvol( uint8_t vol) {clientIvol = vol;}; //ctimeVol = 0;}
 
bool bigSram() {return bigRam;}
void* kmalloc(size_t memorySize)
{
	if (bigRam) return heap_caps_malloc(memorySize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	else return heap_caps_malloc(memorySize, MALLOC_CAP_INTERNAL  | MALLOC_CAP_8BIT);
		
}
void* kcalloc(size_t elementCount, size_t elementSize)
{
	if (bigRam) return heap_caps_calloc(elementCount,elementSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	else return heap_caps_calloc(elementCount,elementSize, MALLOC_CAP_INTERNAL  | MALLOC_CAP_8BIT);
		
}

void tsocket(const char* lab, uint32_t cnt)
{
		char* title = kmalloc(strlen(lab)+50);
		sprintf(title,"{\"%s\":\"%d\"}",lab,cnt*60); 
		websocketbroadcast(title, strlen(title));
		free(title);	
}


/******************************************************************************
 * FunctionName : checkUart
 * Description  : Check for a valid uart baudrate
 * Parameters   : baud
 * Returns      : baud
*******************************************************************************/
uint32_t checkUart(uint32_t speed)
{
	uint32_t valid[] = {1200,2400,4800,9600,14400,19200,28800,38400,57600,76880,115200,230400};
	int i ;
	for (i=0;i<12;i++){
		if (speed == valid[i]) return speed;
	}
	return 115200; // default
}

/******************************************************************************
 * FunctionName : init_hardware
 * Description  : Init all hardware, partitions etc
 * Parameters   : 
 * Returns      : 
*******************************************************************************/
static void init_hardware()
{
	if (VS1053_HW_init()) // init SPI
		VS1053_Start();

    ESP_LOGI(TAG, "Hardware initialized");
}

/* event handler for pre-defined wifi events */
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    EventGroupHandle_t wifi_event = ctx;

    switch (event->event_id)
    {
    case SYSTEM_EVENT_STA_START:
		FlashOn = FlashOff = 100;
        esp_wifi_connect();
        break;
		
	case SYSTEM_EVENT_STA_CONNECTED:
		xEventGroupSetBits(wifi_event, CONNECTED_AP);
		ESP_LOGI(TAG, "Wifi connected");		
		if (wifiInitDone) 
		{
			clientSaveOneHeader("Wifi Connected.",18,METANAME);	
			vTaskDelay(1000);
			autoPlay();
		}
		else
			wifiInitDone = true;
		break;

    case SYSTEM_EVENT_STA_GOT_IP:
		FlashOn = 5;FlashOff = 395;
        xEventGroupSetBits(wifi_event, CONNECTED_BIT);
        break;

    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
		FlashOn = FlashOff = 100;
		xEventGroupClearBits(wifi_event, CONNECTED_AP);
        xEventGroupClearBits(wifi_event, CONNECTED_BIT);
		ESP_LOGE(TAG, "Wifi Disconnected.");
		vTaskDelay(100);
        if (!getAutoWifi()&&(wifiInitDone)) 
		{
			ESP_LOGE(TAG, "reboot");
			vTaskDelay(100);			
			esp_restart();
		} else
		{
			if (wifiInitDone) // a completed init done
			{
				ESP_LOGE(TAG, "Connection tried again");
//				clientDisconnect("Wifi Disconnected.");
				clientSilentDisconnect();
				vTaskDelay(100);
				clientSaveOneHeader("Wifi Disconnected.",18,METANAME);	
				vTaskDelay(100);
				while (esp_wifi_connect() == ESP_ERR_WIFI_SSID) vTaskDelay(10);
			} else 
			{
				ESP_LOGE(TAG, "Try next AP");
				vTaskDelay(100);	
			} // init failed?
		}
        break;

	case SYSTEM_EVENT_AP_START:
		FlashOn = 5;FlashOff = 395;
		xEventGroupSetBits(wifi_event, CONNECTED_AP);
		xEventGroupSetBits(wifi_event, CONNECTED_BIT);
		wifiInitDone = true;
		break;
		
	case SYSTEM_EVENT_AP_STADISCONNECTED:
		break;
		
    default:
        break;
    }
    return ESP_OK;
}

static void unParse(char* str)
{
	int i ;
	if (str == NULL) return;
	for (i=0; i< strlen(str);i++)
	{
		if (str[i] == '\\')
		{
			str[i] = str[i+1];			
			str[i+1]=0;
			if (str[i+2] !=0)strcat(str, str+i+2);
		}
	}
}

static void start_wifi()
{
    ESP_LOGI(TAG, "starting wifi");
	setAutoWifi();
//	wifi_mode_t mode;
	char ssid[SSIDLEN]; 
	char pass[PASSLEN];
	
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
//    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
	
	tcpip_adapter_init();
	tcpip_adapter_dhcpc_stop(TCPIP_ADAPTER_IF_STA); // Don't run a DHCP client	
    /* FreeRTOS event group to signal when we are connected & ready to make a request */
	wifi_event_group = xEventGroupCreate();	
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, wifi_event_group) );
	
	if (g_device->current_ap == APMODE) 
	{
		if (strlen(g_device->ssid1) !=0)
		{g_device->current_ap = STA1;}
		else 
		{	if (strlen(g_device->ssid2) !=0)
			{g_device->current_ap = STA2;}
			else g_device->current_ap = APMODE;
		}	
		saveDeviceSettings(g_device);
	}
	
	while (1)
	{
		ESP_ERROR_CHECK( esp_wifi_stop() );
		vTaskDelay(10);
		
		switch (g_device->current_ap)
		{
			case STA1: //ssid1 used
				strcpy(ssid,g_device->ssid1);
				strcpy(pass,g_device->pass1);
				esp_wifi_set_mode(WIFI_MODE_STA) ;
				break;
			case STA2: //ssid2 used
				strcpy(ssid,g_device->ssid2);
				strcpy(pass,g_device->pass2);	
				esp_wifi_set_mode(WIFI_MODE_STA) ;	
				break;

			default: // other: AP mode
				g_device->current_ap = APMODE;
				ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP)) ;
		}
		
		if (g_device->current_ap == APMODE)
		{
			printf("WIFI GO TO AP MODE\n");
			wifi_config_t ap_config = {
				.ap = {
					.ssid = "WifiKaradio",
					.authmode = WIFI_AUTH_OPEN,
					.max_connection = 2,
					.beacon_interval = 200
				},
			};
			ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
			ESP_LOGE(TAG, "The default AP is  WifiKaRadio. Connect your wifi to it.\nThen connect a webbrowser to 192.168.4.1 and go to Setting\nMay be long to load the first time.Be patient.");

			vTaskDelay(1);
			ESP_ERROR_CHECK( esp_wifi_start() );			

		}
		else
		{
			printf("WIFI TRYING TO CONNECT TO SSID %d\n",g_device->current_ap);
			wifi_config_t wifi_config = {
				.sta = {
					.bssid_set = 0,
					.scan_method = WIFI_ALL_CHANNEL_SCAN,
					.sort_method = WIFI_CONNECT_AP_BY_SIGNAL,				},
			};
			strcpy((char*)wifi_config.sta.ssid,ssid);
			strcpy((char*)wifi_config.sta.password,pass);
			unParse((char*)(wifi_config.sta.ssid));
			unParse((char*)(wifi_config.sta.password));
			if  (strlen(ssid)/*&&strlen(pass)*/)
			{
				esp_wifi_disconnect();
				ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
//				ESP_LOGI(TAG, "connecting %s, %d, %s, %d",ssid,strlen((char*)(wifi_config.sta.ssid)),pass,strlen((char*)(wifi_config.sta.password)));
				ESP_LOGI(TAG, "connecting %s",ssid);
				ESP_ERROR_CHECK( esp_wifi_start() );	
			} else
			{
				g_device->current_ap++;
				g_device->current_ap %=3;
				
				if (getAutoWifi() && (g_device->current_ap == APMODE))
				{
					if (fgetc(stdin)==0xFF) // if a char read, stop the autowifi
					g_device->current_ap = STA1; // if autoWifi then wait for a reconnection to an AP
					ESP_LOGI(TAG,"Wait for the AP");
				}
				else 
					ESP_LOGI(TAG,"Empty AP. Try next one");
				
				saveDeviceSettings(g_device);
				continue;
			}				
		}

		/* Wait for the callback to set the CONNECTED_BIT in the event group. */
		if ( (xEventGroupWaitBits(wifi_event_group, CONNECTED_AP,false, true, 1500) & CONNECTED_AP) ==0) 
		//timeout . Try the next AP
		{
			g_device->current_ap++;
			g_device->current_ap %=3;
			if (getAutoWifi() && (g_device->current_ap == APMODE))
			{
				char inp = fgetc(stdin);
				printf("\nfgetc : %x\n",inp);
				if (inp==0xFF) // 
					g_device->current_ap = STA1;//if a char read, stop the autowifi
			}
			saveDeviceSettings(g_device);
			ESP_LOGI(TAG,"device->current_ap: %d",g_device->current_ap);	
		}	else break;	// 					
	}					
}

void start_network(){
//	struct device_settings *g_device;	
	tcpip_adapter_ip_info_t info;
	wifi_mode_t mode;	
	ip4_addr_t ipAddr;
	ip4_addr_t mask;
	ip4_addr_t gate;
	uint8_t dhcpEn = 0;

	switch (g_device->current_ap)
	{
		case STA1: //ssid1 used
			IP4_ADDR(&ipAddr, g_device->ipAddr1[0], g_device->ipAddr1[1],g_device->ipAddr1[2], g_device->ipAddr1[3]);
			IP4_ADDR(&gate, g_device->gate1[0],g_device->gate1[1],g_device->gate1[2], g_device->gate1[3]);
			IP4_ADDR(&mask, g_device->mask1[0], g_device->mask1[1],g_device->mask1[2], g_device->mask1[3]);
			dhcpEn = g_device->dhcpEn1;
		break;
		case STA2: //ssid2 used
			IP4_ADDR(&ipAddr, g_device->ipAddr2[0], g_device->ipAddr2[1],g_device->ipAddr2[2], g_device->ipAddr2[3]);
			IP4_ADDR(&gate, g_device->gate2[0],g_device->gate2[1],g_device->gate2[2], g_device->gate2[3]);
			IP4_ADDR(&mask, g_device->mask2[0], g_device->mask2[1],g_device->mask2[2], g_device->mask2[3]);
			dhcpEn = g_device->dhcpEn2;
		break;

		default: // other: AP mode
			IP4_ADDR(&ipAddr,192,168,4,1);
			IP4_ADDR(&gate,192,168,4,1);
			IP4_ADDR(&mask,255,255,255,0);
	}	
	
	ip4_addr_copy(info.ip, ipAddr);
	ip4_addr_copy(info.gw, gate);
	ip4_addr_copy(info.netmask, mask);
	
	ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));		
	if (mode == WIFI_MODE_AP)
	{
			xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,false, true, 3000);
			ip4_addr_copy(info.ip, ipAddr);
			tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info);
			strcpy(localIp , ip4addr_ntoa(&info.ip));
			printf("IP: %s\n\n",ip4addr_ntoa(&info.ip));	
	
	}
	else // mode STA
	{	
		if (dhcpEn) // check if IP is valid without DHCP
			tcpip_adapter_dhcpc_start(TCPIP_ADAPTER_IF_STA); //  run a DHCP client
		else
		{
			ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_STA, &info));
			dns_clear_servers(false);
			IP_SET_TYPE(( ip_addr_t* )&info.gw, IPADDR_TYPE_V4);    // mandatory
			(( ip_addr_t* )&info.gw)->addr = IPADDR_TYPE_V4;
			dns_setserver(0,( ip_addr_t* ) &info.gw);
			dns_setserver(1,( ip_addr_t* ) &info.gw);				// if static ip	check dns
		}

		// wait for ip						
		if ((xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,false, true, 3000) & CONNECTED_BIT) == 0) //timeout	
		{ // enable dhcp and restart
			if (g_device->current_ap ==1)
				g_device->dhcpEn1 = 1;
			else
				g_device->dhcpEn2 = 1;
			saveDeviceSettings(g_device);	
			esp_restart();
		}
		
		vTaskDelay(1);	
		// retrieve the current ip	
		tcpip_adapter_ip_info_t ip_info;
		ip_info.ip.addr =0;		
		while (ip_info.ip.addr ==0)
		{
			if (mode == WIFI_MODE_AP)
				tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
			else	
				tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
		}		
		ip_addr_t *ipdns0 = (ip_addr_t *)dns_getserver(0);
//		ip_addr_t ipdns1 = dns_getserver(1);
		printf("\nDNS: %s  \n",ip4addr_ntoa(( struct ip4_addr* ) ipdns0));
		strcpy(localIp , ip4addr_ntoa(&ip_info.ip));
		printf("IP: %s\n\n",ip4addr_ntoa(&ip_info.ip));
		
		if (dhcpEn) // if dhcp enabled update fields
		{  
			tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &info);
			switch (g_device->current_ap)
			{
			case STA1: //ssid1 used
				ip4addr_aton((const char *)&g_device->ipAddr1,(ip4_addr_t *)&info.ip);
				ip4addr_aton((const char *)&g_device->gate1,(ip4_addr_t *)&info.gw);
				ip4addr_aton((const char *)&g_device->mask1,(ip4_addr_t *)&info.netmask);
				break;

			case STA2: //ssid2 used
				ip4addr_aton((const char *)&g_device->ipAddr2,(ip4_addr_t *)&info.ip);
				ip4addr_aton((const char *)&g_device->gate2,(ip4_addr_t *)&info.gw);
				ip4addr_aton((const char *)&g_device->mask2,(ip4_addr_t *)&info.netmask);
				break;
			}
		}
		saveDeviceSettings(g_device);	
		tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, "karadio32");	
	}
	lcd_welcome(localIp,"IP found");
	vTaskDelay(10);
}

//blinking led and timer isr
IRAM_ATTR void timerTask(void* p) {
//	struct device_settings *device;	
	uint32_t cCur;
	queue_event_t evt;
//	int uxHighWaterMark;
	
	cCur = FlashOff*10;

	while(1) {
		// read and treat the timer queue events
		//int nb = uxQueueMessagesWaiting(event_queue);
		//if (nb >29) printf(" %d\n",nb);
		while (xQueueReceive(event_queue, &evt, 0))
		{
			switch (evt.type){
					case TIMER_1MS:
						if (divide)
							ctimeMs++;	// for led	
						divide = !divide;	
						ServiceAddon();
					break;				
					case TIMER_SLEEP:
						clientDisconnect("Timer"); // stop the player
					break;
					case TIMER_WAKE:
						clientConnect(); // start the player	
					break;
					default:
					break;
			}
		}
		vTaskDelay(1);	
	}
//	printf("t0 end\n");
	vTaskDelete( NULL ); // stop the task (never reached)
}

void uartInterfaceTask(void *pvParameters) {
	char tmp[255];
	int d;
	uint8_t c;
	int t ;
	esp_err_t err;
//	struct device_settings *device;
	uint32_t uspeed;
	int uxHighWaterMark;
	
	uspeed = g_device->uartspeed;		
   uart_config_t uart_config0 = {
        .baud_rate = uspeed,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,    //UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = 0,
    };	
	err = uart_param_config(UART_NUM_0, &uart_config0);	
	if (err!=ESP_OK) ESP_LOGE("uartInterfaceTask","uart_param_config err: %d",err);
	
	err = uart_driver_install(UART_NUM_0, 1024 , 0, 0, NULL, 0);
	if (err!=ESP_OK)
	{
		ESP_LOGE("uartInterfaceTask","uart_driver_install err: %d",err);
		vTaskDelete(NULL);
	}
	
	for(t = 0; t<sizeof(tmp); t++) tmp[t] = 0;
	t = 0;
	
	while(1) {
		while(1) {
			d= uart_read_bytes(UART_NUM_0, &c, 1, 100);
			if (d>0)
			{
				if((char)c == '\r') break;
				if((char)c == '\n') break;
				tmp[t] = (char)c;
				t++;
				if(t == sizeof(tmp)-1) t = 0;
			}
			//else printf("uart d: %d, T= %d\n",d,t);
			//switchCommand() ;  // hardware panel of command
		}
		checkCommand(t, tmp);
		uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
		ESP_LOGD("uartInterfaceTask",striWATERMARK,uxHighWaterMark,esp_get_free_heap_size());
				
		for(t = 0; t<sizeof(tmp); t++) tmp[t] = 0;
		t = 0;
	}	
}

// In STA mode start a station or start in pause mode.
// Show ip on AP mode.
void autoPlay()
{
	char apmode[50];
	sprintf(apmode,"at IP %s",localIp);
	if (g_device->current_ap == APMODE) 
	{
		clientSaveOneHeader("Configure the AP with the web page",34,METANAME);
		clientSaveOneHeader(apmode,strlen(apmode),METAGENRE);
	} else
	{
		clientSaveOneHeader(apmode,strlen(apmode),METANAME);
		if ((getVsVersion() < 3))
		{
			clientSaveOneHeader("Invalid audio output. VS1053 not found",38,METAGENRE);
			ESP_LOGE(TAG,"Invalid audio output. VS1053 not found");
			vTaskDelay(200);
		}		

		setCurrentStation( g_device->currentstation);
		if ((g_device->autostart ==1)&&(g_device->currentstation != 0xFFFF))
		{	
			kprintf("autostart: playing:%d, currentstation:%d\n",g_device->autostart,g_device->currentstation);
			vTaskDelay(10); // wait a bit
			playStationInt(g_device->currentstation);
		} else clientSaveOneHeader("Ready",5,METANAME);			
	}
}

/*
 * Main entry point
 */
void app_main()
{
	uint32_t uspeed;
	xTaskHandle pxCreatedTask;
	esp_err_t err;

	ESP_LOGI(TAG, "starting app_main()");
    ESP_LOGI(TAG, "RAM left: %u, Internal %u", esp_get_free_heap_size(), heap_caps_get_free_size(MALLOC_CAP_INTERNAL  | MALLOC_CAP_8BIT));

	const esp_partition_t *running = esp_ota_get_running_partition();
	ESP_LOGE(TAG, "Running partition type %d subtype %d (offset 0x%08x)",
             running->type, running->subtype, running->address);
    // Initialize NVS.
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        // OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
	
	// Check if we are in large SRAM config
	if (esp_get_free_heap_size() > 0x80000) bigRam = true;
	//init hardware	
	partitions_init();
	ESP_LOGI(TAG, "Partition init done...");
	
	if (g_device->cleared != 0xAABB)
	{	
		ESP_LOGE(TAG,"Device config not ok. Try to restore");
		free(g_device);
		restoreDeviceSettings(); // try to restore the config from the saved one
		g_device = getDeviceSettings();		
		if (g_device->cleared != 0xAABB)
		{
			ESP_LOGE(TAG,"Device config not cleared. Clear it.");
			free(g_device);
			eeEraseAll();
			g_device = getDeviceSettings();	
			g_device->cleared = 0xAABB; //marker init done
			g_device->uartspeed = 115200; // default
			g_device->trace_level = ESP_LOG_ERROR; //default
			g_device->vol = 100; //default
			g_device->led_gpio = 255;
			saveDeviceSettings(g_device);			
		} else
			ESP_LOGE(TAG,"Device config restored");
	}	
	
	copyDeviceSettings(); // copy in the safe partion

	// log on telnet
	if (g_device->options & T_LOGTEL)
		logTel = true; // 
	else
		logTel = false; //
	
	// init softwares
	telnetinit();
	websocketinit();

	// log level
	setLogLevel(g_device->trace_level);
	
	//time display
	uint8_t ddmm;
	//option_get_ddmm(&ddmm);	
	//setDdmm(ddmm?1:0);
	
	//SPI init for the VS1053 chip
    init_hardware();

	//ESP_LOGE(TAG,"Corrupt1 %d",heap_caps_check_integrity(MALLOC_CAP_DMA,1));
	
/*	
	// Init i2c if lcd doesn't not (spi) for rde5807=
	if (g_device->lcd_type >= LCD_SPI)
	{
		i2c_config_t conf;
	    conf.mode = I2C_MODE_MASTER;
	    conf.sda_io_num = (g_device->lcd_type == LCD_NONE)?PIN_I2C_SDA:PIN_SI2C_SDA;
		conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
	    conf.scl_io_num = (g_device->lcd_type == LCD_NONE)?PIN_I2C_SCL:PIN_SI2C_SCL;
	    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
	    conf.master.clk_speed = I2C_MASTER_RFREQ_HZ;
		//ESP_ERROR_CHECK
		(i2c_param_config(I2C_MASTER_NUM, &conf));
		ESP_LOGD(TAG, "i2c_driver_install %d", I2C_MASTER_NUM);
		//ESP_ERROR_CHECK
		(i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0));			
	}
*/

	//uart speed
	uspeed = g_device->uartspeed;	
	uspeed = checkUart(uspeed);	
	uart_set_baudrate(UART_NUM_0, uspeed);
	ESP_LOGI(TAG, "Set baudrate at %d",uspeed);
	if (g_device->uartspeed != uspeed)
	{
		g_device->uartspeed = uspeed;
		saveDeviceSettings(g_device);
	}	

	// Version infos
	ESP_LOGI(TAG, "\n");
	ESP_LOGI(TAG, "Project name: %s",esp_ota_get_app_description()->project_name);
	ESP_LOGI(TAG, "Version: %s",esp_ota_get_app_description()->version);
	ESP_LOGI(TAG, "Release %s, Revision %s",RELEASE,REVISION);
//	ESP_LOGI(TAG, "Date: %s,  Time: %s",esp_ota_get_app_description()->date,esp_ota_get_app_description()->time);
	ESP_LOGI(TAG, "SDK %s\n",esp_get_idf_version());	
	ESP_LOGI(TAG, "Date %s, Time: %s\n", __DATE__,__TIME__ );
	ESP_LOGI(TAG, "Heap size: %d",esp_get_free_heap_size());

	// lcd init
	//option_get_lcd_info(&g_device->lcd_type,&rt);
	ESP_LOGI(TAG,"LCD Type %d",g_device->lcd_type);

	lcd_init(g_device->lcd_type);
	ESP_LOGI(TAG, "Hardware init done...");

	lcd_welcome("","");
	lcd_welcome("","STARTING");
	
	// volume
	setIvol( g_device->vol);
	ESP_LOGI(TAG, "Volume set to %d",g_device->vol);
		

	// led blinks
	xTaskCreatePinnedToCore(timerTask, "timerTask",2100, NULL, PRIO_TIMER, &pxCreatedTask,CPU_TIMER); 
	ESP_LOGI(TAG, "%s task: %x","t0",(unsigned int)pxCreatedTask);		
	
	xTaskCreatePinnedToCore(uartInterfaceTask, "uartInterfaceTask", 2500, NULL, PRIO_UART, &pxCreatedTask,CPU_UART); 
	ESP_LOGI(TAG, "%s task: %x","uartInterfaceTask",(unsigned int)pxCreatedTask);
	
//-----------------------------
// start the network
//-----------------------------
    /* init wifi & network*/
    start_wifi();
	start_network();
	
//-----------------------------------------------------
//init softwares
//-----------------------------------------------------

	clientInit();	

    //initialize mDNS
    ESP_ERROR_CHECK(mdns_init());
    //set mDNS hostname (required if you want to advertise services)
    ESP_ERROR_CHECK(mdns_hostname_set(g_device->hostname));
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", g_device->hostname);
    //set default mDNS instance name
	ESP_ERROR_CHECK(mdns_instance_name_set(g_device->hostname));
	ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
	ESP_ERROR_CHECK(mdns_service_add(NULL, "_telnet", "_tcp", 23, NULL, 0));	

    // init player config
    player_config = (player_t*)kcalloc(1, sizeof(player_t));
    player_config->command = CMD_NONE;
    player_config->decoder_status = UNINITIALIZED;
    player_config->decoder_command = CMD_NONE;
    player_config->buffer_pref = BUF_PREF_SAFE;
    player_config->media_stream = kcalloc(1, sizeof(media_stream_t));

	audio_player_init(player_config);	  
	
	// LCD Display infos
    lcd_welcome(localIp,"STARTED");
	vTaskDelay(10);
    ESP_LOGI(TAG, "RAM left %d, Internal %u", esp_get_free_heap_size(),heap_caps_get_free_size(MALLOC_CAP_INTERNAL  | MALLOC_CAP_8BIT));

	//start tasks of KaRadio32
	vTaskDelay(1);
	xTaskCreatePinnedToCore(clientTask, "clientTask", 3800, NULL, PRIO_CLIENT, &pxCreatedTask,CPU_CLIENT); 
	ESP_LOGI(TAG, "%s task: %x","clientTask",(unsigned int)pxCreatedTask);	
	vTaskDelay(1);
    xTaskCreatePinnedToCore(serversTask, "serversTask", 3100, NULL, PRIO_SERVER, &pxCreatedTask,CPU_SERVER); 
	ESP_LOGI(TAG, "%s task: %x","serversTask",(unsigned int)pxCreatedTask);	
	vTaskDelay(1);
	xTaskCreatePinnedToCore (task_addon, "task_addon", 2200, NULL, PRIO_ADDON, &pxCreatedTask,CPU_ADDON);  
	ESP_LOGI(TAG, "%s task: %x","task_addon",(unsigned int)pxCreatedTask);	

	vTaskDelay(60);// wait tasks init
	ESP_LOGI(TAG," Init Done");
	
	setIvol(g_device->vol);
	kprintf("READY. Type help for a list of commands\n");
	// error log on telnet
	//esp_log_set_vprintf((vprintf_like_t)lkprintf);
	//autostart		
	autoPlay();
// All done.
}
