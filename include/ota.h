/*
 * Copyright 2016 jp Cocatrix (http://www.karawin.fr)
*/

#ifndef OTA_H
#define OTA_H

/*
 * Start an over-the-air firmware update.
 *   fname: release to download, "new" (release) or "prv" (prerelease)
 */
void update_firmware(char *fname);

#endif
