#!/usr/bin/env python3
"""
VCU CAN bootloader host script.

Usage:
    python flash_can.py firmware.bin [--channel CHANNEL] [--bitrate BITRATE]

Requires:  pip install python-can
Tested with PCAN-USB, but any python-can compatible adapter works.
"""

import argparse
import struct
import time
import zlib
import can

BL_RX_ID   = 0x7E0   # host → VCU (bootloader commands)
BL_TX_ID   = 0x7E1   # VCU  → host (bootloader responses)

# CAN frame the app listens for to trigger reset into bootloader
APP_RESET_ID   = 0x7DF
APP_RESET_DATA = bytes([0xDE, 0xAD])

CMD_PING   = 0x01
CMD_START  = 0x02
CMD_DATA   = 0x03
CMD_CRC    = 0x04
CMD_JUMP   = 0x05
RESP_ACK   = 0x06
RESP_NACK  = 0x07

TIMEOUT_S  = 2.0


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def send(bus: can.Bus, data: bytes) -> None:
    msg = can.Message(arbitration_id=BL_RX_ID, data=data, is_extended_id=False)
    bus.send(msg, timeout=1.0)


def recv_ack(bus: can.Bus, cmd_name: str) -> None:
    deadline = time.time() + TIMEOUT_S
    while time.time() < deadline:
        msg = bus.recv(timeout=0.1)
        if msg is None:
            continue
        if msg.arbitration_id != BL_TX_ID:
            continue
        if msg.data[0] == RESP_ACK:
            return
        if msg.data[0] == RESP_NACK:
            err = msg.data[1] if len(msg.data) > 1 else 0
            raise RuntimeError(f"{cmd_name} → NACK, error code 0x{err:02X}")
    raise TimeoutError(f"{cmd_name} → no response after {TIMEOUT_S} s")


def trigger_app_reset(bus: can.Bus) -> None:
    """Tell the running app to set the RAM flag and reset into the bootloader."""
    msg = can.Message(arbitration_id=APP_RESET_ID,
                      data=APP_RESET_DATA,
                      is_extended_id=False)
    bus.send(msg, timeout=1.0)
    print("Reset request sent to app — waiting for bootloader ...")


def ping_until_alive(bus: can.Bus, retries: int = 30) -> None:
    """Send PING repeatedly until the bootloader ACKs (gives MCU time to reset)."""
    print("PING", end="", flush=True)
    for _ in range(retries):
        try:
            send(bus, bytes([CMD_PING]))
            msg = bus.recv(timeout=0.1)
            if msg and msg.arbitration_id == BL_TX_ID and msg.data[0] == RESP_ACK:
                print(" OK")
                return
        except Exception:
            pass
        print(".", end="", flush=True)
    raise TimeoutError("Bootloader did not respond — is the VCU connected?")


def flash(bus: can.Bus, firmware: bytes) -> None:
    size = len(firmware)

    # Pad to 4-byte boundary
    if size % 4:
        firmware += b"\xFF" * (4 - size % 4)
        size = len(firmware)

    expected_crc = crc32(firmware)

    # START
    print(f"START  ({size} bytes) ...", end=" ", flush=True)
    send(bus, bytes([CMD_START]) + struct.pack("<I", size))
    recv_ack(bus, "START")
    print("OK (sectors erased)")

    # DATA — 4 bytes per frame
    total_frames = size // 4
    for i in range(total_frames):
        word = firmware[i*4:(i+1)*4]
        send(bus, bytes([CMD_DATA]) + word)
        recv_ack(bus, f"DATA[{i}]")
        if (i + 1) % 100 == 0 or (i + 1) == total_frames:
            pct = (i + 1) / total_frames * 100
            print(f"\r  {pct:5.1f}%  {(i+1)*4}/{size} bytes", end="", flush=True)
    print()

    # CRC
    print(f"CRC    (0x{expected_crc:08X}) ...", end=" ", flush=True)
    send(bus, bytes([CMD_CRC]) + struct.pack("<I", expected_crc))
    recv_ack(bus, "CRC")
    print("OK")

    # JUMP
    print("JUMP   ...", end=" ", flush=True)
    send(bus, bytes([CMD_JUMP]))
    recv_ack(bus, "JUMP")
    print("OK — VCU is booting the new app")


def main() -> None:
    parser = argparse.ArgumentParser(description="VCU CAN bootloader flash tool")
    parser.add_argument("firmware", help="Path to firmware .bin file")
    parser.add_argument("--channel", default="PCAN_USBBUS1",
                        help="python-can channel (default: PCAN_USBBUS1)")
    parser.add_argument("--interface", default="pcan",
                        help="python-can interface (default: pcan)")
    parser.add_argument("--bitrate", type=int, default=250000,
                        help="CAN bitrate in bps (default: 250000)")
    args = parser.parse_args()

    with open(args.firmware, "rb") as f:
        firmware = f.read()

    print(f"Firmware : {args.firmware}  ({len(firmware)} bytes)")
    print(f"CAN      : {args.interface}/{args.channel} @ {args.bitrate} bps")
    print()

    with can.Bus(interface=args.interface,
                 channel=args.channel,
                 bitrate=args.bitrate) as bus:
        trigger_app_reset(bus)   # ask running app to reset into bootloader
        ping_until_alive(bus)    # wait for bootloader to come up
        flash(bus, firmware)     # flash + jump back to app


if __name__ == "__main__":
    main()
