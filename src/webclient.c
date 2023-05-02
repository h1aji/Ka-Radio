/*
 * Copyright 2016 karawin (http://www.karawin.fr)
 */

#include "webclient.h"
#include "webserver.h"

#include "lwip/sockets.h"
#include "lwip/api.h"
#include "lwip/netdb.h"

#include "esp_common.h"

#include "freertos/semphr.h"

#include "vs1053.h"
#include "eeprom.h"
#include "buffer.h"
#include "interface.h"

enum clientStatus cstatus;
//static uint32_t metacount = 0;
//static uint16_t metasize = 0;

extern bool ledStatus;

xSemaphoreHandle sConnect, sConnected, sDisconnect, sHeader;

uint8_t once = 0;
uint8_t volume = 0;
uint8_t playing = 0;

static const char* icyHeaders[] = { "icy-name:", "icy-notice1:", "icy-notice2:",  "icy-url:", "icy-genre:", "icy-br:","icy-description:","ice-audio-info:", "icy-metaint:" };


char notfound[]={"Not Found"};
char parEmty[] = {" "};
const char CLIPLAY[] ICACHE_RODATA_ATTR STORE_ATTR  = {"##CLI.PLAYING#%c%c"};
const char CLISTOP[] ICACHE_RODATA_ATTR STORE_ATTR  = {"##CLI.STOPPED# from %s\n"};

const char strcMALLOC[] ICACHE_RODATA_ATTR STORE_ATTR  = {"Client: incmalloc fails for %d\n"};
const char strcMALLOC1[] ICACHE_RODATA_ATTR STORE_ATTR  = {"%s malloc fails\n"};
const char strcWEBSOCKET[] ICACHE_RODATA_ATTR STORE_ATTR  = {"WebClient webSocket fails %s errno: %d\n"};
const char strcSOCKET[] ICACHE_RODATA_ATTR STORE_ATTR  = {"WebClient Socket fails %s errno: %d\n"};

#define URLMAX	256
#define PATHMAX	512

static struct icyHeader header = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL};

static char metaint[10];
static char clientURL[URLMAX]= {0,0};
static char clientPath[PATHMAX] = {0,0};
static uint16_t clientPort = 80;


struct hostent *server = NULL;


void *incmalloc(size_t n)
{
	void* ret;	
//printf ("Client malloc of %d %d,  Heap size: %d\n",n,((n / 32) + 1) * 32,xPortGetFreeHeapSize( ));
	ret = malloc(n);
	if (ret == NULL) printf(strcMALLOC,n);
//	if (n <4) printf("Client: incmalloc size:%d\n",n);	
//	printf ("Client malloc after of %d bytes ret:%x  Heap size: %d\n",n,ret,xPortGetFreeHeapSize( ));
	return ret;
}	
void incfree(void *p,char* from)
{
	if (p != NULL) free(p);
//	else printf ("Client incfree from %s NULL\n",from);
//	printf ("Client incfree of %x, from %s           Heap size: %d\n",p,from,xPortGetFreeHeapSize( ));
}	




ICACHE_FLASH_ATTR bool getState()
{
	 return playing;
}

ICACHE_FLASH_ATTR void clientPrintState()
{
	if (playing)
		kprintf(CLIPLAY,0x0d,0x0a);
	else
		kprintf(CLISTOP,"State");
}
ICACHE_FLASH_ATTR void clientInit() {
	vSemaphoreCreateBinary(sHeader);
	vSemaphoreCreateBinary(sConnect);
	vSemaphoreCreateBinary(sConnected);
	vSemaphoreCreateBinary(sDisconnect);
	xSemaphoreTake(sConnect, portMAX_DELAY);
	xSemaphoreTake(sConnected, portMAX_DELAY);
	xSemaphoreTake(sDisconnect, portMAX_DELAY);
}

ICACHE_FLASH_ATTR uint8_t clientIsConnected() {
	if(xSemaphoreTake(sConnected, 0)) {
		xSemaphoreGive(sConnected);
		return 0;
	}
	return 1;
}

// for debug only

ICACHE_FLASH_ATTR void dump(uint8_t* from, uint32_t len )
{
	uint32_t i = 0;
	uint8_t* addr ;
	addr =  from;
	for (i;i<len;i+=16){
		printf(PSTR("\n%x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x    \t\
		%c %c %c %c %c %c %c %c %c %c %c %c %c %c %c"),addr,addr[0],addr[1],addr[2],addr[3],addr[4],addr[5],addr[6],addr[7],addr[8],addr[9],addr[10],addr[11],addr[12],addr[13],addr[14],addr[15],addr[0],addr[1],addr[2],addr[3],addr[4],addr[5],addr[6],addr[7],addr[8],addr[9],addr[10],addr[11],addr[12],addr[13],addr[14],addr[15]);
		addr+=16;
	}	
	printf("\n");
}

