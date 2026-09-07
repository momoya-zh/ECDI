"""Phase 11 测试资产生成（详设 §5——标准库手写 PNG chunk，无 PIL 依赖）。

- T1：1×1 不透明白 PNG（R255/G255/B255/A255）→ BGRA 像素 FF FF FF FF
- T2：2×2 PNG，含半透明像素 R200/G100/B50/A128 → 验证 premultiply
输出：C++ hex 数组文本（贴入 ImageDecodeTests.cpp）
"""
import struct
import zlib


def chunk(tag: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(data)) + tag + data
            + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def make_png(width: int, height: int, rgba_rows: list[list[tuple]]) -> bytes:
    """rgba_rows: 每行每像素 (r,g,b,a)——生成真彩 8bit PNG（filter 0）"""
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8bit RGBA
    raw = b""
    for row in rgba_rows:
        raw += b"\x00" + b"".join(struct.pack("BBBB", r, g, b, a) for r, g, b, a in row)
    idat = zlib.compress(raw)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", idat) + chunk(b"IEND", b""))


def to_hex_array(data: bytes, name: str) -> str:
    lines = [f"constexpr std::uint8_t {name}[] = {{"]
    for i in range(0, len(data), 12):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i+12]) + ",")
    lines.append("};")
    lines.append(f"constexpr std::size_t {name.replace('kPng', 'kPngSize').replace('kJpeg', 'kJpegSize')} = sizeof({name});")
    return "\n".join(lines)


png_t1 = make_png(1, 1, [[(255, 255, 255, 255)]])
# T2：2×2——第 0 行不透明白/黑，第 1 行半透明 R200/G100/B50/A128 ×2
png_t2 = make_png(2, 2, [
    [(255, 255, 255, 255), (0, 0, 0, 255)],
    [(200, 100, 50, 128), (200, 100, 50, 128)],
])


def make_quadrant_png() -> bytes:
    """视觉测试：64×64 四象限（红/绿/蓝/黄 不透明）——色彩与象限肉眼验证"""
    rows = []
    for y in range(64):
        row = []
        for x in range(64):
            if x < 32 and y < 32:
                row.append((255, 0, 0, 255))
            elif x >= 32 and y < 32:
                row.append((0, 255, 0, 255))
            elif x < 32:
                row.append((0, 0, 255, 255))
            else:
                row.append((255, 255, 0, 255))
        rows.append(row)
    return make_png(64, 64, rows)


def make_alpha_gradient_png() -> bytes:
    """视觉测试：64×64 红色 alpha 横向渐变（0→255）——覆盖深底呈淡入，预乘视觉验证"""
    rows = []
    for y in range(64):
        row = []
        for x in range(64):
            a = round(255 * x / 63)
            row.append((255, 0, 0, a))
        rows.append(row)
    return make_png(64, 64, rows)


print(to_hex_array(make_quadrant_png(), "kPngQuadrant64"))
print()
print(to_hex_array(make_alpha_gradient_png(), "kPngAlphaGrad64"))
print(f"// sizes: t1={len(png_t1)}B t2={len(png_t2)}B")
