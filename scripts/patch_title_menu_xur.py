#!/usr/bin/env python3
"""Surgical in-place patch for FE_MAINMENU*.XUR (offline PC ship).

Hard-blocks Xbox LIVE Game and makes Challenge Game the top selectable stop:
  - DefaultFocus -> tecmo_challenge
  - LIVE Enabled=false (insert bool into XuiControl mask)
  - LIVE NavUp/NavDown -> tecmo_challenge; PressPath -> empty
  - Challenge NavUp -> tecmo_challenge (no wrap to LIVE)
  - Quit (returnLib) NavDown -> tecmo_challenge
  - "Return to Game Library" -> "Quit Game" (UTF-16BE padded)

Keeps XUR section layout intact: bumps FileSize, DATA length, and
TotalPropertiesCount when Enabled is inserted. Verified via XUIHelper
XUR->XUI round-trip.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def parse_strn(data: bytes | bytearray) -> list[str]:
    pos = 0x3C
    strn_off = strn_len = None
    for _ in range(4):
        magic = bytes(data[pos : pos + 4])
        off, length = struct.unpack_from(">II", data, pos + 4)
        if magic == b"STRN":
            strn_off, strn_len = off, length
        pos += 12
    if strn_off is None:
        raise RuntimeError("STRN section missing")
    strings = [""]
    i, end = strn_off, strn_off + strn_len
    while i + 2 <= end:
        n = struct.unpack_from(">H", data, i)[0]
        i += 2
        if i + n * 2 > end:
            break
        strings.append(
            "".join(chr(struct.unpack_from(">H", data, i + 2 * c)[0]) for c in range(n))
        )
        i += n * 2
    return strings


def replace_utf16be_padded(data: bytearray, old: str, new: str) -> int:
    if len(new) > len(old):
        raise ValueError("replacement longer than source")
    padded = new + (" " * (len(old) - len(new)))
    old_b = b"".join(struct.pack(">H", ord(c)) for c in old)
    new_b = b"".join(struct.pack(">H", ord(c)) for c in padded)
    hits = 0
    start = 0
    while True:
        idx = data.find(old_b, start)
        if idx < 0:
            break
        data[idx : idx + len(old_b)] = new_b
        hits += 1
        start = idx + len(old_b)
    return hits


def bump_headers_for_data_insert(data: bytearray, nbytes: int = 1) -> None:
    size = struct.unpack_from(">I", data, 14)[0]
    struct.pack_into(">I", data, 14, size + nbytes)
    props = struct.unpack_from(">I", data, 24)[0]
    struct.pack_into(">I", data, 24, props + nbytes)
    pos = 0x3C
    for _ in range(4):
        magic = bytes(data[pos : pos + 4])
        if magic == b"DATA":
            length = struct.unpack_from(">I", data, pos + 8)[0]
            struct.pack_into(">I", data, pos + 8, length + nbytes)
            return
        pos += 12
    raise RuntimeError("DATA section missing")


def patch_file(path: Path) -> dict:
    data = bytearray(path.read_bytes())
    orig_len = len(data)
    strings = parse_strn(data)
    idx = {s: i for i, s in enumerate(strings)}

    def need(name: str) -> int:
        if name not in idx:
            raise RuntimeError(f"missing string {name!r}")
        return idx[name]

    chal = need("tecmo_challenge")
    live = need("tecmo_xboxLive")
    qp = need("tecmo_quickPlay")
    ret = need("tecmo_returnLib")
    main_live = need("tecmo_mainXboxLive")
    main_chal = need("tecmo_mainChallenge")
    main_ret = need("tecmo_mainReturn")
    help_id = need("tecmo_helpOptions")
    blank = need("XuiSceneBlank")
    fe = need("fe_mainMenu")
    nav_cls = need("XuiNavButton")
    text_live = need("Xbox LIVE Game")
    if "fe_online.xur" in idx:
        press = idx["fe_online.xur"]
    elif "fe_online_4x3.xur" in idx:
        press = idx["fe_online_4x3.xur"]
    else:
        raise RuntimeError("missing LIVE PressPath string")

    stats = {
        "chal_nav": 0,
        "live_nav": 0,
        "quit_nav": 0,
        "live_press": 0,
        "live_enabled": 0,
        "default_focus": 0,
        "quit_hits": 0,
        "delta": 0,
    }

    def one(pat: bytes, repl: bytes, label: str) -> None:
        nonlocal data
        count = data.count(pat)
        if count != 1:
            raise RuntimeError(f"{label} count {count}")
        data = data.replace(pat, repl, 1)
        stats[label] = 1

    one(
        struct.pack(">HHH", main_chal, live, qp),
        struct.pack(">HHH", main_chal, chal, qp),
        "chal_nav",
    )
    one(
        struct.pack(">HHH", main_live, ret, chal),
        struct.pack(">HHH", main_live, chal, chal),
        "live_nav",
    )
    one(
        struct.pack(">HHH", main_ret, help_id, live),
        struct.pack(">HHH", main_ret, help_id, chal),
        "quit_nav",
    )

    seq = bytes([0x00, 0x01, 0x01]) + struct.pack(">H", press)
    needle = struct.pack(">HHHH", main_live, chal, chal, text_live) + seq
    repl = (
        struct.pack(">HHHH", main_live, chal, chal, text_live)
        + bytes([0x00, 0x01, 0x01])
        + struct.pack(">H", 0)
    )
    one(needle, repl, "live_press")

    marker = bytes([0x1A, 0x04, 0xC2]) + struct.pack(">HHH", main_live, chal, chal)
    at = data.find(marker)
    if at < 0:
        raise RuntimeError("enabled marker missing")
    hdr_pat = struct.pack(">H", nav_cls) + b"\x01"
    hdr = data.rfind(hdr_pat, 0, at)
    if hdr < 0:
        raise RuntimeError("LIVE NavButton header missing")
    props_count = struct.unpack_from(">H", data, hdr + 3)[0]
    struct.pack_into(">H", data, hdr + 3, props_count + 1)
    data[at] = 0x22
    data[at + 2] = 0xC6
    data[at + 3 + 2 : at + 3 + 2] = b"\x00"
    bump_headers_for_data_insert(data, 1)
    stats["live_enabled"] = 1

    fe_b = struct.pack(">H", fe)
    blank_b = struct.pack(">H", blank)
    qp_b = struct.pack(">H", qp)
    chal_b = struct.pack(">H", chal)
    hits: list[int] = []
    start = 0
    while True:
        j = data.find(fe_b, start)
        if j < 0:
            break
        window = data[j : j + 32]
        bpos = window.find(blank_b, 2)
        if bpos > 0:
            qpos = window.find(qp_b, bpos + 2)
            if 0 < qpos - bpos <= 8:
                hits.append(j + qpos)
        start = j + 2
    if not hits:
        raise RuntimeError("DefaultFocus not found")
    for off in hits:
        data[off : off + 2] = chal_b
    stats["default_focus"] = len(hits)

    stats["quit_hits"] = replace_utf16be_padded(
        data, "Return to Game Library", "Quit Game"
    )
    stats["delta"] = len(data) - orig_len
    path.write_bytes(data)
    return stats


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--game-root",
        type=Path,
        help="Game root containing FRONT_END/FE_MAINMENU*.XUR",
    )
    ap.add_argument(
        "--src-root",
        type=Path,
        help="Source FRONT_END tree to copy from before patching",
    )
    ap.add_argument(
        "--out-dir",
        type=Path,
        help="Write patched XURs here (e.g. an ignored output directory)",
    )
    args = ap.parse_args()

    if args.out_dir and args.src_root:
        args.out_dir.mkdir(parents=True, exist_ok=True)
        front = args.src_root / "FRONT_END"
        rc = 0
        for name in ("FE_MAINMENU.XUR", "FE_MAINMENU_4X3.XUR"):
            src = front / name
            if not src.is_file():
                print(f"missing {src}", file=sys.stderr)
                rc = 1
                continue
            dst = args.out_dir / name
            dst.write_bytes(src.read_bytes())
            try:
                stats = patch_file(dst)
                print(f"{name}: {stats}")
            except Exception as exc:
                print(f"{name}: FAILED {exc}", file=sys.stderr)
                rc = 1
        return rc

    if not args.game_root:
        print("need --game-root or --src-root/--out-dir", file=sys.stderr)
        return 2
    front = args.game_root / "FRONT_END"
    if not front.is_dir():
        print(f"missing FRONT_END under {args.game_root}", file=sys.stderr)
        return 2
    rc = 0
    for name in ("FE_MAINMENU.XUR", "FE_MAINMENU_4X3.XUR"):
        path = front / name
        if not path.is_file():
            continue
        before = path.read_bytes()
        try:
            stats = patch_file(path)
            print(f"{name}: {stats}")
        except Exception as exc:
            path.write_bytes(before)
            print(f"{name}: FAILED {exc}", file=sys.stderr)
            rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