ICACHE_FLASH_ATTR struct icyHeader* clientGetHeader()
{
	return &header;
}

	
ICACHE_FLASH_ATTR bool clientParsePlaylist(char* s)
{
  char* str;
  char* ns; 
  char path[PATHMAX] = "/";
  char url[URLMAX] = "";
  char port[6] = "80";
  int remove = 0;
  int i = 0; int j = 0;
  
// for extm3u skip line with #EXTINF  
  str = strstr(s,"#EXTINF");
  if (str != NULL) //skip to next line
  {
	ns = str;
    while ((strlen(ns) > 1) && (ns[0]!=0x0A)) ns++;
//	printf("EXTM3U: %s\n",ns);
	s= ns;
  }
  str = strstr(s,"<location>http://");  //for xspf
  if (str != NULL) remove = 17;
  str = strstr(s,"<REF href = \"http://");  //for asx
  if (str != NULL) remove = 20;
  
  if (str ==NULL) 
  {	  
	str = strstr(s,"http://");
	if (str != NULL) remove = 7;
  }
  if (str ==NULL) 
  {	  
	str = strstr(s,"https://");
	if (str != NULL) remove = 8;
  } 
  if (str != NULL) 
  
  {
	str += remove; //skip http://
	
//	printf("parse str %s\n",str);
	
	while ((str[i] != '/')&&(str[i] != ':')&&(str[i] != 0x0a)&&(str[i] != 0x0d)&&(j<77)) {url[j] = str[i]; i++ ;j++;}
	url[j] = 0;
//	kprintf("parse str url %s\n",url);
	j = 0;
	if (str[i] == ':')  //port
	{
		i++;
		while ((str[i] != '/')&&(str[i] != 0x0a)&&(str[i] != 0x0d)) {port[j] = str[i]; i++ ;j++;}
		port[j] = 0;
	}
	j = 0;
	if ((str[i] != 0x0a)&&(str[i] != 0x0d)&&(str[i] != 0)&&(str[i] != '"')&&(str[i] != '<')&&(j<PATHMAX))
	{	
	  while ((str[i] != 0x0a)&&(str[i] != 0x0d)&&(str[i] != 0)&&(str[i] != '"')&&(str[i] != '<')&&(j<PATHMAX)) {path[j] = str[i]; i++; j++;}
	  path[j] = 0;
	}
	
	
	
	if (strncmp(url,"localhost",9)!=0) clientSetURL(url);
	clientSetPath(path);
	clientSetPort(atoi(port));
//kprintf("##CLI.URL#: %s, path: %s, port: %s\n",url,path,port);
	return true;
  }
  else 
  { 
   cstatus = C_DATA;
   return false;
  }
}
ICACHE_FLASH_ATTR char* stringify(char* str,int len)
{
	#define MORE	20
//		if ((strchr(str,'"') == NULL)&&(strchr(str,'/') == NULL)) return str;
        if (len == 0) return str;
		char* new = incmalloc(len+MORE);
		int nlen = len+MORE;
		if (new != NULL)
		{
//			printf("stringify: enter: len:%d  \"%s\"\n",len,str);
			int i=0 ,j =0;
			for (i = 0;i< len+10;i++) new[i] = 0;
			for (i=0;i< len;i++)
			{
				if (str[i] == '"') {
					new[j++] = '\\';
					new[j++] =(str)[i] ;
				} else
				if (str[i] == '/') {
					new[j++] = '\\';
					new[j++] =(str)[i] ;
				}
				else				
				if (str[i] == '\\') {
					new[j++] = '\\';
					new[j++] =(str)[i] ;
				}				
/*				else	// pseudo ansi utf8 convertion
					if ((str[i] > 192) && (str[i+1] < 0x80)){ // 128 = 0x80
					new[j++] = 195; // 192 = 0xC0   195 = 0xC3
					new[j++] =(str)[i]-64 ; // 64 = 0x40
				} */
				else new[j++] =(str)[i] ;
				
				if ( j+MORE> nlen) 
				{
					nlen +=MORE;
					new = realloc(new,nlen); // some room
				}
			}
			incfree(str,"str");

			new = realloc(new,j+1); // adjust
//printf("stringify: exit: len:%d  \"%s\"\n",j,new);
			return new;		
		} else 
		{
			printf(strcMALLOC1,"stringify");
		}	
		return str;
}

ICACHE_FLASH_ATTR bool clientPrintMeta()
{
	if (header.members.mArr[METADATA] != NULL)
		kprintf(PSTR("##CLI.META#: %s\n"),header.members.mArr[METADATA]);
	else
		kprintf(PSTR("##CLI.META#:%c"), 0x0D);
}

ICACHE_FLASH_ATTR char* getMeta()
{
	return (header.members.mArr[METADATA] == NULL)?parEmty:header.members.mArr[METADATA];
}

ICACHE_FLASH_ATTR void removePartOfString(char* origine, char* remove)
{
	if (strlen(origine) == 0) return;
	char* copy = incmalloc(strlen(origine));
	char* t_end;
	if (copy != NULL)
	{
		while ( (t_end = strstr(origine,remove))!= NULL)
		{
			*t_end = 0;
			strcpy(copy,origine);
			strcat(copy,t_end+(strlen(remove)));
			strcpy(origine,copy);
		}
		incfree(copy,"removePt");
	}
}

// A metadata found. Extract the Stream title
ICACHE_FLASH_ATTR void clientSaveMetadata(char* s,int len)
{
	char* t_end = NULL;
	char* t ;
	bool found = false;
	if ((len == 0)||(s==NULL)) printf("clientSaveMetadata:  len:%d\n",len); 
	if ((len > 256) ||(s == NULL) || (len == 0))
	{
		if (header.members.mArr[METADATA] != NULL)
		incfree(header.members.mArr[METADATA],"metad");
		header.members.mArr[METADATA] = NULL;
		return;
	}
	t = s;
	len = strlen(t);
//printf("clientSaveMetadata:  len:%d   char:%s\n",len,s);
	t_end = strstr(t,"song_spot=");
	if (t_end != NULL)
	{ 
		*t_end = 0;
		found = true;
		removePartOfString(t, "text=");
		removePartOfString(t, "\"");
	}
	else
	{
		t_end = strstr(t,";StreamUrl='");
		if (t_end != NULL) 
		{
			*t_end = 0;found = true;
		} 
	}			
	t = strstr(t,"StreamTitle='");
	if (t!= NULL) {t += 13;found = true;} else t = s;
	len = strlen(t);
	if ((t_end != NULL)&&(len >=3)) t_end -= 3;
	else {
		if (t_end != NULL) t_end -=1;
		else
		if (len >=2) {t_end = t+len-2;found = true;} 
		else t_end = t+len;
	}
	if (found)
	{	
		t_end = strstr(t_end,"'");
		if (t_end !=NULL)
		*t_end = 0;
	
		if (t!=NULL)
		{
			t_end = strstr(t,"||");
			if (t_end !=NULL)
			*t_end = 0;
		}
			
	}
	else
	{
		if (len >=2) len-=2; 
	}

//printf("clientSaveMetadata0:  len:%d   char:%s\n",strlen(t),t);
// see if it is !=
	char* tt;
	tt = incmalloc((len+5)*sizeof(char));
	if (tt != NULL)
	{
		strcpy(tt,t);
		tt = stringify(tt,len); // to compare we need to stringify
	}
	if  ((header.members.mArr[METADATA] == NULL)||((header.members.mArr[METADATA] != NULL)&&(t!= NULL)&&(strcmp(tt,header.members.mArr[METADATA]) != 0)))
	{
		if (header.members.mArr[METADATA] != NULL)
			incfree(header.members.mArr[METADATA],"metad");
		header.members.mArr[METADATA] = (char*)incmalloc((len+3)*sizeof(char));
		if(header.members.mArr[METADATA] == NULL) 
		{	printf(strcMALLOC1);
			return;
		}

		strcpy(header.members.mArr[METADATA], t);
//			dump((uint8_t*)(header.members.mArr[METADATA]),strlen(header.members.mArr[METADATA]));
		header.members.mArr[METADATA] = stringify(header.members.mArr[METADATA],len);
		clientPrintMeta(); 
		while ((header.members.mArr[METADATA][strlen(header.members.mArr[METADATA])-1] == ' ')||
			(header.members.mArr[METADATA][strlen(header.members.mArr[METADATA])-1] == '\r')||
		(header.members.mArr[METADATA][strlen(header.members.mArr[METADATA])-1] == '\n')
		)
		{
			header.members.mArr[METADATA][strlen(header.members.mArr[METADATA])-1] = 0; // avoid blank at end
		}

// send station name if no metadata
		if (strlen(header.members.mArr[METADATA])!=0)
			t_end = header.members.mArr[METADATA];
		else
			t_end = (header.members.single.name ==NULL)?"":header.members.single.name;

		char* title = incmalloc(strlen(t_end)+15);
		if (title != NULL)
		{
//printf("sprint%d\n",1);
			if(kasprintf(title,PSTR("{\"meta\":\"%s\"}"),t_end))
				websocketbroadcast(title, strlen(title));
			incfree(title,"title");
		} else printf(strcMALLOC1,"Title"); 
	}
	incfree(tt,"");		
//	printf("clientSaveMetadata: %s\n",header.members.mArr[METADATA]);
}	

