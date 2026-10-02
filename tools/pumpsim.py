#!/usr/bin/env python3
"""Bench test for a NibeGW gateway: plays the heat pump on RS-485.

Sends frames captured from a NIBE F730 through a USB-RS485 adapter, checks
the gateway's replies and how fast they come, checks that a UDP read
request is relayed on the next read token, and that frames are forwarded
by UDP.

Point the gateway at this machine first, e.g. on the gateway's NSH:
    nibegw stop
    nibegw start -a <this machine's IP>

Usage: pumpsim.py --gateway <gateway IP> [--port /dev/ttyUSBx] [--rounds N]
"""

import argparse
import glob
import socket
import sys
import time

import serial

ACK = b"\x06"
NAK = b"\x15"

READ_TOKEN = bytes.fromhex("5C00206900 49")
WRITE_TOKEN = bytes.fromhex("5C00206B00 4B")
PRODUCT = bytes.fromhex("5C00206D11 0123814637333020435520337834303056 B2")
DATA = bytes.fromhex(
    "5C00206850 01A81F0100A86400FDA7D003449C1E004F9CA000509C7800519C0301"
    "529C1B01879C14014E9CC601479C010115B9B0FF3AB94B00C9AF0000489C0D014C9C"
    "E7004B9C0000FFFF0000FFFF0000FFFF0000 45")
RMU40 = bytes.fromhex("5C00196000 79")         # Other address: no reply
BAD_CRC = bytes.fromhex("5C00206B00 4C")        # Wrong checksum: NAK

# A MODBUS 40 read request for register 40004 as the NIBE integrations send
READ_REQUEST = bytes.fromhex("C06902449C 73")


def wire_time(n):
    return n * 10 / 9600


class Pump:
    def __init__(self, port):
        self.ser = serial.Serial(port, 9600, timeout=0)

    def exchange(self, frame, window=0.3):
        """Send frame, return (reply bytes, ms from end of frame to reply)."""
        self.ser.reset_input_buffer()
        start = time.monotonic()
        self.ser.write(frame)
        self.ser.flush()
        end_of_frame = start + wire_time(len(frame))

        reply = b""
        first = None
        deadline = time.monotonic() + window
        while time.monotonic() < deadline:
            chunk = self.ser.read(256)
            if chunk:
                if first is None:
                    first = time.monotonic()
                reply += chunk

        # Some adapters echo what they send
        if reply.startswith(frame):
            reply = reply[len(frame):]

        latency = None if first is None else (first - end_of_frame) * 1000
        return reply, latency


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--gateway", required=True, help="gateway IP address")
    ap.add_argument("--port", help="RS-485 adapter (default: FTDI by-id)")
    ap.add_argument("--udp-port", type=int, default=9999)
    ap.add_argument("--read-port", type=int, default=10000)
    ap.add_argument("--rounds", type=int, default=20)
    args = ap.parse_args()

    port = args.port or (glob.glob("/dev/serial/by-id/*FTDI*") or [None])[0]
    if port is None:
        sys.exit("no RS-485 adapter found, use --port")

    pump = Pump(port)
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.bind(("0.0.0.0", args.udp_port))
    udp.settimeout(0)

    failures = 0
    latencies = []

    def check(name, frame, expect):
        nonlocal failures
        reply, latency = pump.exchange(frame)
        ok = reply == expect
        if latency is not None and expect:
            latencies.append(latency)
        lat = "-" if latency is None else f"{latency:5.1f} ms"
        print(f"{'ok  ' if ok else 'FAIL'} {name:<26} reply={reply.hex(' ') or '-':<20} {lat}")
        if not ok:
            failures += 1
        time.sleep(0.05)

    for _ in range(args.rounds):
        check("read token", READ_TOKEN, ACK)
        check("write token", WRITE_TOKEN, ACK)
        check("data frame", DATA, ACK)
        check("product frame", PRODUCT, ACK)
        check("frame for RMU40 (0x19)", RMU40, b"")
        check("bad checksum", BAD_CRC, NAK)

    # A queued read request replaces the ACK on the next read token
    udp.sendto(READ_REQUEST, (args.gateway, args.read_port))
    time.sleep(0.2)
    check("read token + request", READ_TOKEN, READ_REQUEST)
    check("read token, queue empty", READ_TOKEN, ACK)

    # Everything addressed to 0x20 with a good checksum is forwarded as is
    forwarded = []
    time.sleep(0.5)
    while True:
        try:
            forwarded.append(udp.recv(512))
        except BlockingIOError:
            break

    sent = {READ_TOKEN, WRITE_TOKEN, DATA, PRODUCT}
    exact = all(f in sent for f in forwarded)
    expected = args.rounds * 4 + 2
    print(f"{'ok  ' if len(forwarded) == expected and exact else 'FAIL'} "
          f"udp forwarded {len(forwarded)}/{expected} frames, "
          f"{'all exact' if exact else 'some differ from what was sent'}")
    if len(forwarded) != expected or not exact:
        failures += 1

    if latencies:
        latencies.sort()
        print(f"reply latency: min {latencies[0]:.1f} ms, "
              f"median {latencies[len(latencies) // 2]:.1f} ms, "
              f"max {latencies[-1]:.1f} ms")

    print("PASS" if failures == 0 else f"{failures} FAILURE(S)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
