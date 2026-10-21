#include "bootloader.h"
#include <stddef.h>


static int bl_flag_is_set(void)
{
    uint32_t flag = *(volatile uint32_t *)BL_FLAG_ADDR;
    *(volatile uint32_t *)BL_FLAG_ADDR = 0;
    return (flag == BL_FLAG_MAGIC);
}


static CAN_HandleTypeDef *s_hcan;
static CAN_TxHeaderTypeDef s_tx_hdr = {
    .StdId              = BL_TX_ID,
    .IDE                = CAN_ID_STD,
    .RTR                = CAN_RTR_DATA,
    .TransmitGlobalTime = DISABLE,
};

static void send_frame(const uint8_t *data, uint32_t dlc)
{
    uint32_t mailbox;
    uint32_t t0 = HAL_GetTick();
    while (HAL_CAN_GetTxMailboxesFreeLevel(s_hcan) == 0) {
        if (HAL_GetTick() - t0 > 10U) return;
    }
    s_tx_hdr.DLC = dlc;
    HAL_CAN_AddTxMessage(s_hcan, &s_tx_hdr, (uint8_t *)data, &mailbox);
}

static void wait_tx_done(void)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_CAN_GetTxMailboxesFreeLevel(s_hcan) < 3U) {
        if (HAL_GetTick() - t0 > 10U) return;
    }
}

static void send_ack(uint8_t cmd)
{
    uint8_t data[2] = { BL_RESP_ACK, cmd };
    send_frame(data, 2);
}

static void send_ack_data(uint32_t idx)
{
    uint8_t data[5] = { BL_RESP_ACK, BL_CMD_DATA,
                        (uint8_t)idx, (uint8_t)(idx >> 8), (uint8_t)(idx >> 16) };
    send_frame(data, 5);
}

static void send_nack(uint8_t cmd, uint8_t err)
{
    uint8_t data[3] = { BL_RESP_NACK, cmd, err };
    send_frame(data, 3);
}

static void send_nack_seq(uint32_t next_idx)
{
    uint8_t data[6] = { BL_RESP_NACK, BL_CMD_DATA, BL_ERR_SEQ,
                        (uint8_t)next_idx, (uint8_t)(next_idx >> 8), (uint8_t)(next_idx >> 16) };
    send_frame(data, 6);
}

static void send_pending(uint8_t cmd)
{
    uint8_t data[2] = { BL_RESP_PENDING, cmd };
    send_frame(data, 2);
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}


static uint32_t addr_to_sector(uint32_t addr)
{
    if      (addr < 0x08008000U) return FLASH_SECTOR_1;
    else if (addr < 0x0800C000U) return FLASH_SECTOR_2;
    else if (addr < 0x08010000U) return FLASH_SECTOR_3;
    else if (addr < 0x08020000U) return FLASH_SECTOR_4;
    else if (addr < 0x08040000U) return FLASH_SECTOR_5;
    else                         return FLASH_SECTOR_6;
}

static HAL_StatusTypeDef erase_sector(uint32_t sector)
{
    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,
        .Sector       = sector,
        .NbSectors    = 1,
    };
    uint32_t bad_sector;
    return HAL_FLASHEx_Erase(&erase, &bad_sector);
}

/* Trailer sector first, so the current app is invalidated before anything else. */
static HAL_StatusTypeDef erase_app_sectors(uint32_t size)
{
    uint32_t trailer_sector = addr_to_sector(BL_TRAILER_ADDR);
    uint32_t last_sector    = addr_to_sector(APP_START_ADDR + size - 1U);

    if (erase_sector(trailer_sector) != HAL_OK) return HAL_ERROR;
    send_pending(BL_CMD_START);

    for (uint32_t s = FLASH_SECTOR_1; s <= last_sector; s++) {
        if (s == trailer_sector) continue;
        if (erase_sector(s) != HAL_OK) return HAL_ERROR;
        send_pending(BL_CMD_START);
    }
    return HAL_OK;
}

static HAL_StatusTypeDef flash_write_word(uint32_t addr, uint32_t word)
{
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word);
}

static void flash_flush_dcache(void)
{
    __HAL_FLASH_DATA_CACHE_DISABLE();
    __HAL_FLASH_DATA_CACHE_RESET();
    __HAL_FLASH_DATA_CACHE_ENABLE();
}

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

static uint32_t flash_crc(uint32_t size)
{
    return crc32_update(0, (const uint8_t *)APP_START_ADDR, size);
}

/* Magic word last. */
static HAL_StatusTypeDef write_trailer(uint32_t size, uint32_t crc)
{
    if (flash_write_word(BL_TRAILER_ADDR + offsetof(bl_trailer_t, size), size) != HAL_OK)
        return HAL_ERROR;
    if (flash_write_word(BL_TRAILER_ADDR + offsetof(bl_trailer_t, crc), crc) != HAL_OK)
        return HAL_ERROR;
    return flash_write_word(BL_TRAILER_ADDR + offsetof(bl_trailer_t, magic), BL_TRAILER_MAGIC);
}

static int app_is_valid(void)
{
    const volatile bl_trailer_t *t = (const volatile bl_trailer_t *)BL_TRAILER_ADDR;
    uint32_t size = t->size;

    if (t->magic != BL_TRAILER_MAGIC) return 0;
    if (size < 8U || size > APP_MAX_SIZE || (size & 3U)) return 0;

    uint32_t sp = *(volatile uint32_t *)APP_START_ADDR;
    uint32_t pc = *(volatile uint32_t *)(APP_START_ADDR + 4U);
    if (sp < 0x20000000U || sp > 0x20020000U || (sp & 3U)) return 0;
    if (!(pc & 1U) || pc < APP_START_ADDR || pc >= APP_START_ADDR + size) return 0;

    return flash_crc(size) == t->crc;
}

