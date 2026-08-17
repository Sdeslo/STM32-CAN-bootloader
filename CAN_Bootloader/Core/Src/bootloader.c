#include "bootloader.h"
#include <string.h>

/* ── Helpers ─────────────────────────────────────────────────────────────────*/

static CAN_HandleTypeDef *s_hcan;
static CAN_TxHeaderTypeDef s_tx_hdr = {
    .StdId              = BL_TX_ID,
    .IDE                = CAN_ID_STD,
    .RTR                = CAN_RTR_DATA,
    .TransmitGlobalTime = DISABLE,
};

static void send_ack(void)
{
    uint8_t data = BL_RESP_ACK;
    uint32_t mailbox;
    s_tx_hdr.DLC = 1;
    HAL_CAN_AddTxMessage(s_hcan, &s_tx_hdr, &data, &mailbox);
}

static void send_nack(uint8_t err)
{
    uint8_t data[2] = { BL_RESP_NACK, err };
    uint32_t mailbox;
    s_tx_hdr.DLC = 2;
    HAL_CAN_AddTxMessage(s_hcan, &s_tx_hdr, data, &mailbox);
}

/* ── Flash helpers ───────────────────────────────────────────────────────────*/

/* Returns FLASH_SECTOR_x for an address inside the app region (sectors 1-6). */
static uint32_t addr_to_sector(uint32_t addr)
{
    if      (addr < 0x08008000U) return FLASH_SECTOR_1;   /* 16 KB */
    else if (addr < 0x0800C000U) return FLASH_SECTOR_2;   /* 16 KB */
    else if (addr < 0x08010000U) return FLASH_SECTOR_3;   /* 16 KB */
    else if (addr < 0x08020000U) return FLASH_SECTOR_4;   /* 64 KB */
    else if (addr < 0x08040000U) return FLASH_SECTOR_5;   /* 128 KB */
    else                         return FLASH_SECTOR_6;   /* 128 KB */
}

static HAL_StatusTypeDef erase_app_sectors(void)
{
    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,   /* 2.7–3.6 V */
        .Sector       = FLASH_SECTOR_1,
        .NbSectors    = 6,                       /* sectors 1-6 */
    };
    uint32_t bad_sector;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&erase, &bad_sector);
    HAL_FLASH_Lock();
    return st;
}

/* Write exactly 4 bytes (one word) to flash. Caller must unlock. */
static HAL_StatusTypeDef flash_write_word(uint32_t addr, uint32_t word)
{
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
}

/* ── Simple CRC-32 (no hardware peripheral needed in bootloader) ─────────────*/

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    static const uint32_t table[16] = {
        0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
        0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
        0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
        0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU,
    };
    crc = ~crc;
    while (len--) {
        crc = (crc >> 4) ^ table[(crc ^ *data)       & 0x0FU];
        crc = (crc >> 4) ^ table[(crc ^ (*data >> 4)) & 0x0FU];
        data++;
    }
    return ~crc;
}

/* ── Jump to application ─────────────────────────────────────────────────────*/

static int app_is_valid(void)
{
    /* Stack pointer must point inside RAM */
    uint32_t sp = *(volatile uint32_t *)APP_START_ADDR;
    return (sp >= 0x20000000U && sp <= 0x20020000U);
}

static void jump_to_app(void)
{
    /* Disable all interrupts and systick */
    __disable_irq();
    SysTick->CTRL = 0;

    /* Clear pending IRQs */
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    /* Relocate vector table */
    SCB->VTOR = APP_START_ADDR;

    /* Load app stack pointer and reset handler */
    uint32_t sp  = *(volatile uint32_t *)(APP_START_ADDR);
    uint32_t pc  = *(volatile uint32_t *)(APP_START_ADDR + 4U);

    /* Set stack pointer and branch to reset handler via function pointer */
    __set_MSP(sp);
    void (*app_reset)(void) = (void (*)(void))pc;
    app_reset();
    /* never returns */
}

/* ── CAN filter: accept only BL_RX_ID ───────────────────────────────────────*/

static void setup_can_filter(void)
{
    CAN_FilterTypeDef f = {
        .FilterIdHigh         = BL_RX_ID << 5,
        .FilterIdLow          = 0,
        .FilterMaskIdHigh     = 0x7FFU << 5,
        .FilterMaskIdLow      = 0,
        .FilterFIFOAssignment = CAN_RX_FIFO0,
        .FilterBank           = 0,
        .FilterMode           = CAN_FILTERMODE_IDMASK,
        .FilterScale          = CAN_FILTERSCALE_32BIT,
        .FilterActivation     = CAN_FILTER_ENABLE,
        .SlaveStartFilterBank = 14,
    };
    HAL_CAN_ConfigFilter(s_hcan, &f);
}

