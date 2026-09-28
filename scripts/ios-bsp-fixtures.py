#!/usr/bin/env python3
"""Generate original BSP v21 render-lump fixtures, not a playable/vis-built map.

Layouts match public/bspfile.h. Two independently indexed planar faces exercise
signed surfedges, texture-name indirection, independent UV bases and RGBExp32.
"""
from pathlib import Path
import struct
import sys


def write_fixture(root):
    lumps = {}
    lumps[0] = b'{\n"classname" "worldspawn"\n}\n\0'
    lumps[1] = struct.pack('<4fi', 0, 0, 1, .5, 2)
    lumps[2] = struct.pack('<3f5i', .5, .5, .5, 0, 4, 4, 4, 4)
    vertices, edges, surfedges, texinfo, faces, lighting = [], [(0, 0)], [], [], [], bytearray()
    for tile in range(2):
        left = tile - 1
        base = len(vertices)
        vertices += [(left, -1, .5), (left+1, -1, .5), (left+1, 1, .5), (left, 1, .5)]
        firstedge = len(surfedges)
        for i in range(4):
            a, b = base+i, base+(i+1)%4
            reverse = i % 2 == 1
            edges.append((b, a) if reverse else (a, b))
            surfedges.append(-(len(edges)-1) if reverse else len(edges)-1)
        # Texture UV: (x-left, (y+1)/2). Lightmap U alone is mirrored.
        texinfo.append(struct.pack('<16f2i',
            4, 0, 0, -4*left, 0, 2, 0, 2,
            -3, 0, 0, 3*(left+1), 0, 1.5, 0, 1.5, 0, 0))
        lightofs = len(lighting)
        for y in range(4):
            for x in range(4):
                if tile:
                    values = [32, 64, 128, 255]
                    rgb = [values[(x//2+2*(y//2)+c)%4] for c in range(3)]
                    lighting += struct.pack('<3Bb', *rgb, 0)
                else:
                    lighting += struct.pack('<3Bb', 64, 128, 255, -1)
        face = struct.pack('<HBBihhhh4Bif4iiHHI',
            0, 0, 1, firstedge, 4, tile, -1, -1,
            0, 255, 255, 255, lightofs, 2., 0, 0, 3, 3, -1, 0, 0, 0)
        assert len(face) == 56
        faces.append(face)
    lumps[3] = b''.join(struct.pack('<3f', *v) for v in vertices)
    lumps[6] = b''.join(texinfo)
    lumps[7] = b''.join(faces)
    lumps[8] = lighting
    lumps[12] = b''.join(struct.pack('<2H', *e) for e in edges)
    lumps[13] = struct.pack('<8i', *surfedges)
    lumps[14] = struct.pack('<9f3i', -1, -1, .5, 1, 1, .5, 0, 0, 0, -1, 0, 2)
    lumps[43] = b'ios/lightmapped\0'
    lumps[44] = struct.pack('<i', 0)
    header_size = 8 + 64*16 + 4
    result = bytearray(header_size)
    struct.pack_into('<4si', result, 0, b'VBSP', 21)
    for index, data in sorted(lumps.items()):
        result += b'\0' * (-len(result) % 4)
        struct.pack_into('<4i', result, 8+16*index, len(result), len(data), 1 if index in (7, 8) else 0, 0)
        result += data
    struct.pack_into('<i', result, header_size-4, 1)
    path = Path(root) / 'maps/ios-lightmap.bsp'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(result)


if __name__ == '__main__':
    write_fixture(sys.argv[1])
