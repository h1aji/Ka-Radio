I've corrected the spelling and grammar, improved sentence structure, standardized command descriptions and formatting, and converted the document into Markdown.

I've preserved the original command names, syntax, examples, protocol messages, and technical values. I've also retained the original behavior descriptions rather than changing implementation details.

# Wi-Fi Webradio Interfaces

## 1. HTTP Interface

A web browser or `curl` can be used to send basic commands to the Wi-Fi Webradio.

### Request format

```
http://yourip/?command[&command]
```

If a command returns information, it must be the last command in the request or sent on its own.

### Available commands

| Command                     | Description                                                                                 |
| --------------------------- | ------------------------------------------------------------------------------------------- |
| `version`                   | Returns the software release and revision: `Release: x.x, Revision: x`.                     |
| `infos`                     | Returns information about the current playback status.                                      |
| `list=xxx`                  | Returns the name of station `xxx` (0–254).                                                  |
| `stop`                      | Stops playback of the current station.                                                      |
| `start`                     | Starts playback of the current station.                                                     |
| `play=xxx`                  | Plays station `xxx` (0–254).                                                                |
| `volume=xxx`                | Sets the volume to `xxx` (0–254).                                                           |
| `uart`                      | Sets the UART baud rate to 115200. This setting is not saved and will be lost after reboot. |
| `instant="http://your_url"` | Immediately plays the specified stream URL.                                                 |

### Playback information

The `infos` command returns information in the following format:

```
vol: 46
num: 240
stn: Hotmix 80
tit: BANANARAMA - Venus
sts: 1
```

Where:

| Field | Description                                 |
| ----- | ------------------------------------------- |
| `vol` | Current volume.                             |
| `num` | Current station number.                     |
| `stn` | Name of the current station.                |
| `tit` | Title of the currently playing song.        |
| `sts` | Player state: `0` = stopped, `1` = playing. |

### Instant playback

The `instant` command allows a stream to be played without adding it to the station list.

Example:

```
http://192.168.1.253/?instant="http://airspectrum.cdnstream1.com:8114/1648_128"
```

The URL must:

- Start with `http://` or `https://`.
- Be enclosed in double quotation marks (`"`).
- Contain no spaces.

### Combining commands

The volume command can be combined with other commands.

Example:

```
http://192.168.1.253/?volume=50&play=128&infos
```

This sets the volume to 50, plays station 128, and returns the current playback information.

**Note:** Accessing the URL without any parameters opens the full web interface.

---

## 2. UART Interface

The UART interface can be used both to receive events from the Webradio and to send commands to it.

### 2.1. Example event notifications

The following examples illustrate the messages sent by the Webradio over UART.

#### Station playback events

```
##CLI.STOPPED# from playStationInt
##CLI.NAMESET#: 88 Public Domain Classical - Swiss Internet Radio
##CLI.URLSET#: 82.197.165.137
##CLI.PATHSET#: /
##CLI.PORTSET#: 80
##CLI.OVOLSET#: 0
##CLI.VOL#: 110
##CLI.ICY0#: Public Domain Classical - Swiss Internet Radio - Shoutcast Streaming Technology by www.init7.net
##CLI.ICY1#: <BR>This stream requires <a href="http://www.winamp.com/">Winamp</a><BR>
##CLI.ICY2#: SHOUTcast Distributed Network Audio Server/Linux v1.9.8<BR>
##CLI.ICY3#: http://publicdomainproject.org
##CLI.ICY4#: Classical
##CLI.ICY5#: 128
##CLI.META#: Felix Mendelssohn-Bartholdy - String Quartet in E flat major Op. 44 No.3: 4. Molto allegro con fuoco
##CLI.PLAYING#
```

Another example:

```
##CLI.STOPPED# from playStationInt
##CLI.NAMESET#: 92 ALBUMROCK.MIAMI
##CLI.URLSET#: uk4.internet-radio.com
##CLI.PATHSET#: /
##CLI.PORTSET#: 8081
##CLI.OVOLSET#: 0
##CLI.VOL#: 110
##CLI.ICY0#: ALBUMROCK.MIAMI
##CLI.ICY1#: <BR>This stream requires <a href="http://www.winamp.com/">Winamp</a><BR>
##CLI.ICY2#: SHOUTcast Distributed Network Audio Server/Linux v1.9.8<BR>
##CLI.ICY3#: www.AlbumRock.Miami
##CLI.ICY4#: Classic Rock 60s 70s 80s Miami Florida Album Rock
##CLI.ICY5#: 128
##CLI.META#: The Who - Eminence Front
##CLI.PLAYING#
```