// websocket: next station
ICACHE_FLASH_ATTR void wsStationNext()
{
	struct shoutcast_info* si =NULL;
	do {
		++currentStation;
		if (currentStation >= 255)
			currentStation = 0;
		if (si != NULL) incfree(si,"wsstation");
		si = getStation(currentStation);	
	}
	while (si == NULL || ((si != NULL)&&(strcmp(si->domain,"")==0)) || ((si != NULL)&&(strcmp( si->file,"")== 0)));

	playStationInt	(currentStation);
	incfree(si,"wsstation");
}
// websocket: previous station
ICACHE_FLASH_ATTR void wsStationPrev()
{
	struct shoutcast_info* si = NULL;
	do {
		if (currentStation >0)
		{	
			if (si != NULL) incfree(si,"wsstation");
			si = getStation(--currentStation);
		}	
		else return;
	}
	while (si == NULL || ((si != NULL)&&(strcmp(si->domain,"")==0)) || ((si != NULL)&&(strcmp( si->file,"")== 0)));

	playStationInt	(currentStation);
	incfree(si,"wsstation");
}

// websocket: broadcast volume to all client
ICACHE_FLASH_ATTR void wsVol(char* vol)
{
	char answer[21];
	if (vol != NULL)
	{	
//printf("sprint%d\n",2);
		if (kasprintf(answer,PSTR("{\"wsvol\":\"%s\"}"),vol))
			websocketbroadcast(answer, strlen(answer));
	} 
}	
// websocket: broadcast monitor url
ICACHE_FLASH_ATTR void wsMonitor()
{
		char *answer;
		uint16_t len;
		len = strlen(clientURL)+strlen(clientPath)+30;
		answer= malloc(len);
		if (answer)
		{
			memset(answer,0,len);
			if ((clientPath[0]!= 0))
			{
//printf("sprint%d\n",3);
				if (kasprintf(answer,PSTR("{\"monitor\":\"http://%s:%d%s\"}"),clientURL,clientPort,clientPath))
				websocketbroadcast(answer, strlen(answer));
//printf("answer: %s\n",answer);
			}
			free(answer);
		}
}						
//websocket: broadcast all icy and meta info to web client.
ICACHE_FLASH_ATTR void wsHeaders()
{
	uint8_t header_num;
	char currentSt[6]; 	
	sprintf(currentSt,("%d"),currentStation);
	char* not2;
	not2 = header.members.single.notice2;
	if (not2 ==NULL) not2=header.members.single.audioinfo;
	if ((header.members.single.notice2 != NULL)&&(strlen(header.members.single.notice2)==0)) not2=header.members.single.audioinfo;
	int json_length ;
	json_length =104+ //93
		strlen(currentSt)+
		((header.members.single.description ==NULL)?0:strlen(header.members.single.description)) +
		((header.members.single.name ==NULL)?0:strlen(header.members.single.name)) +
		((header.members.single.bitrate ==NULL)?0:strlen(header.members.single.bitrate)) +
		((header.members.single.url ==NULL)?0:strlen(header.members.single.url))+ 
		((header.members.single.notice1 ==NULL)?0:strlen(header.members.single.notice1))+
		((not2 ==NULL)?0:strlen(not2))+
		((header.members.single.genre ==NULL)?0:strlen(header.members.single.genre))+
		((header.members.single.metadata ==NULL)?0:strlen(header.members.single.metadata))
		;
	char* wsh = incmalloc(json_length+1);
	if (wsh == NULL) {printf(strcMALLOC1,"wsHeader");return;}
//printf("sprint%d\n",5);
	if (kasprintf(wsh,PSTR("{\"wsicy\":{\"curst\":\"%s\",\"descr\":\"%s\",\"meta\":\"%s\",\"name\":\"%s\",\"bitr\":\"%s\",\"url1\":\"%s\",\"not1\":\"%s\",\"not2\":\"%s\",\"genre\":\"%s\"}}"),
			currentSt,
			(header.members.single.description ==NULL)?"":header.members.single.description,
			(header.members.single.metadata ==NULL)?"":header.members.single.metadata,	
			(header.members.single.name ==NULL)?"":header.members.single.name,
			(header.members.single.bitrate ==NULL)?"":header.members.single.bitrate,
			(header.members.single.url ==NULL)?"":header.members.single.url,
			(header.members.single.notice1 ==NULL)?"":header.members.single.notice1,
			(not2 ==NULL)?"":not2 ,
			(header.members.single.genre ==NULL)?"":header.members.single.genre))
			{
//printf("WSH: len:%d  \"%s\"\n",strlen(wsh),wsh);
				websocketbroadcast(wsh, strlen(wsh));	
			}
	incfree (wsh,"wsh");
}	