static void jump_to_app(void)
{
    wait_tx_done();
    HAL_CAN_Stop(s_hcan);
    HAL_FLASH_Lock();

    /* Needs HAL_GetTick, so before __disable_irq() */
    HAL_RCC_DeInit();

    __disable_irq();

    HAL_DeInit();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;

    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }

    SCB->VTOR = APP_START_ADDR;
    __DSB();
    __ISB();

    uint32_t sp  = *(volatile uint32_t *)(APP_START_ADDR);
    uint32_t pc  = *(volatile uint32_t *)(APP_START_ADDR + 4U);

    /* No stack access allowed once MSP changes */
    __asm volatile (
        "msr msp, %0  \n"
        "cpsie i      \n"
        "bx  %1       \n"
        : : "r" (sp), "r" (pc) : "memory");
    __builtin_unreachable();
}

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

void BL_Run(CAN_HandleTypeDef *hcan)
{
    s_hcan = hcan;

    int flash_requested = bl_flag_is_set();
    int app_ok          = app_is_valid();

    setup_can_filter();
    HAL_CAN_Start(hcan);

    uint32_t total_size   = 0;
    uint32_t next_idx     = 0;
    int      flash_active = 0;

    int      stay   = flash_requested || !app_ok;
    uint32_t t_boot = HAL_GetTick();

    CAN_RxHeaderTypeDef rx_hdr;
    uint8_t rx_data[8];

    while (1) {
        if (!stay && (HAL_GetTick() - t_boot) >= BL_WAIT_MS)
            jump_to_app();

        if (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) == 0)
            continue;

        if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_hdr, rx_data) != HAL_OK)
            continue;

        if (rx_hdr.DLC == 0)
            continue;

        uint8_t cmd = rx_data[0];
        stay = 1;

        switch (cmd) {

        case BL_CMD_PING:
            send_ack(cmd);
            break;

        case BL_CMD_START:
            if (rx_hdr.DLC < 5) { send_nack(cmd, BL_ERR_DLC); break; }
            total_size = rd_le32(&rx_data[1]);

            if (total_size == 0 || total_size > APP_MAX_SIZE || (total_size & 3U)) {
                send_nack(cmd, BL_ERR_SIZE);
                break;
            }
            app_ok       = 0;
            flash_active = 0;
            HAL_FLASH_Unlock();
            if (erase_app_sectors(total_size) != HAL_OK) {
                HAL_FLASH_Lock();
                send_nack(cmd, BL_ERR_ERASE);
                break;
            }
            next_idx     = 0;
            flash_active = 1;
            send_ack(cmd);
            break;

        case BL_CMD_DATA:
            if (!flash_active) { send_nack(cmd, BL_ERR_NOSTART); break; }
            if (rx_hdr.DLC < 8) { send_nack(cmd, BL_ERR_DLC); break; }

            {
                uint32_t idx = (uint32_t)rx_data[1]
                             | ((uint32_t)rx_data[2] << 8)
                             | ((uint32_t)rx_data[3] << 16);

                /* ACK lost, host resent the last word */
                if (next_idx > 0 && idx == next_idx - 1U) {
                    send_ack_data(idx);
                    break;
                }
                if (idx != next_idx) {
                    send_nack_seq(next_idx);
                    break;
                }
                if ((idx + 1U) * 4U > total_size) {
                    send_nack(cmd, BL_ERR_SIZE);
                    break;
                }
                if (flash_write_word(APP_START_ADDR + idx * 4U, rd_le32(&rx_data[4])) != HAL_OK) {
                    send_nack(cmd, BL_ERR_WRITE);
                    break;
                }
                next_idx++;
                send_ack_data(idx);
            }
            break;

        case BL_CMD_CRC:
            if (rx_hdr.DLC < 5) { send_nack(cmd, BL_ERR_DLC); break; }

            {
                uint32_t expected = rd_le32(&rx_data[1]);

                if (!flash_active) {
                    /* ACK lost, host resent CRC */
                    const volatile bl_trailer_t *t = (const volatile bl_trailer_t *)BL_TRAILER_ADDR;
                    if (app_ok && t->crc == expected) send_ack(cmd);
                    else                              send_nack(cmd, BL_ERR_NOSTART);
                    break;
                }
                if (next_idx * 4U != total_size) {
                    send_nack(cmd, BL_ERR_SIZE);
                    break;
                }

                /* Read back from flash, not the received bytes */
                flash_flush_dcache();
                if (flash_crc(total_size) != expected) {
                    send_nack(cmd, BL_ERR_CRC);
                    break;
                }
                if (write_trailer(total_size, expected) != HAL_OK) {
                    send_nack(cmd, BL_ERR_WRITE);
                    break;
                }
                HAL_FLASH_Lock();
                flash_flush_dcache();
                flash_active = 0;
                app_ok       = app_is_valid();
                if (app_ok) send_ack(cmd);
                else        send_nack(cmd, BL_ERR_NOJUMP);
            }
            break;

        case BL_CMD_JUMP:
            if (flash_active || !app_ok) { send_nack(cmd, BL_ERR_NOJUMP); break; }
            send_ack(cmd);
            jump_to_app();
            break;

        default:
            break;
        }
    }
}
