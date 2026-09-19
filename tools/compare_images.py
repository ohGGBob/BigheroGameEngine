#!/usr/bin/env python3
"""成像回归比对工具（P1-3 基础件）。

比对两张截图是否"感知一致"：逐像素容差 + 差异像素占比阈值。
纯标准库实现 PNG 解码（8-bit RGB/RGBA/灰度、非隔行），无第三方依赖，
供 CI 与本地 `--screenshot` 基线回归使用。

用法:
    python tools/compare_images.py baseline.png current.png \
        [--tolerance 8] [--max-ratio 0.01]

退出码: 0 = 通过（差异像素占比 <= max-ratio）；1 = 超差；2 = 输入错误。
"""

import argparse
import struct
import sys
import zlib


def _decode_png(path):
    """解码 8-bit 非隔行 PNG，返回 (width, height, pixels[RGB])。"""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: 不是 PNG 文件")

    pos, width, height, bit_depth, color_type = 8, 0, 0, 0, 0
    idat = bytearray()
    while pos < len(data):
        (length,), ctype = struct.unpack(">I", data[pos:pos + 4]), data[pos + 4:pos + 8]
        chunk, crc = data[pos + 8:pos + 8 + length], data[pos + 8 + length:pos + 12 + length]
        if struct.unpack(">I", struct.pack(">I", zlib.crc32(ctype + chunk) & 0xFFFFFFFF))[0] != struct.unpack(">I", crc)[0]:
            raise ValueError(f"{path}: CRC 校验失败 ({ctype.decode('latin1')})")
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
            if bit_depth != 8:
                raise ValueError(f"{path}: 仅支持 8-bit（当前 {bit_depth}）")
            if color_type not in (0, 2, 6):
                raise ValueError(f"{path}: 仅支持灰度/RGB/RGBA（当前 color_type={color_type}）")
            if chunk[12] != 0:
                raise ValueError(f"{path}: 不支持隔行扫描")
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break
        pos += 12 + length

    channels = {0: 1, 2: 3, 6: 4}[color_type]
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    # 还原 PNG 过滤器（None/Sub/Up/Average/Paeth）
    out = bytearray(height * stride)
    prev = bytearray(stride)
    for y in range(height):
        row = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        ft, line = row[0], bytearray(row[1:])
        if ft == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line

    # 统一展开为 RGB 三元组序列
    pixels = []
    for i in range(0, len(out), channels):
        if channels == 1:
            pixels.append((out[i], out[i], out[i]))
        else:
            pixels.append((out[i], out[i + 1], out[i + 2]))
    return width, height, pixels


def _luma(px):
    return 0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]


def main():
    ap = argparse.ArgumentParser(description="成像回归比对：逐像素容差 + 差异占比阈值")
    ap.add_argument("baseline")
    ap.add_argument("current")
    ap.add_argument("--tolerance", type=float, default=8.0,
                    help="单像素亮度差容许值（0-255，默认 8）")
    ap.add_argument("--max-ratio", type=float, default=0.01,
                    help="差异像素占比上限（默认 0.01 = 1%%）")
    args = ap.parse_args()

    try:
        w1, h1, p1 = _decode_png(args.baseline)
        w2, h2, p2 = _decode_png(args.current)
    except (ValueError, OSError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    if (w1, h1) != (w2, h2):
        print(f"FAIL: 尺寸不一致 {w1}x{h1} vs {w2}x{h2}")
        return 1

    total = w1 * h1
    diff = sum(1 for a, b in zip(p1, p2) if abs(_luma(a) - _luma(b)) > args.tolerance)
    ratio = diff / total
    l1 = sum(map(_luma, p1)) / total
    l2 = sum(map(_luma, p2)) / total

    verdict = "PASS" if ratio <= args.max_ratio else "FAIL"
    print(f"{verdict}: diff_ratio={ratio:.5f} ({diff}/{total}, tolerance={args.tolerance}, "
          f"max_ratio={args.max_ratio}) | mean_luma baseline={l1:.2f} current={l2:.2f}")
    return 0 if ratio <= args.max_ratio else 1


if __name__ == "__main__":
    sys.exit(main())
