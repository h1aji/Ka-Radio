# ESP8266 Web Radio

Karadio is a WiFi shoutcast player based on esp8266 nodemcu and vs1053b chips.
This repo is a fork of [Ka-Radio](https://github.com/karawin/Ka-Radio)
I try to adjust the code and add extra functionality.

Please note: project is currently under developement

### Installation

Karadio requires [ESP8266 RTOS SDK](https://github.com/espressif/ESP8266_RTOS_SDK). Installation guide can be found on the main page.

Install python modules required for web interface build

```sh
sudo pip3 install rjsmin
sudo pip3 install rcsmin
```


Flash new image

```sh
./flash.sh
```


### Wiring
```
ESP pin   - VS1053 pin
------------------------
GPIO14    - SCK
GPIO12    - MISO
GPIO13    - MOSI
GPIO16    - DCS
GPIO10    - DREQ
GPIO00    - CS
```

RST has no GPIO of its own. It is the diode-OR of CS and DCS with a 1k pull-down,
so the VS1053 is in reset only while both CS and DCS are low:
```
GPIO00 (CS)  --|>|--+
                     +-- VS1053 RST --+
GPIO16 (DCS) --|>|--+                 |
                                     1k
                                      |
                                     GND
```
Use signal diodes (e.g. 1N4148) for a higher RST level.

Optional external RAM 23LCV1024 supported

```
ESP pin   - 23LC1024 pin
------------------------
GPIO15    - CS   (1)
GPIO12    - MISO (2)
          - SIO2 (3) *
GND       - GND  (4)
GPIO13    - MOSI (5)
GPIO14    - SCK  (6)
          - SIO3 (7) *
3.3V      - VCC  (8)

*=optional, may also be connected to Vcc on 23LC1024 side.
```

External RAM is detected at boot time if connected.


Infra-red sensor

```
GPIO02  - OUT
```

A NEC receiver module (TSOP, VS1838...) powered from 3.3V. The pin must be high at power-up,
which the idle level of the module gives. The remote is the one of `ir.arduino`:

```
Key       Command
------------------------------------------------------
0..9      type a station number (up to 3 digits), played 1.5 s after the last digit
|<<       cli.previous
>>|       cli.next
PLAY      cli.start (nothing when already playing)
STOP      cli.stop
+         cli.vol+   (repeats while held)
-         cli.vol-   (repeats while held)
MUTE      mute / restore the volume
```

An unknown key prints `##IR.KEY#: <code>`, to add a key of another remote.

LCD

```
GPIO04  - SDA
GPIO05  - SCL
```

### Roadmap

 - Review firmware update process
 - Setup Github CI/CD
 - Adjust web interface for mobile devices

License
----

ESPRESSIF MIT