#### Station list events

```
#CLI.LIST#
#CLI.LISTINFO#:   2: RTL2, streaming.radio.rtl.fr:80/rtl2-1-44-128.m3u
##CLI.LIST#
```

#### Client information events

When the `cli.info` command is issued, the Webradio responds with information about the current station, stream metadata, volume, and playback status.

Example:

```
##CLI.NAMESET#: 92 ALBUMROCK.MIAMI
##CLI.ICY0#: ALBUMROCK.MIAMI
##CLI.ICY1#: <BR>This stream requires <a href="http://www.winamp.com/">Winamp</a><BR>
##CLI.ICY2#: SHOUTcast Distributed Network Audio Server/Linux v1.9.8<BR>
##CLI.ICY3#: www.AlbumRock.Miami
##CLI.ICY4#: Classic Rock 60s 70s 80s Miami Florida Album Rock
##CLI.ICY5#: 128
##CLI.ICY6#:
##CLI.ICY7#:
##CLI.META#: The Who - Eminence Front
##CLI.VOL#: 108
##CLI.PLAYING#
##CLI.META#: The Beatles - Mother Nature's Son
##SYS.TZO#: 1
##SYS.DATE#: 2017-04-13T22:47:44+01:00
```

### 2.2. ICY metadata fields

The `ICYx` fields correspond to the following HTTP stream metadata headers:

| Field  | Metadata header    |
| ------ | ------------------ |
| `ICY0` | `icy-name:`        |
| `ICY1` | `icy-notice1:`     |
| `ICY2` | `icy-notice2:`     |
| `ICY3` | `icy-url:`         |
| `ICY4` | `icy-genre:`       |
| `ICY5` | `icy-br:`          |
| `ICY6` | `icy-description:` |
| `ICY7` | `ice-audio-info:`  |

Other messages may also be printed for debugging purposes. However, useful events always start with `##`.

---

## 3. UART Commands

Commands are entered through the UART interface.

### 3.1. Wi-Fi commands

These commands manage the wireless network connection.

| Command                       | Description                                                                 |
| ----------------------------- | --------------------------------------------------------------------------- |
| `wifi.list`                   | Displays the list of detected Wi-Fi SSIDs.                                  |
| `wifi.con`                    | Displays the configured AP1 and AP2 SSIDs.                                  |
| `wifi.con("ssid","password")` | Saves the specified SSID and password as AP1 for use after the next reboot. |
| `wifi.discon`                 | Disconnects from the currently connected Wi-Fi network.                     |
| `wifi.station`                | Displays the current SSID and password.                                     |
| `wifi.status`                 | Displays the current Wi-Fi connection status and network configuration.     |

The `wifi.status` response has the following format:

```
#WIFI.STATUS#
5
192.168.1.40
255.255.255.0
192.168.1.70
##WIFI.STATUS#
```

The fields are:

1. `#WIFI.STATUS#` — Start of the response.
2. Connection state.
3. Current IP address.
4. Network subnet mask.
5. Default gateway.
6. `##WIFI.STATUS#` — End of the response.

### 3.2. Station client commands

These commands control station playback and stream configuration.

#### Instant playback

| Command             | Description                                                   |
| ------------------- | ------------------------------------------------------------- |
| `cli.url("url")`    | Sets the station hostname or IP address for instant playback. |
| `cli.path("/path")` | Sets the stream path for instant playback.                    |
| `cli.port("xxxx")`  | Sets the port number for instant playback.                    |
| `cli.instant`       | Starts playback of the configured instant stream.             |

#### Station playback

| Command                      | Description                                                                           |
| ---------------------------- | ------------------------------------------------------------------------------------- |
| `cli.start`                  | Starts playback of the current station.                                               |
| `cli.play("xxx")`            | Plays the specified station from the recorded station list. Use `0` to stop playback. |
| `cli.prev` or `cli.previous` | Selects and plays the previous station in the list.                                   |
| `cli.next`                   | Selects and plays the next station in the list.                                       |
| `cli.stop`                   | Stops the currently playing station or instant stream.                                |

#### Station list

| Command         | Description                                                                               |
| --------------- | ----------------------------------------------------------------------------------------- |
| `cli.list`      | Lists all recorded stations.                                                              |
| `cli.list("x")` | Displays information about a single recorded station. The response uses `#CLI.LISTINFO#`. |

#### Volume control