//Clear all ICY and META infos
ICACHE_FLASH_ATTR void clearHeaders()
{
	uint8_t header_num;
	for(header_num=0; header_num<ICY_HEADER_COUNT; header_num++) {
		if(header_num != METAINT) 
			if(header.members.mArr[header_num] != NULL) {
				incfree(header.members.mArr[header_num],"header" );
//				header.members.mArr[header_num][0] = 0;				
				header.members.mArr[header_num] = NULL;				
			}
	}
	header.members.mArr[METAINT] = 0;
	wsHeaders();
}
	
ICACHE_FLASH_ATTR bool clientPrintOneHeader(uint8_t header_num)
{
	if (header.members.mArr[header_num] != NULL)
	kprintf(PSTR("##CLI.ICY%d#: %s\n"),header_num,header.members.mArr[header_num]);
}

ICACHE_FLASH_ATTR bool clientPrintHeaders()
{
	uint8_t header_num;
	for(header_num=0; header_num<ICY_HEADER_COUNT; header_num++) {
		if((header_num != METAINT) && (header_num != METADATA))
			if(header.members.mArr[header_num] != NULL) {
				kprintf(PSTR("##CLI.ICY%d#: %s\n"),header_num,header.members.mArr[header_num]);
			}	
	}
	clientPrintMeta();	
}	

ICACHE_FLASH_ATTR bool clientSaveOneHeader(char* t, uint16_t len, uint8_t header_num)
{
	char* tt;
	if(header.members.mArr[header_num] != NULL) 
		incfree(header.members.mArr[header_num],"headernum");
	tt = incmalloc((len+1)*sizeof(char));
	if(tt == NULL)
	{
		printf(strcMALLOC1,"clientSOneH");
		return false;
	}	
	
	int i;
	for(i = 0; i<len+1; i++) tt[i] = 0;
	strncpy(tt, t, len);
//	header.members.mArr[header_num] = stringify(header.members.mArr[header_num],len);
	header.members.mArr[header_num] = stringify(tt,len); //tt is freed here
	vTaskDelay(10);
	clientPrintOneHeader(header_num);
//	printf("header after num:%d addr:0x%x  cont:\"%s\"\n",header_num,header.members.mArr[header_num],header.members.mArr[header_num]);
	return true;
}

	
ICACHE_FLASH_ATTR bool clientParseHeader(char* s)
{
	// icy-notice1 icy-notice2 icy-name icy-genre icy-url icy-br
	uint8_t header_num;
	bool ret = false;
//	printf("ParseHeader: %s\n",s);
	xSemaphoreTake(sHeader,portMAX_DELAY);
	if ((cstatus != C_HEADER1)&& (cstatus != C_PLAYLIST))// not ended. dont clear
	{
		clearHeaders();
	}
	for(header_num=0; header_num<ICY_HEADERS_COUNT; header_num++)
	{
//				printf("icy deb: %d\n",header_num);		
		char *t;
		t = strstr(s, icyHeaders[header_num]);
		if( t != NULL )
		{
			t += strlen(icyHeaders[header_num]);
			char *t_end = strstr(t, "\r\n");
			if(t_end != NULL)
			{
//				printf("icy in: %d\n",header_num);		
				uint16_t len = t_end - t;
				if(header_num != METAINT) // Text header field
				{
					ret = clientSaveOneHeader(t, len, header_num);
				}
				else // Numerical header field
				{					
						int i;
						for(i = 0; i<len+1; i++) metaint[i] = 0;
						strncpy(metaint, t, len);
						header.members.single.metaint = atoi(metaint);
//						printf("len = %d,MetaInt= %s, Metaint= %d\n",len, metaint,header.members.single.metaint);
						ret = true;
//						printf("icy: %s, %d\n",icyHeaders[header_num],header.members.single.metaint);					
				}
			}
		}
	}
	if (ret == true) {
		wsHeaders();
//		wsMonitor();
	}
	xSemaphoreGive(sHeader);
		return ret;
}


ICACHE_FLASH_ATTR void clientSetName(char* name,uint16_t index)
{
	kprintf(PSTR("##CLI.NAMESET#: %d %s\n"),index,name);
}

ICACHE_FLASH_ATTR void clientSetURL(char* url)
{
	int l = strlen(url)+1;
	if (url[0] == 0xff) return; // wrong url
	strcpy(clientURL, url);
	kprintf(PSTR("##CLI.URLSET#: %s\n"),clientURL);
}

ICACHE_FLASH_ATTR void clientSetPath(char* path)
{
	if (path[0] == 0xff) return; // wrong path
	strcpy(clientPath, path);
	kprintf(PSTR("##CLI.PATHSET#: %s\n"),clientPath);
}

ICACHE_FLASH_ATTR void clientSetPort(uint16_t port)
{
	clientPort = port;
	kprintf(PSTR("##CLI.PORTSET#: %d\n"),port);
}


ICACHE_FLASH_ATTR void clientConnect()
{
	cstatus = C_HEADER;
	once = 0;
	if((server = (struct hostent*)gethostbyname(clientURL))) {
		xSemaphoreGive(sConnect);
	} else {
		clientDisconnect(PSTR("clientConnect"));
	}
}
ICACHE_FLASH_ATTR void clientConnectOnce()
{
	cstatus = C_HEADER;
	once = 1; // play one time
	if((server = (struct hostent*)gethostbyname(clientURL))) {
		xSemaphoreGive(sConnect);
	} else {
		clientDisconnect(PSTR("clientConnectOnce"));
	}
}
ICACHE_FLASH_ATTR void clientSilentConnect()
{
	cstatus = C_HEADER;
	once = 0;
	if(server != NULL) {
		xSemaphoreGive(sConnect);
	} else {
		clientSilentDisconnect();
	}
}
ICACHE_FLASH_ATTR void clientSilentDisconnect()
{
	xSemaphoreGive(sDisconnect);
}

