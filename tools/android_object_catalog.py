#!/usr/bin/env python3
"""Locate decompiled OAM sequences; emit addresses, never ROM artwork.

Usage: python tools/android_object_catalog.py /path/to/baserom.gba
"""
import ast
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
NAMES = """sKeyzerIdleOam sDiamondOamData sLittleHeartOamData
sBoxJewelPieceNEClosedOam sBoxJewelPieceSEClosedOam sBoxJewelPieceSWClosedOam
sBoxJewelPieceNWClosedOam sBoxJewelPieceOpenedOam sBoxCDClosedOam sBoxCDOpenedOam
sBoxHeartRefillClosedOam sBoxHeartRefillOpenedOam sEscapeSwitchIdleOam sEscapeSwitchPressedOam
sRockOamData sMarumenIdleOam sMarumenStunnedOam sSpearMaskWalkingOam
sVortexSmallPartOam sVortexMediumPartOam sVortexLargePartOam
sTotsumenOam_83BB0E8 sKaentsuboIdleOam""".split()

def number(expression):
    expression = expression.replace("U8_MAX", "255").replace("ST_OAM_HFLIP", "8").replace("ST_OAM_VFLIP", "16")
    def visit(node):
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
            return -visit(node.operand) if isinstance(node.op, ast.USub) else visit(node.operand)
        if isinstance(node, ast.BinOp):
            a, b = visit(node.left), visit(node.right)
            if isinstance(node.op, ast.Add): return a + b
            if isinstance(node.op, ast.Sub): return a - b
            if isinstance(node.op, ast.BitOr): return a | b
        raise ValueError(expression)
    return visit(ast.parse(expression.strip(), mode="eval").body)

def frame(body):
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    items = re.findall(r"OAM_ENTRY\((.*?)\)", body, re.S)
    if not items: return None
    words = [number(body.split(",", 1)[0])]
    for item in items:
        x, y, dimensions, flip, tile, palette, priority = [part.strip() for part in item.split(",")]
        width, height = map(int, dimensions.removeprefix("SPRITE_SIZE_").split("x"))
        sizes = [(8,8),(16,16),(32,32),(64,64),(16,8),(32,8),(32,16),(64,32),(8,16),(8,32),(16,32),(32,64)]
        shape, size = divmod(sizes.index((width,height)), 4)
        words.extend([(shape << 14) | (number(y) & 255), (size << 14) | (number(flip) << 9) | (number(x) & 511),
                      (number(palette) << 12) | (number(priority) << 10) | number(tile)])
    if words[0] != len(items): raise ValueError("OAM count")
    return struct.pack("<" + "H" * len(words), *words)

def main():
    rom = pathlib.Path(sys.argv[1]).read_bytes()
    frames, animations = {}, {}
    for source in sorted((ROOT / "src").rglob("*.c")):
        text = source.read_text()
        for name, body in re.findall(r"const u16 (\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
            try:
                data = frame(body)
                if data: frames[name] = data
            except (ValueError, SyntaxError): pass
        for name, body in re.findall(r"const struct AnimationFrame (\w+)\[\]\s*=\s*\{(.*?)\};", text, re.S):
            animations[name] = re.findall(r"\{\s*(\w+)\s*,\s*([^}]+)\}", body)
    output = ["// Generated from decompiled sequence definitions; contains no ROM artwork.", "#pragma once", "#include <cstdint>", "namespace wl4::object_catalog {"]
    for name in NAMES:
        sequence = animations.get(name)
        if not sequence or any(part not in frames for part, _ in sequence):
            print("Missing definition:", name, file=sys.stderr); continue
        first = frames[sequence[0][0]]
        candidates, at = [], 0
        while (at := rom.find(first, at)) >= 0:
            pattern = struct.pack("<IB3x", at + 0x08000000, number(sequence[0][1]))
            pos = 0
            while (pos := rom.find(pattern, pos)) >= 0:
                candidates.append(pos); pos += 1
            at += 1
        found = None
        for pos in candidates:
            valid = True
            for index, (part, duration) in enumerate(sequence):
                pointer, time = struct.unpack_from("<IB", rom, pos + index * 8)
                offset = pointer - 0x08000000
                if time != number(duration) or offset < 0 or rom[offset:offset+len(frames[part])] != frames[part]:
                    valid = False; break
            end = pos + len(sequence)*8
            if valid and rom[end:end+8] == bytes(8): found = pos; break
        if found is None: raise ValueError("Sequence not found: " + name)
        output.append(f"constexpr uint32_t {name}=0x{found+0x08000000:08x};")
    output.append("} // namespace wl4::object_catalog")
    (ROOT / "android/app/src/main/cpp/object_catalog.h").write_text("\n".join(output) + "\n")

if __name__ == "__main__": main()