| Command          | Description                                                          |
| ---------------- | -------------------------------------------------------------------- |
| `cli.vol("xxx")` | Sets the volume to `xxx` (0–254, maximum volume).                    |
| `cli.vol`        | Returns the current volume. The response format is `##CLI.VOL# xxx`. |
| `cli.vol-`       | Decreases the volume by 10.                                          |
| `cli.vol+`       | Increases the volume by 10.                                          |

**Note:** Every volume command, whether received through UART or HTTP, generates a response in the following format:

```
##CLI.VOL#: xxx
```

#### Client information

| Command    | Description                                                                                                                       |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------- |
| `cli.info` | Returns the station name, all available ICY metadata, current song information, volume, and playback status (playing or stopped). |

This command is primarily intended to refresh the LCD display.

See the Client information events example above.

### 3.3. System commands

These commands configure and manage the Webradio system.

#### UART configuration

| Command         | Description                                                                     |
| --------------- | ------------------------------------------------------------------------------- |
| `sys.uart("x")` | Changes the UART baud rate. The new setting takes effect after the next reboot. |

Supported baud rates:

```
1200, 2400, 4800, 9600, 14400, 19200,
28800, 38400, 57600, 76880, 115200, 230400
```

#### I2S configuration

| Command        | Description                                                                                        |
| -------------- | -------------------------------------------------------------------------------------------------- |
| `sys.i2s`      | Displays the current I2S clock speed.                                                              |
| `sys.i2s("x")` | Changes and saves the I2S clock speed on VS1053 GPIO5 (MCLK), used for the external DAC interface. |

Supported values:

| Value           | Clock speed |
| --------------- | ----------- |
| `0`             | 48 kHz      |
| `1`             | 96 kHz      |
| `2`             | 192 kHz     |
| Any other value | 48 kHz      |

#### System maintenance

| Command          | Description                                                              |
| ---------------- | ------------------------------------------------------------------------ |
| `sys.erase`      | Erases all saved configuration data and recorded stations.               |
| `sys.heap`       | Displays the available RAM heap size.                                    |
| `sys.update`     | Starts an OTA (Over-The-Air) software update.                            |
| `sys.prerelease` | Starts an OTA update using the prerelease firmware for testing purposes. |
| `sys.boot`       | Reboots the Webradio.                                                    |

#### VS1053 patch configuration

| Command          | Description                                          |
| ---------------- | ---------------------------------------------------- |
| `sys.patch("x")` | Changes the VS1053 patch loading setting at startup. |
| `sys.patch`      | Displays the current VS1053 patch status.            |

Supported values:

- `0` — Do not load the patch.
- `1` or greater — Load the patch at startup (default).

#### LED configuration

| Command        | Description                               |
| -------------- | ----------------------------------------- |
| `sys.led("x")` | Changes the LED indication mode.          |
| `sys.led`      | Displays the current LED indication mode. |

Supported values:

- `0` — Playback mode: the LED lights up while a station is playing.
- `1` or greater — Blink mode (default).

#### Time and date configuration

| Command         | Description                                                        |
| --------------- | ------------------------------------------------------------------ |
| `sys.tzo("xx")` | Sets the local time zone offset.                                   |
| `sys.tzo`       | Displays the configured time zone offset.                          |
| `sys.date`      | Sends an NTP request and displays the current local date and time. |

The date and time are returned in ISO 8601 format:

```
YYYY-MM-DDThh:mm:ssTZD
```

Example:

```
2017-07-16T19:20:30+01:00
```

For reference, see the [ISO 8601 date and time format](https://www.w3.org/TR/NOTE-datetime).

#### Additional commands

| Command   | Description                                           |
| --------- | ----------------------------------------------------- |
| `sys.adc` | Reads the current ADC input value.                    |
| `sys.log` | Performs no operation. Used for debugging the add-on. |

### 3.4. Other commands

| Command   | Description                                   |
| --------- | --------------------------------------------- |
| `<enter>` | Displays the information header: `#INFO:""#`. |
| `help`    | Lists all available commands.                 |

### 3.5. Command errors

When an invalid command or command error occurs, the following response is displayed:

```
##CMD_ERROR#
```

A few technical details worth checking against the firmware: I preserved the baud-rate value `76880` exactly as supplied, although it may be a typo for `76800`. I also preserved the different `#CLI.LIST#` and `##CLI.LIST#` prefixes and the `icy-audio-info` spelling exactly as provided. These may be intentional protocol details, so I haven't silently changed them.