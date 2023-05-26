# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES
  "mdns"
  "nvs_flash"
)
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS 
        "addon.c"
        "custom.c"
        "eeprom.c"
        "interface.c"
        "main.c"
        "ntp.c"
        "ota.c"
        "servers.c"
        "telnet.c"
        "timezone.c"
        "vs1053.c"
        "vs1053b-patches.c"
        "webclient.c"
        "webserver.c"
        "websocket.c"
)

set(COMPONENT_ADD_INCLUDEDIRS 
	"../include/"
	"../webpage/"
)

register_component()
