#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <stdint.h>
#include "stm32f4xx_hal.h"

/* ── Memory layout ───────────────────────────────────────────────────────────
 * Bootloader : sector 0  0x08000000  16 KB
 * App        : sector 1+ 0x08004000  up to 368 KB (sectors 1-6)
 * Fault NV   : sector 7  0x08060000  128 KB  (reserved, never touched here)
 */
#define APP_START_ADDR      0x08004000U
#define APP_END_ADDR        0x08060000U   /* exclusive — fault NV starts here */
#define APP_MAX_SIZE        (APP_END_ADDR - APP_START_ADDR)

/* ── CAN IDs ─────────────────────────────────────────────────────────────────*/
#define BL_RX_ID    0x7E0U   /* host → VCU */
#define BL_TX_ID    0x7E1U   /* VCU  → host */

/* ── Commands (host → VCU) ───────────────────────────────────────────────────*/
#define BL_CMD_PING     0x01U   /* DLC=1 */
#define BL_CMD_START    0x02U   /* DLC=5  [1-4] = total_size LE */
#define BL_CMD_DATA     0x03U   /* DLC=5  [1-4] = 4 bytes of firmware */
#define BL_CMD_CRC      0x04U   /* DLC=5  [1-4] = expected CRC32 LE */
#define BL_CMD_JUMP     0x05U   /* DLC=1 */

/* ── Responses (VCU → host) ──────────────────────────────────────────────────*/
#define BL_RESP_ACK     0x06U   /* DLC=1 */
#define BL_RESP_NACK    0x07U   /* DLC=2  [1] = error code */

/* ── Error codes ─────────────────────────────────────────────────────────────*/
#define BL_ERR_SIZE     0x01U   /* firmware too large */
#define BL_ERR_ERASE    0x02U   /* flash erase failed */
#define BL_ERR_WRITE    0x03U   /* flash write failed */
#define BL_ERR_CRC      0x04U   /* CRC mismatch */
#define BL_ERR_NOSTART  0x05U   /* DATA received before START */
#define BL_ERR_NOJUMP   0x06U   /* no valid app to jump to */

/* ── Timeout before jumping to app on power-up ───────────────────────────────*/
#define BL_WAIT_MS      2000U

void BL_Run(CAN_HandleTypeDef *hcan);

#endif /* BOOTLOADER_H */
