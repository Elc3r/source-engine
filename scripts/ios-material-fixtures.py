#!/usr/bin/env python3
"""Generate original SM2 VCS and 4x4 VTF assets for the iOS probe.

No external compiler or game data is used. The shaders are hand-assembled:
VS: dcl_position v0; dcl_texcoord v1; mov oPos,v0; mov oT0,v1
PS: dcl t0; dcl_2d s0; texld r0,t0,s0; mov oC0,r0
The binary layouts follow shader_vcs_version.h (v4, no diff reference) and
vtf.h (7.2). Keep shader names synchronized with MaterialProbeShader.cpp and fixture VMTs.
"""
from pathlib import Path
import struct
import sys


def register(kind, index=0):
    return 0x80000000 | ((kind << 28) & 0x70000000) | ((kind << 8) & 0x1800) | index


def dst(kind, index=0):
    return register(kind, index) | 0x000f0000


def src(kind, index=0):
    return register(kind, index) | 0x00e40000


def write_fixtures(root):
    root = Path(root)
    shaders = root / 'shaders/fxc'
    shaders.mkdir(parents=True, exist_ok=True)
    programs = {
        'ios_probe_vs20': [0xfffe0200,
            0x0200001f, 0x80000000, dst(1),
            0x0200001f, 0x80000005, dst(1, 1),
            0x02000001, dst(4), src(1),
            0x02000001, dst(6), src(1, 1), 0x0000ffff],
        'ios_probe_ps20': [0xffff0200,
            0x0200001f, 0x80000000, dst(3),
            0x0200001f, 0x90000000, dst(10),
            0x03000042, dst(0), src(3), src(10),
            0x02000001, dst(8), src(0), 0x0000ffff],
    }
    # Hand-assembled equivalents of screenspaceeffect_vs20.fxc's two static
    # X360APPCHOOSER variants. c4-c7 carry the engine's transposed MVP matrix;
    # c48.zw carry bloom UV offsets. The selected pixel shader samples only t0.
    variants = []
    for transformed in [False, True]:
        code = [0xfffe0200,
                0x0200001f, 0x80000000, dst(1),
                0x0200001f, 0x80000005, dst(1, 1)]
        if transformed:
            code += [0x0200001f, 0x8000000a, dst(1, 2)]
        code += [0x05000051, dst(2, 1), 0, 0, 0, 0x3f800000]  # def c1,0,0,0,1
        if transformed:
            for component in range(4):
                mask = 1 << (16 + component)
                code += [0x03000009, register(4) | mask, src(1), src(2, 4 + component)]
            code += [0x02000001, dst(6, 3), src(1, 2)]
        else:
            code += [0x02000001, dst(4), src(1)]
        code += [0x02000001, dst(6), src(1, 1),
                 0x02000001, dst(6, 1), register(2, 1),  # c1.xxxx = zero
                 0x03000002, dst(6, 2), src(1, 1), register(2, 48) | 0x00ee0000,
                 0x0000ffff]
        variants.append(code)
    programs['screenspaceeffect_vs20'] = variants
    programs['ios_probe_sample'] = programs['ios_probe_ps20']
    for name, tokens in programs.items():
        combos = tokens if isinstance(tokens[0], list) else [tokens]
        codes = [struct.pack('<' + 'I' * len(combo), *combo) for combo in combos]
        header = struct.pack('<7I', 4, len(combos), 1, 0, 0, 0, 0)
        offset = len(header) + 8 * len(combos)
        dictionary = b''
        for code in codes:
            dictionary += struct.pack('<2I', offset, len(code))
            offset += len(code)
        (shaders / (name + '.vcs')).write_bytes(header + dictionary + b''.join(codes))

    # RGBA8888, point sampled, clamped, no mipmaps/LOD; one frame, no thumbnail.
    header = bytearray(80)
    struct.pack_into('<4s3I2HI2H', header, 0, b'VTF\0', 7, 2, 80, 4, 4, 0x30d, 1, 0)
    struct.pack_into('<3f', header, 32, .5, .5, .25)
    struct.pack_into('<fIBiBBH', header, 48, 1., 0, 1, -1, 0, 0, 1)
    colors = [(255, 0, 0, 255), (0, 255, 0, 255),
              (0, 0, 255, 255), (255, 255, 0, 255)]
    pixels = bytes(channel for y in range(4) for x in range(4)
                   for channel in colors[(y // 2) * 2 + x // 2])
    textures = root / 'materials/ios'
    textures.mkdir(parents=True, exist_ok=True)
    (textures / 'draw.vtf').write_bytes(header + pixels)
    # Force an alpha-bearing upload as well as the opaque RGB path above.
    struct.pack_into('<I', header, 20, 0x230d)
    alpha_pixels = bytearray(pixels)
    for y in range(4):
        for x in range(4):
            alpha_pixels[(y * 4 + x) * 4 + 3] = [255, 192, 128, 64][(y // 2) * 2 + x // 2]
    (textures / 'draw-alpha.vtf').write_bytes(header + alpha_pixels)


if __name__ == '__main__':
    write_fixtures(sys.argv[1])
