#!/usr/bin/env python3
import os
import pty
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time


READY = b"9D-READY\n"
TVERSION = 100
RVERSION = 101


def read_exact(fd, size, timeout=5):
    deadline = time.monotonic() + timeout
    data = bytearray()
    while len(data) < size:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([fd], [], [], remaining)[0]:
            raise RuntimeError("timed out reading connected stream")
        data.extend(os.read(fd, size - len(data)))
    return bytes(data)


def version_request(tag=1):
    version = b"9P2000.u"
    body = struct.pack("<IH", 8192, len(version)) + version
    return struct.pack("<IBH", 7 + len(body), TVERSION, tag) + body


def wait_for_raw(fd, timeout=5):
    deadline = time.monotonic() + timeout
    flags = termios.ECHO | termios.ICANON | termios.ISIG
    while time.monotonic() < deadline:
        if not termios.tcgetattr(fd)[3] & flags:
            return
        time.sleep(0.01)
    raise RuntimeError("connected tty was not configured raw")


def check_version(fd, tag=1):
    header = read_exact(fd, 7)
    size, kind, response_tag = struct.unpack("<IBH", header)
    response = read_exact(fd, size - 7)
    if kind != RVERSION or response_tag != tag:
        raise RuntimeError("unexpected Rversion header")
    if response[6:] != b"9P2000.u":
        raise RuntimeError("unexpected negotiated version")


def run(binary, root, signal_ready):
    master, slave = pty.openpty()
    path = os.ttyname(slave)
    command = [binary, "-p", "stream!" + path, root]
    if signal_ready:
        command.insert(1, "-R")
    process = subprocess.Popen(command, stderr=subprocess.PIPE)
    try:
        if signal_ready and read_exact(master, len(READY)) != READY:
            raise RuntimeError("wrong readiness marker")
        wait_for_raw(slave)
        os.write(master, version_request())
        check_version(master)
    finally:
        process.terminate()
        process.wait(timeout=5)
        os.close(master)
        os.close(slave)


with tempfile.TemporaryDirectory() as root:
    run(sys.argv[1], root, False)
    run(sys.argv[1], root, True)

print("connected stream tests passed")
