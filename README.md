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
| 6 | `0x08040000` | 128 KB | Application (last 16 B = app-valid trailer at `0x0805FFF0`) |
| 7 | `0x08060000` | 128 KB | Fault NV storage (never erased) |

The bootloader lives entirely in sector 0. The linker script (`STM32F446RETX_FLASH.ld`) is set to `LENGTH = 16K` so the build fails if the bootloader overflows sector 0.

The last 16 bytes of RAM (`0x2001FFF0`) are a `NOINIT` region in the linker script, holding the boot request flag. The bootloader's stack starts below it, so the flag survives the bootloader's own startup.

### App-valid trailer

The last 16 bytes of sector 6 hold `{ size, crc32, reserved, magic }`. The bootloader writes the trailer only after it has read back the whole image from flash and the CRC matched, and it writes the magic word last. START erases sector 6 **first**, so the old app is invalidated before any other sector is touched. At every boot the bootloader checks the magic, the size, the vector table (SP in RAM, reset vector inside the image) and the CRC of the image in flash.

---

## Boot behavior

The MCU always boots from `0x08000000` (BOOT0 pulled low). The bootloader runs first every time.

**Cold boot (power cycle):**
RAM flag is gone and the app is valid → bootloader waits 50 ms → jumps to the app. If any bootloader frame arrives during those 50 ms, it stays in the bootloader.

**No valid app** (never flashed, interrupted flash, CRC mismatch): the bootloader stays and waits for the flash script.

**Flash requested (soft reset from app):**
App sets a RAM flag at `0x2001FFF0` to `0xDEADBEEF`, then calls `NVIC_SystemReset()`. The bootloader reads and immediately clears the flag, then waits indefinitely for the flash script to connect.

If power is cut mid-flash, the trailer is already erased, so `app_is_valid()` fails on the next boot and the bootloader stays alive waiting. Connect and reflash to recover.

**Jump to app:** the bootloader returns the clocks to HSI (`HAL_RCC_DeInit`), force-resets every peripheral (`HAL_DeInit`, including CAN and the TIM5 timebase), stops SysTick, clears every NVIC enable and pending bit, sets `VTOR`, and enters the app with interrupts **enabled**, like after a real reset.

---

## CAN protocol

**Bus:** CAN1, 250 kbps  
**Host → STM32:** ID `0x7E0`  
**STM32 → Host:** ID `0x7E1`

| Command | Byte 0 | Bytes 1–4 | Description |
|---------|--------|-----------|-------------|
| PING | `0x01` | — | Check bootloader is alive |
| START | `0x02` | `total_size` (LE, multiple of 4) | Erase the trailer sector, then only the sectors the image needs |
| DATA | `0x03` | bytes 1–3: word index (LE), bytes 4–7: 4 bytes firmware | Write one word at `APP_START + index * 4` |
| CRC | `0x04` | `crc32` (LE, zlib) | Read the image back from flash, compare, then write the trailer |
| JUMP | `0x05` | — | Jump to app |

Every response echoes the command byte in byte 1:

| Response | Frame | Description |
|----------|-------|-------------|
| ACK | `[0x06, cmd]` (DATA: `[0x06, 0x03, idx0, idx1, idx2]`) | Command accepted |
| NACK | `[0x07, cmd, err]` (SEQ: `[0x07, 0x03, 0x07, next0, next1, next2]`) | Command rejected |
| PENDING | `[0x08, cmd]` | Still working. Sent after each erased sector; the host resets its timeout |

| Error | Meaning |
|-------|---------|
| `0x01` SIZE | Bad size, write past `total_size`, or CRC sent before all words arrived |
| `0x02` ERASE | Flash erase failed |
| `0x03` WRITE | Flash program failed |
| `0x04` CRC | CRC of flash contents does not match |
| `0x05` NOSTART | DATA/CRC without START |
| `0x06` NOJUMP | No valid app |
| `0x07` SEQ | Unexpected word index; bytes 3–5 give the index the bootloader expects |
| `0x08` DLC | Frame too short |

**Retransmits:** if the host resends the last DATA word (its ACK was lost), the bootloader re-ACKs it without writing. If a word goes missing, the bootloader NACKs with SEQ and the index it expects, and the host resumes from there. Resending CRC after a success is ACKed again.

---

## App integration

Add this to your application's CAN receive handler. When ID `0x7DF` with data `[0xDE, 0xAD]` is received:

```c
*(volatile uint32_t *)0x2001FFF0UL = 0xDEADBEEFUL;
NVIC_SystemReset();
```

The app's linker script must start flash at `0x08004000`:

```
FLASH (rx) : ORIGIN = 0x08004000, LENGTH = 368K - 16
```

The last 16 bytes are reserved for the trailer. To keep the app's own stack off the boot flag, it can also use `RAM ... LENGTH = 128K - 16`. That's optional, because the app writes the flag right before resetting.

> **Upgrading from the previous bootloader:** apps flashed by the old version have no trailer, so the new bootloader treats them as invalid and waits. Reflash the app once with the new `flash_can.py`.

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
3. Send START (waits through PENDING while sectors erase) → DATA (with word index, retried on timeout) → CRC → JUMP
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
