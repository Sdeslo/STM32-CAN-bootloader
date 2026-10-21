#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* Memory layout
 * Bootloader : sector 0  0x08000000  16 KB
 * App        : sector 1+ 0x08004000  up to 368 KB - 16 B (sectors 1-6)
 * Fault NV   : sector 7  0x08060000  128 KB  (reserved, never touched here)
 */
#define APP_START_ADDR      0x08004000U
#define APP_END_ADDR        0x08060000U   /* exclusive — fault NV starts here */

/* App-valid trailer, last 16 B of sector 6 */
typedef struct {
    uint32_t size;
    uint32_t crc;
    uint32_t reserved;
    uint32_t magic;
} bl_trailer_t;

#define BL_TRAILER_ADDR     (APP_END_ADDR - sizeof(bl_trailer_t))
#define BL_TRAILER_MAGIC    0xA55AC33CU
#define APP_MAX_SIZE        (BL_TRAILER_ADDR - APP_START_ADDR)

/* CAN IDs */
#define BL_RX_ID    0x7E0U   /* host → VCU */
#define BL_TX_ID    0x7E1U   /* VCU  → host */

/* Commands (host → STM32) */
#define BL_CMD_PING     0x01U   /* DLC=1 */
#define BL_CMD_START    0x02U   /* DLC=5  [1-4] = total_size LE (multiple of 4) */
#define BL_CMD_DATA     0x03U   /* DLC=8  [1-3] = word index LE, [4-7] = 4 bytes of firmware */
#define BL_CMD_CRC      0x04U   /* DLC=5  [1-4] = expected CRC32 LE */
#define BL_CMD_JUMP     0x05U   /* DLC=1 */

/* Responses (STM32 → host), byte 1 = command byte */
#define BL_RESP_ACK     0x06U
#define BL_RESP_NACK    0x07U
#define BL_RESP_PENDING 0x08U

/* Error codes */
#define BL_ERR_SIZE     0x01U
#define BL_ERR_ERASE    0x02U
#define BL_ERR_WRITE    0x03U
#define BL_ERR_CRC      0x04U
#define BL_ERR_NOSTART  0x05U
#define BL_ERR_NOJUMP   0x06U
#define BL_ERR_SEQ      0x07U
#define BL_ERR_DLC      0x08U

/* Timeout before jumping to app on cold boot (no RAM flag) */
#define BL_WAIT_MS      50U

/* Set by app before NVIC_SystemReset(); NOINIT region in the linker script */
#define BL_FLAG_ADDR    0x2001FFF0UL
#define BL_FLAG_MAGIC   0xDEADBEEFUL

void BL_Run(CAN_HandleTypeDef *hcan);

#endif /* BOOTLOADER_H */
