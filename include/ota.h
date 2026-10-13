/*
 * Copyright 2016 jp Cocatrix (http://www.karawin.fr)
 *
 * Over-the-air firmware update (user/ota.c).
 *
 * The flash writer and the download task are internal to ota.c; the SDK
 * prototypes they implement (system_upgrade_init, system_upgrade,
 * system_upgrade_deinit, system_upgrade_start) come from the SDK upgrade.h.
 */

#ifndef OTA_H
#define OTA_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Start an over-the-air firmware update: download the image into the slot
 * that is not running, check its CRC and reboot into it.
 * Progress and the result are sent to the web page with the websocket
 * message {"upgrade":"..."} and to the console.
 *
 *   fname: release to download, "new" (release) or "prv" (prerelease)
 */
void update_firmware(char *fname);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */
