
#ifndef _C_TYPES_H_
#define _C_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __le16      u16

#define LOCAL       static

#ifndef NULL
#define NULL        (void *)0
#endif /* NULL */



#define SHMEM_ATTR

#ifdef ICACHE_FLASH
#define ICACHE_FLASH_ATTR __attribute__((section(".irom0.text")))
#else
#define ICACHE_FLASH_ATTR
#endif

#define DMEM_ATTR           __attribute__((section(".bss")))
#define IRAM_ATTR           __attribute__((section(".text")))
#define ICACHE_RODATA_ATTR  __attribute__((section(".irom.text")))

#ifndef __cplusplus
#define BOOL            bool
#define TRUE            true
#define FALSE           false
#endif /* !__cplusplus */

#ifdef __cplusplus
}
#endif

#endif /* _C_TYPES_H_ */
