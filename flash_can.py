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
RESP_ACK     = 0x06
RESP_NACK    = 0x07
RESP_PENDING = 0x08

ERR_SEQ    = 0x07

TIMEOUT_S        = 2.0
ERASE_TIMEOUT_S  = 5.0   # per sector
RETRIES          = 5


class Nack(Exception):
    def __init__(self, cmd_name: str, err: int, data: bytes):
        super().__init__(f"{cmd_name} → NACK, error code 0x{err:02X}")
        self.err = err
        self.data = data


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def send(bus: can.Bus, data: bytes) -> None:
    msg = can.Message(arbitration_id=BL_RX_ID, data=data, is_extended_id=False)
    bus.send(msg, timeout=1.0)


def recv_ack(bus: can.Bus, cmd: int, cmd_name: str,
             timeout: float = TIMEOUT_S, idx: int | None = None) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = bus.recv(timeout=0.1)
        if msg is None or msg.arbitration_id != BL_TX_ID or len(msg.data) < 2:
            continue
        kind, rcmd = msg.data[0], msg.data[1]
        if rcmd != cmd:
            continue
        if kind == RESP_PENDING:
            deadline = time.time() + timeout
        elif kind == RESP_ACK:
            if idx is None or (len(msg.data) >= 5 and
                               int.from_bytes(msg.data[2:5], "little") == idx):
                return
        elif kind == RESP_NACK:
            err = msg.data[2] if len(msg.data) > 2 else 0
            raise Nack(cmd_name, err, bytes(msg.data))
    raise TimeoutError(f"{cmd_name} → no response after {timeout} s")


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
            recv_ack(bus, CMD_PING, "PING", timeout=0.1)
            print(" OK")
            return
        except Exception:
            pass
        print(".", end="", flush=True)
    raise TimeoutError("Bootloader did not respond — is the VCU connected?")


def send_with_retry(bus: can.Bus, data: bytes, cmd: int, cmd_name: str,
                    idx: int | None = None) -> None:
    for attempt in range(RETRIES):
        send(bus, data)
        try:
            recv_ack(bus, cmd, cmd_name, idx=idx)
            return
        except TimeoutError:
            if attempt == RETRIES - 1:
                raise


def flash(bus: can.Bus, firmware: bytes) -> None:
    size = len(firmware)

    if size % 4:
        firmware += b"\xFF" * (4 - size % 4)
        size = len(firmware)

    expected_crc = crc32(firmware)

    print(f"START  ({size} bytes) ...", end=" ", flush=True)
    send(bus, bytes([CMD_START]) + struct.pack("<I", size))
    recv_ack(bus, CMD_START, "START", timeout=ERASE_TIMEOUT_S)
    print("OK (sectors erased)")

    total_frames = size // 4
    i = 0
    while i < total_frames:
        word = firmware[i*4:(i+1)*4]
        frame = bytes([CMD_DATA]) + i.to_bytes(3, "little") + word
        try:
            send_with_retry(bus, frame, CMD_DATA, f"DATA[{i}]", idx=i)
        except Nack as e:
            if e.err != ERR_SEQ or len(e.data) < 6:
                raise
            expected = int.from_bytes(e.data[3:6], "little")
            if expected > total_frames:
                raise
            i = expected
            continue
        i += 1
        if i % 100 == 0 or i == total_frames:
            pct = i / total_frames * 100
            print(f"\r  {pct:5.1f}%  {i*4}/{size} bytes", end="", flush=True)
    print()

    print(f"CRC    (0x{expected_crc:08X}) ...", end=" ", flush=True)
    send_with_retry(bus, bytes([CMD_CRC]) + struct.pack("<I", expected_crc),
                    CMD_CRC, "CRC")
    print("OK")

    print("JUMP   ...", end=" ", flush=True)
    send(bus, bytes([CMD_JUMP]))
    recv_ack(bus, CMD_JUMP, "JUMP")
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
        trigger_app_reset(bus)
        ping_until_alive(bus)
        flash(bus, firmware)


if __name__ == "__main__":
    main()