ICACHE_FLASH_ATTR void clientDisconnect(const char* from)
{
	//connect = 0;
	char* lfrom = malloc(strlen(from)+16);
	if (lfrom != NULL)
	{	
		flashRead(lfrom,(int)from,strlen(from));
		lfrom[strlen(from)] = 0;
		kprintf(CLISTOP,lfrom);
		incfree(lfrom,"disc");
	}
	xSemaphoreGive(sDisconnect);

	if (!ledStatus) gpio2_output_set(1);
	vTaskDelay(10);
//	clearHeaders();
}

IRAM_ATTR void clientReceiveCallback(int sockfd, char *pdata, int len)
{
	static int metad ;
	static int rest ;
	static uint32_t chunked;
	static uint32_t cchunk;
	static char* metadata = NULL;
	uint16_t l ;
	uint32_t lc;
	char *inpdata;
	char* inpchr;
	uint32_t clen;
	int bread;
	char* t1;
	char* t2;
	bool  icyfound;

//if (cstatus != C_DATA) {printf("cstatus= %d\n",cstatus);  printf("Len=%d, Byte_list = %s\n",len,pdata);}
	if (cstatus != C_DATA)
	{
		t1 = strstr(pdata, "404"); 
		if (t1 != NULL) t1 = strstr(pdata, notfound); 
		if (t1 != NULL) { // 
			kprintf(CLIPLAY,0x0d,0x0a);
			kprintf(PSTR("%c"),0x0d);
			clientSaveOneHeader(notfound, 13,METANAME);
			wsHeaders();
			vTaskDelay(150);
			clientDisconnect(PSTR("C_DATA"));
			cstatus = C_HEADER;
			return;
		}	
	}	
	switch (cstatus)
	{
	case C_PLAYLIST:
         if (!clientParsePlaylist(pdata)) //need more
		  cstatus = C_PLAYLIST1;
		else {clientDisconnect(PSTR("C_PLIST"));  }
    break;
	case C_PLAYLIST1:
       clientDisconnect(PSTR("C_PLIST1"));	   
        clientParsePlaylist(pdata) ;//more?
		cstatus = C_PLAYLIST;
	break;
	case C_HEADER0:
	case C_HEADER:
		clearHeaders();
		metad = -1;	
		t1 = strstr(pdata, "302 "); 
		if (t1 ==NULL) t1 = strstr(pdata, "301 "); 
		if (t1 != NULL) { // moved to a new address
			if( strcmp(t1,"Found")||strcmp(t1,"Temporarily")||strcmp(t1,"Moved"))
			{
//printf("Len=%d,\n %s\n",len,pdata);
				kprintf(PSTR("Header: Moved%c"),0x0d);
				clientDisconnect(PSTR("C_HDER"));
				clientParsePlaylist(pdata);
				cstatus = C_PLAYLIST;				
			}	
			break;
		}
		//no break here
	case C_HEADER1:  // not ended
		{
			int i = 0;
			cstatus = C_HEADER1;
			do {
				t1 = strstr(pdata, "\r\n\r\n"); // END OF HEADER
//printf("Header len: %d,  Header: %s\n",len,pdata);
				if ((t1 != NULL) && (t1 <= pdata+len-4)) 
				{
						t2 = strstr(pdata, "Internal Server Error"); 
						if (t2 != NULL)
						{
							printf(PSTR("Internal Server Error%c"),0x0d);
							clientDisconnect(PSTR("Internal Server Error"));
							cstatus = C_HEADER;
							
						}
						icyfound = clientParseHeader(pdata);
//						wsMonitor();											
/*						if(header.members.single.bitrate != NULL) 
							if (strcmp(header.members.single.bitrate,"320")==0)
								 system_update_cpu_freq(SYS_CPU_160MHZ);
							else system_update_cpu_freq(SYS_CPU_80MHZ);*/
						if(header.members.single.metaint > 0) 
							metad = header.members.single.metaint;
//	printf("t1: 0x%x, cstatus: %d, icyfound: %d  metad:%d Metaint:%d\n", t1,cstatus, icyfound,metad, header.members.single.metaint); 
						cstatus = C_DATA;	// a stream found

						t2 = strstr(pdata, "Transfer-Encoding: chunked"); // chunked stream? 
//						t2 = NULL;
						chunked = 0;
						t1+= 4; 
						if ( t2 != NULL) 
						{
							while (len -(t1-pdata)<8) {vTaskDelay(1);len += recv(sockfd, pdata+len, RECEIVE+8-len, 0); }
							chunked = (uint32_t) strtol(t1, NULL, 16) +2;
							if (strchr((t1),0x0A) != NULL)
								*strchr(t1,0x0A) = 0;
							
//	printf("chunked: %d,  strlen: %d  \"%s\"\n",chunked,strlen(t1)+1,t1);
							t1 +=strlen(t1)+1; //+1 for char 0, 
						}
						
						int newlen = len - (t1-pdata) ;
						cchunk = chunked;
//	printf("newlen: %d   len: %d   chunked:%d  pdata:%x \n",newlen,len,chunked,pdata);
						if(newlen > 0) clientReceiveCallback(sockfd,t1, newlen);
				} else
				{
					t1 = NULL;
					if (i++ > 5) {clientDisconnect("header1");break;}
					vTaskDelay(1); //avoid watchdog is infernal loop
					bread = recvfrom(sockfd, pdata+len, RECEIVE-len, 0,NULL,NULL);
					if ( bread < 0 )
					{
						kprintf(PSTR("Chunk: errno: %d, read: %d, "),errno, bread);
					}					
					if (bread > 0) len += bread;
				}
			} while (t1 == NULL);
		}
	break;
	default:		
// -----------	

// Chunk computing
		lc = len; // lc rest after chunk
//	 printf("CDATAIN: chunked: %d, cchunk: %d, len: %d\n",chunked,cchunk,len);
		if((chunked != 0)&&((cchunk ==0)||(len >= cchunk-1)))  //if in chunked mode and chunk received or complete in data
		{
//	 printf("CDATA1: chunked: %d, cchunk: %d, len: %d\n",chunked,cchunk,len);
			if (len == cchunk) // if a complete chunk in pdata, remove crlf
			{ 
				len -= 2;
				cchunk = 0;
//	printf("lenoe:%d, chunked:%d  cchunk:%d, lc:%d, metad:%d\n",len,chunked,cchunk, lc,metad );
			} else  // an incomplete chunk in progress
			{	
				if (len == cchunk-1) // missing lf: remove cr only, wait lf in next data
				{ 
					len -= 1;
					cchunk = 1;
//	printf("leno1:%d, chunked:%d  cchunk:%d, lc:%d, metad:%d\n",len,chunked,cchunk, lc,metad );
				} 				
				else		// a part of end of chunk 	and beginnining of a new one
				{
					inpdata = pdata;
					
					while (lc != 0)
					{					
						while (lc < cchunk+9) 
						{
							vTaskDelay(1);
							bread = recvfrom(sockfd, pdata+len, 9, 0,NULL,NULL); 
							if ( bread < 0 )
							{
								kprintf(PSTR("Chunk1: errno: %d, read: %d, "),errno, bread);
							}					
							if (bread >=0)
								clen = bread;
							else clen = 0;
							lc+=clen;len+=clen;
//	printf("more:%d, lc:%d\n",clen,lc);
						} //security to be sure to receive the new length
						
//	printf("leni0:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d, \n",len,inpdata,chunked,cchunk, lc );
						inpchr=strchr(inpdata+cchunk,0x0D) ;
						if ((inpchr != NULL) &&(inpchr- (inpdata+cchunk) <16))
							*inpchr = 0; // replace lf by a end of string
						else {
/*							printf("0D not found\n");
							printf("len:%d, inpdata:%x, pdata:%x,chunked:%d  cchunk:%d, lc:%d, str:%s\n",len,inpdata,pdata,chunked,cchunk, lc,inpdata+cchunk );*/
							clientDisconnect(PSTR("chunk")); clientConnect();
							lc = 0; 
							break;
						}
						chunked = (uint32_t) strtol(inpdata+cchunk, NULL, 16)+2;  // new chunk lenght including cr lf
						clen = strlen(inpdata+cchunk)  +2;
						lc = lc -cchunk  -clen; // rest after
//	printf("leni:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d, clen:%d, str: %s\n",len,inpdata,chunked,cchunk, lc,clen,inpdata+cchunk );
						// compact data without chunklen and crlf
						if (cchunk >1){
							memcpy (inpdata+cchunk-2,pdata+len-lc, lc); 
//	printf("lenm:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d\n",len,inpdata,chunked,cchunk, lc);
							len -= (clen +2);
							inpdata +=   (cchunk -2);
//	printf("memcpy1 at %x from %x, lc:%d\n",inpdata+cchunk-2,pdata+len-lc,lc);
						}
						else{
							memcpy (inpdata,inpdata+cchunk+clen, lc); 
//	printf("lenm:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d\n",len,inpdata,chunked,cchunk, lc);
							len -= (clen + cchunk);							
//	printf("memcpy2 at %x from %x, lc:%d, len:%d\n",inpdata,inpdata+cchunk+clen,lc,len);
						}

						if (chunked > lc)
						{						
							cchunk = chunked - lc ;
							if (cchunk ==1) len --;
							if (cchunk ==0) len -=2;
							lc = 0;
						}	
						else
						{
							cchunk = chunked;
						}	
//	printf("leniout:%d, inpdata:%x, chunked:%d  cchunk:%d, lc:%d, metad:%d  clen:%d \n",len,inpdata,chunked,cchunk, lc,metad,clen );				
					}
				}
			}
		} 
		else 
		{
			if (chunked != 0) cchunk -= len; 
			lc = 0;
		}
		
// printf("CDATAOUT: chunked: %d, cchunk: %d, len: %d\n",chunked,cchunk,len);
		
// meta data computing
		if (rest <0) 
		{
//printf("Negative enter len= %d, metad= %d  rest= %d   pdata= %x :\"%s\"\n",len,metad,rest,pdata,pdata);
			if (len>-rest)
//				*(pdata+len-rest) = 0; //truncated
				*(pdata-rest) = 0; //truncated
			else
				*(pdata+len) = 0; //truncated
			strcat(metadata,pdata);
//			metad = header.members.single.metaint ;
			if (len>-rest)
			{
//printf("Negaposi   len= %d, metad= %d  rest= %d   pdata= %x :\"%s\"\n",len,metad,rest,pdata,pdata);
				clientSaveMetadata(metadata,strlen(metadata));
				metad = header.members.single.metaint ;
				pdata -= rest;	
				len += rest;
				rest = 0;
			}
			else
			{
//printf("Negative   len= %d, metad= %d  rest= %d   pdata= %x :\"%s\"\n",len,metad,rest,pdata,pdata);
				pdata += len;
				rest += len;
				len = 0;
//printf("Negatafter len= %d, metad= %d  rest= %d   pdata= %x :\"%s\"\n",len,metad,rest,pdata,pdata);
			}			
//printf("Negative len out = %d, pdata: %x,metad= %d  rest= %d \n",len,pdata,metad,rest);

		}
		inpdata = pdata;
		clen = len;
		if((header.members.single.metaint != 0)&&(clen > metad)) 
		{
//printf("metain len:%d, clen:%d, metad:%d, l:%d, inpdata:%x, rest:%d\n",len,clen,metad, l,inpdata,rest );
			int jj = 0;
			while ((clen > metad)&&(header.members.single.metaint != 0)) // in buffer
			{
//printf("metainb len:%d, clen:%d, metad:%d, l:%d, inpdata:%x, rest:%d\n",len,clen,metad, l,inpdata,rest );
				jj++;
				l = inpdata[metad]*16;	//new meta length
				rest = clen - metad  -l -1;
/*				
if (l !=0){
	printf("metain len:%d, clen:%d, metad:%d, l:%d, inpdata:%x, rest:%d\n",len,clen,metad, l,inpdata,rest );
	printf("mt len:%d, clen:%d, metad:%d ,&l:%x, l:%d, rest:%d, str: %s\n",len,clen,metad,inpdata+metad, l,rest,inpdata+metad+1 );
}
//else
//	printf("mt len:%d, clen:%d, metad:%d,&l:%x, l:%d, rest:%d\n",len,clen,metad,inpdata+metad, l,rest );
if (l > 80) dump(inpdata,len);
*/	
				if (l !=0)
				{
					if (rest <0)
					{
						*(inpdata+clen) = 0; //truncated
//printf("mtlen len:%d, clen:%d, metad:%d, l:%d, inpdata:%x,  rest:%d\n",len,clen,metad, l,inpdata,rest );				
						
						if (metadata != NULL) incfree(metadata,"meta"); 
						metadata = incmalloc(l+1);	
						strcpy(metadata,inpdata+metad+1);
					}
					else clientSaveMetadata(inpdata+metad+1,l);
				}	
				if (metad >0)
				{		
//if (getBufferFree() < metad) printf("metaout wait metad: %d, bufferfree: %d\n",metad,getBufferFree());			
					while(getBufferFree() < metad)	 // wait some room
						vTaskDelay(20);
					bufferWrite(inpdata, metad); 
				}
				metad  = header.members.single.metaint;
				inpdata = inpdata+clen-rest;
//if (rest <0) printf("mt1 len:%d, clen:%d, metad:%d, l:%d, inpdata:%x,  rest:%d\n",len,clen,metad, l,inpdata,rest );
				clen = rest;				
				if (rest <0) {clen = 0; break;}
			}	// while in buffer
			if (rest >=0)
			{	
				metad = header.members.single.metaint - rest ; //until next
				if (rest >0)
				{
//if (getBufferFree() < rest) printf("metaout wait rest: %d, bufferfree: %d\n",rest,getBufferFree());
					while(getBufferFree() < rest)						
						vTaskDelay(20);// 
					bufferWrite(inpdata, rest); 
				}
				rest = 0;
			}	
//printf("metaout len:%d, clen:%d, metad:%d, l:%d, inpdata:%x, rest:%d\n",len,clen,metad, l,inpdata,rest );						
		} else 
		{		
			if (header.members.single.metaint != 0) metad -= len;
//printf("out len = %d, metad = %d  metaint= %d, rest:%d\n",len,metad,header.members.single.metaint,rest);
			if (len >0) 
			{
//printf("metaout wait len: %d\n",len);					
				while(getBufferFree() < len) 
						vTaskDelay(20); 
				bufferWrite(pdata+rest, len);
			}			
		}
// ---------------			
		if (!playing )
		{
//printf("test memory: %d  on size %d\n",	(BUFFER_SIZE == BIGMEMORY)?(6*BIGMEMORY/10):(BUFFER_SIZE/2),BUFFER_SIZE);		
			if ( (getBufferFree() < ((BUFFER_SIZE == BIGMEMORY)?(6*BIGMEMORY/10):(BUFFER_SIZE/2))) ||(once ==1)) 
			{
				volume = VS1053_GetVolume();
				VS1053_SetVolume(0);
				playing=1;
				if (once == 0)vTaskDelay(20);
				VS1053_SetVolume(volume);
				kprintf(CLIPLAY,0x0d,0x0a);
				if (!ledStatus) gpio2_output_set(0);
			}	
		}
	}
}

