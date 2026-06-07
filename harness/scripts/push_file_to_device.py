#!/usr/bin/env python3
"""Push an arbitrary local file to an arbitrary path on the disting NT over USB-MIDI sysex.

Reuses the file-upload chunk protocol from push_plugin_to_device.py (Expert
Sleepers, MIT), but writes to a caller-supplied device path with no preset /
rescan dance. Use it to place sample WAVs (e.g. a WAD-smuggled 16-bit WAV) under
/samples/<folder>/ without USB disk mode.

Usage:
    python3 push_file_to_device.py <sysExId> <local_file> <nt_path>

Example:
    python3 push_file_to_device.py 0 build/PATTERN.wav "/samples/00 PATTERN/PATTERN.wav"

NT_SYSEX_PORT overrides the port name (default: first free port containing
"disting NT"). Pause other MIDI clients (nt_helper) during the upload: the ACK
handshake on the shared endpoint can be corrupted by concurrent sysex traffic.
"""

import os
import sys

import mido

if len(sys.argv) != 4:
    sys.exit("usage: push_file_to_device.py <sysExId> <local_file> <nt_path>")

sysExId = int(sys.argv[1])
local_file = sys.argv[2]
nt_path = sys.argv[3]


def _nt_ports(names):
    forced = os.environ.get("NT_SYSEX_PORT")
    if forced:
        return [n for n in names if forced in n]
    return [n for n in names if "disting NT" in n]


out_candidates = _nt_ports(mido.get_output_names())
in_names = _nt_ports(mido.get_input_names())
if not out_candidates or not in_names:
    sys.exit("No 'disting NT' MIDI port found (set NT_SYSEX_PORT to override).")

outPort = inPort = None
for name in out_candidates:
    in_match = name if name in in_names else in_names[0]
    try:
        outPort = mido.open_output(name)
        inPort = mido.open_input(in_match)
    except (IOError, OSError):
        continue
    break

if outPort is None:
    sys.exit("All 'disting NT' MIDI ports are busy; free one and retry.")


def addCheckSum(arr):
    s = 0
    for i in range(7, len(arr)):
        s += arr[i]
    arr.append((-s) & 0x7F)


def uploadFile(path_local, path_nt):
    kOpUpload = 4
    ack = mido.Message.from_bytes(
        [0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x7A, 0, kOpUpload, 0xF7]
    )
    with open(path_local, "rb") as f:
        data = f.read()
    size = len(data)
    pos = 0
    while True:
        count = min(512, size - pos)
        if count == 0:
            break
        createAlways = int(pos == 0)
        arr = [0xF0, 0x00, 0x21, 0x27, 0x6D, sysExId, 0x7A, kOpUpload]
        arr += [ord(c) for c in path_nt]
        arr.append(0)
        arr.append(createAlways)
        arr += [0, 0, 0, 0, 0]
        arr.append((pos >> 28) & 0x0F)
        arr.append((pos >> 21) & 0x7F)
        arr.append((pos >> 14) & 0x7F)
        arr.append((pos >> 7) & 0x7F)
        arr.append((pos >> 0) & 0x7F)
        arr += [0, 0, 0, 0, 0]
        arr.append((count >> 28) & 0x0F)
        arr.append((count >> 21) & 0x7F)
        arr.append((count >> 14) & 0x7F)
        arr.append((count >> 7) & 0x7F)
        arr.append((count >> 0) & 0x7F)
        for j in range(count):
            b = data[pos + j]
            arr.append((b >> 4) & 0xF)
            arr.append(b & 0xF)
        addCheckSum(arr)
        arr.append(0xF7)
        outPort.send(mido.Message.from_bytes(arr))
        pos += count
        if inPort.receive() != ack:
            return False
    return True


ok = uploadFile(local_file, nt_path)
print("Success!" if ok else "Error uploading file!")
sys.exit(0 if ok else 1)
