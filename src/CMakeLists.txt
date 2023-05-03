# Edit following two lines to set component requirements (see docs)
set(COMPONENT_REQUIRES "")
set(COMPONENT_PRIV_REQUIRES )

set(COMPONENT_SRCS 
        "buffer.c"
        "cdecode.c"
        "cencode.c"
        "eeprom.c"
        "el_uart.c"
        "extram.c"
        "flash.c"
        "gpio16.c"
        "interface.c"
        "main.c"
        "ntp.c"
        "ota.c"
        "servers.c"
        "telnet.c"
        "timezone.c"
        "upgrade.c"
        "upgrade_crc32.c"
        "upgrade_lib.c"
        "vs1053b-patches.c"
        "vs1053.c"
        "webclient.c"
        "webserver.c"
        "websocket.c"
)

set(COMPONENT_ADD_INCLUDEDIRS 
	"../include/"
	"../webpage/"
)

register_component()
