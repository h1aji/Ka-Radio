#include "c_types.h"

//#define CACHE_FLASH __attribute__((section(".irom0.rodata")))

struct servFile
{
	const char name[32];
	const char type[16];
	uint16_t size;
	const char* content;
	struct servFile *next;
};

//ICACHE_ICACHE_RODATA_ATTR STORE_ATTR 
#define ICACHE_STORE_TYPEDEF_ATTR __attribute__((aligned(4),packed))
#define ICACHE_STORE_ATTR __attribute__((aligned(4)))
#define ICACHE_RAM_ATTR __attribute__((section(".iram0.text")))

#include "../../webpage/index"
#include "../../webpage/style"
#include "../../webpage/style1"
#include "../../webpage/script"
#include "../../webpage/logo"
#include "../../webpage/favicon"

const struct servFile faviconFile = {
	"/favicon.png",
	"image/png",
	sizeof(favicon_png),
	favicon_png,
	(struct servFile*)NULL
};
const struct servFile logoFile = {
	"/logo.png",
	"image/png",
	sizeof(logo_png),
	logo_png,
	(struct servFile*)&faviconFile
};

const struct servFile scriptFile = {
	"/script.js",
	"text/javascript",
	sizeof(script_js),
	script_js,
	(struct servFile*)&logoFile
};

const struct servFile styleFile = {
	"/style.css",
	"text/css",
	sizeof(style_css),
	style_css,
	(struct servFile*)&scriptFile
};

const struct servFile styleFile1 = {
	"/style1.css",
	"text/css",
	sizeof(style1_css),
	style1_css,
	(struct servFile*)&styleFile
};
const struct servFile indexFile = {
	"/",
	"text/html",
	sizeof(index_html),
	index_html,
	(struct servFile*)&styleFile1
};
