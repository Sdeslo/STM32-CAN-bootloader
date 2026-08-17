# STM32 CAN Bootloader — STM32F446RE

Custom CAN bootloader for a STM32F446RE. Allows flashing the application firmware over CAN without touching the board.

---

## Flash memory layout

| Sector | Address | Size | Contents |
|--------|---------|------|----------|
| 0 | `0x08000000` | 16 KB | **Bootloader** (this project) |
| 1 | `0x08004000` | 16 KB | Application |
| 2 | `0x08008000` | 16 KB | Application |
| 3 | `0x0800C000` | 16 KB | Application |
| 4 | `0x08010000` | 64 KB | Application |
| 5 | `0x08020000` | 128 KB | Application |
| 6 | `0x08040000` | 128 KB | Application |
| 7 | `0x08060000` | 128 KB | Fault NV storage (never erased) |

The bootloader lives entirely in sector 0. The linker script (`STM32F446RETX_FLASH.ld`) is set to `LENGTH = 16K` so the build fails if the bootloader overflows sector 0.

---

## Boot behavior

The MCU always boots from `0x08000000` (BOOT0 pulled low). The bootloader runs first every time.

**Cold boot (power cycle):**
RAM flag is gone → bootloader waits 50 ms → jumps straight to the app. No noticeable delay.

**Flash requested (soft reset from app):**
App sets a RAM flag at `0x2001FFF0` to `0xDEADBEEF`, then calls `NVIC_SystemReset()`. The bootloader reads and immediately clears the flag, then waits indefinitely for the flash script to connect.

If power is cut mid-flash, the flag is already cleared so the next boot skips the bootloader. `app_is_valid()` will fail on the partial image and the bootloader stays alive waiting — connect and reflash to recover.

---

## CAN protocol

**Bus:** CAN1, 250 kbps  
**Host → VCU:** ID `0x7E0`  
**VCU → Host:** ID `0x7E1`

| Command | Byte 0 | Bytes 1–4 | Description |
|---------|--------|-----------|-------------|
| PING | `0x01` | — | Check bootloader is alive |
| START | `0x02` | `total_size` (LE) | Erase app sectors, prepare for data |
| DATA | `0x03` | 4 bytes firmware | Write 4 bytes sequentially from app start |
| CRC | `0x04` | `crc32` (LE) | Verify written flash against CRC32 |
| JUMP | `0x05` | — | Jump to app |

| Response | Byte 0 | Byte 1 | Description |
|----------|--------|--------|-------------|
| ACK | `0x06` | — | Command accepted |
| NACK | `0x07` | error code | Command rejected |

---

## App integration

Add this to your application's CAN receive handler. When ID `0x7DF` with data `[0xDE, 0xAD]` is received:

```c
*(volatile uint32_t *)0x2001FFF0UL = 0xDEADBEEFUL;
NVIC_SystemReset();
```

The app's linker script must start flash at `0x08004000`:

```
FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 368K
```

---

## Flashing over CAN

### Requirements

```
pip install python-can
```

A python-can compatible USB-CAN adapter (PCAN-USB, PEAK, etc.).

### Usage

```
python flash_can.py firmware.bin --interface pcan --channel PCAN_USBBUS1
```

The script will:
1. Send a reset request to the running app (`0x7DF [DE AD]`)
2. Ping until the bootloader responds
3. Send START → DATA → CRC → JUMP
4. App boots automatically after a successful flash

### Options

```
positional arguments:
  firmware              Path to firmware .bin file

options:
  --channel CHANNEL     python-can channel (default: PCAN_USBBUS1)
  --interface INTERFACE python-can interface (default: pcan)
  --bitrate BITRATE     CAN bitrate in bps (default: 250000)
```

---

## Project structure

```
CAN_Bootloader/
├── Core/
│   ├── Inc/
│   │   ├── bootloader.h       # Protocol defines, memory map, RAM flag
│   │   └── main.h
│   └── Src/
│       ├── bootloader.c       # Bootloader logic (erase, write, CRC, jump)
│       └── main.c             # CubeIDE generated — calls BL_Run(&hcan1)
└── STM32F446RETX_FLASH.ld     # Restricted to 16 KB (sector 0 only)
flash_can.py                   # Host-side flash script
```