#define VSTASKBUF	2048 //1024
uint8_t b[VSTASKBUF];
IRAM_ATTR void vsTask(void *pvParams) { 
//	portBASE_TYPE uxHighWaterMark;
	struct device_settings *device;
	uint16_t size ,s;
	VS1053_Start();
	
	device = getDeviceSettings();
	vTaskDelay(10);
	VS1053_SetVolume( device->vol);	
	VS1053_SetTreble(device->treble);
	VS1053_SetBass(device->bass);
	VS1053_SetTrebleFreq(device->freqtreble);
	VS1053_SetBassFreq(device->freqbass);
	VS1053_SetSpatial(device->spacial);
	incfree(device,"device");
	
	VS1053_SPI_SpeedUp();
	while(1) {
		if(playing) {			
			size = bufferRead(b, VSTASKBUF);
			s = 0; 			
			while(s < size) 
			{
				s += VS1053_SendMusicBytes(b+s, size-s);	
			}
			vTaskDelay(2);	
		} else 
		{
			vTaskDelay(30);		
//			uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
//			printf("watermark vstask: %x  %d\n",uxHighWaterMark,uxHighWaterMark);			
		}	
	}
}

//uint8_t bufrec[RECEIVE+10];

ICACHE_FLASH_ATTR void clientTask(void *pvParams) { 
	portBASE_TYPE uxHighWaterMark;
	struct timeval timeout; 
    timeout.tv_usec = 0;
	timeout.tv_sec = 10000; // bug *1000 for seconds
	int sockfd;
	int bytes_read;
	char *useragent;
	struct device_settings*device;
	struct sockaddr_in dest;
	uint8_t *bufrec;
	uint8_t cnterror;
	
	vTaskDelay(300);	

	bufrec = incmalloc(RECEIVE+20);
	useragent = incmalloc(50);
	
	device = getDeviceSettings();
	if (device != NULL)
	{
		strcpy(useragent,device->ua);
		if (strlen(useragent) == 0) 
		{
			strcpy(useragent,"Karadio/1.9");
			strcpy(device->ua,useragent);
		}	
		free(device);
	}
	

	//	portBASE_TYPE uxHighWaterMark;
//	clearHeaders();

//	uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
//	printf("watermark webclient:%d  heap:%d\n",uxHighWaterMark,xPortGetFreeHeapSize( ));
	
	while(1) {
		xSemaphoreGive(sConnected);
		if(xSemaphoreTake(sConnect, portMAX_DELAY)) {

			//VS1053_HighPower();
			xSemaphoreTake(sDisconnect, 0);	
			sockfd = socket(AF_INET, SOCK_STREAM, 0);
			if(sockfd >= 0) ; //printf("WebClient Socket created\n");
			else printf(strcWEBSOCKET,"create",errno);
			bzero(&dest, sizeof(dest));
			dest.sin_family = AF_INET;
			dest.sin_port = htons(clientPort);
			dest.sin_addr.s_addr = inet_addr(inet_ntoa(*(struct in_addr*)(server -> h_addr_list[0])));
			bytes_read = 0;
			/*---Connect to server---*/
			if(connect(sockfd, (struct sockaddr*)&dest, sizeof(dest)) >= 0) 
			{
//				printf("WebClient Socket connected\n");
				memset(bufrec,0, RECEIVE+20);
				
				char *t0 = strstr(clientPath, ".m3u");
				if (t0 == NULL)  t0 = strstr(clientPath, ".pls");
				if (t0 == NULL)  t0 = strstr(clientPath, ".xspf");				
				if (t0 == NULL)  t0 = strstr(clientPath, ".m3u8");	
				if (t0 == NULL)  t0 = strstr(clientPath, ".asx");					
				if (t0 != NULL)  // a playlist asked
				{
				  cstatus = C_PLAYLIST;
//printf("sprint%d\n",6);				  
				  kasprintf(bufrec,PSTR("GET %s HTTP/1.0\r\nHOST: %s\r\n\r\n"), clientPath,clientURL); //ask for the playlist
			    } 
				else 
				{
					if (strcmp(clientURL,"stream.pcradio.biz") ==0) strcpy(useragent,"pcradio");
//printf("sprint%d\n",7);					
					kasprintf(bufrec,PSTR("GET %s HTTP/1.1\r\nHost: %s\r\nicy-metadata: 1\r\nUser-Agent: %s\r\n\r\n"), clientPath,clientURL,useragent); 
				}
//printf("st:%d, Client Sent:\n%s\n",cstatus,bufrec);
				xSemaphoreTake(sConnected, 0);
				send(sockfd, bufrec, strlen(bufrec), 0);								
///// set timeout
/*				if (once == 0)
					timeout.tv_sec = 10000; // bug *1000 for seconds
				else

					timeout.tv_sec = 3000; // bug *1000 for seconds
*/
				if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
					printf(strcSOCKET,"setsockopt",errno);
//////			
				wsMonitor();
				cnterror = 0;		
				do
				{
					bytes_read = recvfrom(sockfd, bufrec,RECEIVE, 0, NULL, NULL);	
//if (bytes_read < 1000 )  
//printf("Rec:%d\n",bytes_read);
					if ( bytes_read < 0 )
					{
						kprintf(PSTR("Client socket: %d  read: %d, "),sockfd, bytes_read);
						if (errno == 11) bytes_read = 0;
					}

					if ( bytes_read > 0 )
					{
						cnterror = 0;
						clientReceiveCallback(sockfd,bufrec, bytes_read);
					}
					// buffer  empty?
					else 
					{	
						vTaskDelay(1);
						if (getBufferEmpty()) cnterror++;
						kprintf(PSTR("Errno: %d, %d%c"),errno,cnterror,0x0d);
//						if (errno != 11) vTaskDelay(50); //timeout 
//						else 
							vTaskDelay(20);
						if ((errno == 128)||(cnterror > 4)) break;
					}
					vTaskDelay(1);
					if(xSemaphoreTake(sDisconnect, 0)){ clearHeaders(); break;	}
				}
				while (( bytes_read > 0 )||(playing && (bytes_read == 0)));
			} else
			{
				printf(strcSOCKET,"connect", errno);
				clientSaveOneHeader("Invalid address",15,METANAME);	
				wsHeaders();
				vTaskDelay(1);
				clientDisconnect(PSTR("Invalid")); 
				close(sockfd);
				continue;
			}	
			/*---Clean up---*/
			if (bytes_read <= 0 )  //nothing received or error or disconnected
			{	
					if ((playing)&&(once == 0))  // try restart
					{
						clientDisconnect(PSTR("try restart")); 
						clientConnect();
						playing=1; // force
//						printf(CLIPLAY,0x0d,0x0a);
					}	
					else if ((!playing)&&(once == 1)){ // nothing played. Force the read of the buffer
						// some data not played						
						if ((!playing )&& (getBufferFree() < (BUFFER_SIZE))) {						
							playing=1;
							vTaskDelay(1);
							if (VS1053_GetVolume()==0) VS1053_SetVolume(volume);
							kprintf(CLIPLAY,0x0d,0x0a);
							while (!getBufferEmpty()) vTaskDelay(100);							
							vTaskDelay(150);
							playing=0;
							clientDisconnect(PSTR("data not played")); 
						}
					}						
						//						
					else if ((!playing)&&(once == 0)) {  // nothing received
							clientSaveOneHeader(notfound, 9,METANAME);
							wsHeaders();
							vTaskDelay(1);
							clientDisconnect(PSTR("not found")); 
							
					}	
					else{  //playing & once=1 and no more received stream
						while (!getBufferEmpty()) vTaskDelay(100);
						vTaskDelay(200);
						clientDisconnect(PSTR("once")); 						
					}					
			}//jpc
						
			// marker for heap size (debug)
/*			uxHighWaterMark = uxTaskGetStackHighWaterMark( NULL );
			printf(PSTR("watermark webclient :%d  heap:%d\n"),uxHighWaterMark,xPortGetFreeHeapSize( ));
*/
			if (playing)  // stop clean
			{		
				volume = VS1053_GetVolume();
				VS1053_SetVolume(0);
				bufferReset();
				VS1053_flush_cancel();
				playing = 0;
				vTaskDelay(40);	// stop without click
				//VS1053_LowPower();
				VS1053_SetVolume(volume);
			}	

			shutdown(sockfd,SHUT_RDWR); // stop the socket
			vTaskDelay(1);	
			close(sockfd);
			bufferReset();
			if (cstatus == C_PLAYLIST) 			
			{
			  clientConnect();
			}
		}
	}
}
