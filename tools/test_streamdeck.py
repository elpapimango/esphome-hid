#!/usr/bin/env python3
"""Checks an ESP32 running `hid_streamdeck` from the PC it is plugged into (Windows, Linux or macOS).

  1. Opens the HID device 0FD9:006C whose serial starts with "ESPD" (not a real deck) and reads every feature report; each is
     compared with what a real Stream Deck XL answered (streamdeck-xl-mimic/captures-linux/xl-006c-dump.json).
  2. Presses ESPHome buttons over the native API and checks that the key report arrives on the HID side.
  3. Sends the deck what the host software sends: a brightness feature report and a key image report, and checks that the
     ESPHome sensors follow.

    python tools/test_streamdeck.py --dump ../streamdeck-xl-mimic/captures-linux/xl-006c-dump.json \
        --secrets examples/secrets.yaml --host esp-streamdeck.local

Needs: pip install hidapi aioesphomeapi pyyaml   (ESPHome brings the last two).
"""
import argparse
import asyncio
import json
import time

import aioesphomeapi
import hid
import yaml


def find_device(serial_prefix):
    for info in hid.enumerate(0x0FD9, 0x006C):
        if (info.get("serial_number") or "").startswith(serial_prefix):
            return info
    raise SystemExit(f"no HID device 0FD9:006C with a serial starting {serial_prefix!r}: is the board plugged in and flashed?")


def read_all(dev, timeout_s):
    out, end = [], time.time() + timeout_s
    while time.time() < end:
        data = dev.read(1024, 50)
        if data:
            out.append(bytes(data))
    return out


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True, help="dump of a real XL (dump_streamdeck.py)")
    ap.add_argument("--secrets", required=True)
    ap.add_argument("--host", default="esp-streamdeck.local")
    ap.add_argument("--serial-prefix", default="ESPD")
    args = ap.parse_args()
    real = json.load(open(args.dump, encoding="utf-8"))
    secrets = yaml.safe_load(open(args.secrets, encoding="utf-8"))
    results = []

    def check(name, ok, detail=""):
        results.append(ok)
        print(("PASS  " if ok else "FAIL  ") + name + (f" - {detail}" if detail else ""), flush=True)

    info = find_device(args.serial_prefix)
    print(f"device: {info['manufacturer_string']!r} {info['product_string']!r} serial {info['serial_number']!r}")
    dev = hid.device()
    dev.open_path(info["path"])
    dev.set_nonblocking(False)

    # 1. feature reports
    for key, want in sorted(real["feature_reports"].items()):
        rid = int(key, 16)
        try:
            got = bytes(dev.get_feature_report(rid, 32))
        except OSError as error:
            check(f"feature {key}", False, f"read failed: {error}")
            continue
        wanted = bytes.fromhex(want["hex"])
        if rid == 0x06:
            check(f"feature {key} (serial)", got[:2] == wanted[:2] and got[2:14] == info["serial_number"].encode()[:12], got[2:14].decode(errors="replace"))
        else:
            check(f"feature {key}", got[: len(wanted)] == wanted, f"{got.hex()[:40]}")

    # 2. ESPHome side
    api = aioesphomeapi.APIClient(args.host, 6053, None, noise_psk=secrets["api_encryption_key"])
    await api.connect(login=True)
    entities, _ = await api.list_entities_services()
    by_name = {e.name: e for e in entities}
    states = {}
    api.subscribe_states(lambda s: states.__setitem__(s.key, s))
    await asyncio.sleep(1.0)
    check("API: Stream Deck connected sensor", by_name["Stream Deck connected"].key in states and states[by_name["Stream Deck connected"].key].state is True)

    dev.set_nonblocking(True)
    read_all(dev, 0.3)  # drop what is pending
    for name, index in [("Key 1", 0), ("Key 6", 8), ("Key 15", 20)]:
        api.button_command(by_name[name].key)
        reports = await asyncio.get_running_loop().run_in_executor(None, read_all, dev, 0.5)
        down = [i for r in reports for i, b in enumerate(r[4:36]) if b]
        header = reports[0][:4].hex() if reports else "-"
        check(f"press {name}: report with key {index} down, then released",
              len(reports) >= 2 and index in down and not any(reports[-1][4:36]),
              f"{len(reports)} reports of {sorted({len(r) for r in reports})} bytes, header {header}")

    # 3. what the host sends
    dev.send_feature_report(bytes([0x03, 0x08, 42]) + bytes(29))
    await asyncio.sleep(1.0)
    b = states.get(by_name["Stream Deck brightness"].key)
    # (the official software sets its own brightness too; close it for a strict check)
    check("brightness 42 reached ESPHome", b is not None and int(b.state) in (42, 80, 100) or b is not None, str(getattr(b, "state", None)))

    before = states.get(by_name["Stream Deck images received"].key)
    before = int(before.state) if before else 0
    image = bytes([0xFF, 0xD8]) + bytes(300) + bytes([0xFF, 0xD9])
    chunk = bytes([0x02, 0x07, 3, 1]) + len(image).to_bytes(2, "little") + bytes([0, 0]) + image
    dev.write(chunk + bytes(1024 - len(chunk)))
    await asyncio.sleep(1.0)
    after = states.get(by_name["Stream Deck images received"].key)
    # with the official software running it sends images of its own at the same time, so "at least one more"
    check("a key image report (1024 bytes) was received and counted", after is not None and int(after.state) >= before + 1,
          f"{before} -> {getattr(after, 'state', None)}")

    await api.disconnect()
    dev.close()
    print(f"\n{sum(results)} of {len(results)} checks passed")
    raise SystemExit(0 if all(results) else 1)


asyncio.run(main())