/* ── Main bootloader entry point ─────────────────────────────────────────────*/

void BL_Run(CAN_HandleTypeDef *hcan)
{
    s_hcan = hcan;
    setup_can_filter();
    HAL_CAN_Start(hcan);

    /* State */
    uint32_t write_addr   = APP_START_ADDR;
    uint32_t total_size   = 0;
    uint32_t bytes_rx     = 0;
    uint32_t running_crc  = 0;
    int      flash_active = 0;
    int      flash_unlocked = 0;

    uint32_t deadline = HAL_GetTick() + BL_WAIT_MS;

    CAN_RxHeaderTypeDef rx_hdr;
    uint8_t rx_data[8];

    while (1) {
        /* ── Check for timeout → jump if no flashing in progress ──────────── */
        if (!flash_active && HAL_GetTick() > deadline) {
            if (app_is_valid()) {
                if (flash_unlocked) HAL_FLASH_Lock();
                HAL_CAN_Stop(hcan);
                jump_to_app();
            }
            /* No valid app — stay in bootloader waiting for a flash */
            deadline = HAL_GetTick() + BL_WAIT_MS;
        }

        /* ── Poll for CAN frame ────────────────────────────────────────────── */
        if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) == 0)
            continue;

        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_hdr, rx_data) != HAL_OK)
            continue;

        uint8_t cmd = rx_data[0];

        switch (cmd) {

        /* ── PING ────────────────────────────────────────────────────────── */
        case BL_CMD_PING:
            send_ack();
            break;

        /* ── START: erase app, prepare for data ──────────────────────────── */
        case BL_CMD_START:
            if (rx_hdr.DLC < 5) { send_nack(BL_ERR_NOSTART); break; }
            total_size = (uint32_t)rx_data[1]
                       | ((uint32_t)rx_data[2] << 8)
                       | ((uint32_t)rx_data[3] << 16)
                       | ((uint32_t)rx_data[4] << 24);

            if (total_size == 0 || total_size > APP_MAX_SIZE) {
                send_nack(BL_ERR_SIZE);
                break;
            }
            if (erase_app_sectors() != HAL_OK) {
                send_nack(BL_ERR_ERASE);
                break;
            }
            HAL_FLASH_Unlock();
            flash_unlocked = 1;
            write_addr    = APP_START_ADDR;
            bytes_rx      = 0;
            running_crc   = 0;
            flash_active  = 1;
            send_ack();
            break;

        /* ── DATA: write 4 bytes to flash ────────────────────────────────── */
        case BL_CMD_DATA:
            if (!flash_active) { send_nack(BL_ERR_NOSTART); break; }
            if (rx_hdr.DLC < 5) { send_nack(BL_ERR_WRITE); break; }

            {
                uint32_t word = (uint32_t)rx_data[1]
                              | ((uint32_t)rx_data[2] << 8)
                              | ((uint32_t)rx_data[3] << 16)
                              | ((uint32_t)rx_data[4] << 24);

                if (flash_write_word(write_addr, word) != HAL_OK) {
                    send_nack(BL_ERR_WRITE);
                    break;
                }
                running_crc = crc32_update(running_crc, &rx_data[1], 4);
                write_addr += 4;
                bytes_rx   += 4;
            }
            send_ack();
            break;

        /* ── CRC: verify written flash ───────────────────────────────────── */
        case BL_CMD_CRC:
            if (!flash_active) { send_nack(BL_ERR_NOSTART); break; }
            if (rx_hdr.DLC < 5) { send_nack(BL_ERR_CRC); break; }

            {
                uint32_t expected = (uint32_t)rx_data[1]
                                  | ((uint32_t)rx_data[2] << 8)
                                  | ((uint32_t)rx_data[3] << 16)
                                  | ((uint32_t)rx_data[4] << 24);

                if (running_crc != expected) {
                    send_nack(BL_ERR_CRC);
                    break;
                }
            }
            HAL_FLASH_Lock();
            flash_unlocked = 0;
            flash_active   = 0;
            send_ack();
            break;

        /* ── JUMP: reset into app ────────────────────────────────────────── */
        case BL_CMD_JUMP:
            if (!app_is_valid()) { send_nack(BL_ERR_NOJUMP); break; }
            send_ack();
            HAL_Delay(10);   /* let ACK transmit before cutting power to CAN */
            if (flash_unlocked) HAL_FLASH_Lock();
            HAL_CAN_Stop(hcan);
            jump_to_app();
            break;

        default:
            break;
        }
    }
}
