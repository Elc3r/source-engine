#!/usr/bin/env python3
"""Generate original BSP v21 render-lump fixtures, not a playable/vis-built map.

Layouts match public/bspfile.h. Two independently indexed planar faces exercise
signed surfedges, texture-name indirection, independent UV bases and RGBExp32.
"""
from pathlib import Path
import math
import struct
import sys


def write_fixture(root, spatial=False):
    lumps = {}
    lumps[0] = b'{\n"classname" "worldspawn"\n}\n\0'
    planes = []
    lumps[2] = struct.pack('<3f5i', .5, .5, .5, 0, 4, 4, 4, 4)
    vertices, edges, surfedges, texinfo, faces, lighting = [], [(0, 0)], [], [], [], bytearray()
    for tile in range(2):
        left = (-.65 if tile else -1) if spatial else tile-1
        right = (.65 if tile else 1) if spatial else left+1
        bottom, top = (-.65,.65) if spatial and tile else (-1,1)
        slope = .4 if spatial and tile else 0
        depth = -.4 if spatial and tile else .5
        length = math.sqrt(1+slope*slope)
        planes.append(struct.pack('<4fi', -slope/length, 0, 1/length, depth/length, 2 if not slope else 5))
        base = len(vertices)
        vertices += [(x,y,depth+slope*x) for x,y in [(left,bottom),(right,bottom),(right,top),(left,top)]]
        firstedge = len(surfedges)
        for i in range(4):
            a, b = base+i, base+(i+1)%4
            reverse = i % 2 == 1
            edges.append((b, a) if reverse else (a, b))
            surfedges.append(-(len(edges)-1) if reverse else len(edges)-1)
        # Normalize base UVs across the face; lightmap U alone is mirrored.
        texinfo.append(struct.pack('<16f2i',
            4/(right-left), 0, 0, -4*left/(right-left), 0, 4/(top-bottom), 0, -4*bottom/(top-bottom),
            -3/(right-left), 0, 0, 3*right/(right-left), 0, 3/(top-bottom), 0, -3*bottom/(top-bottom), 0, 0))
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
            tile, 0, 1, firstedge, 4, tile, -1, -1,
            0, 255, 255, 255, lightofs, (right-left)*(top-bottom)*length, 0, 0, 3, 3, -1, 0, 0, 0)
        assert len(face) == 56
        faces.append(face)
    lumps[1] = b''.join(planes)
    lumps[3] = b''.join(struct.pack('<3f', *v) for v in vertices)
    lumps[6] = b''.join(texinfo)
    lumps[7] = b''.join(faces)
    lumps[8] = lighting
    lumps[12] = b''.join(struct.pack('<2H', *e) for e in edges)
    lumps[13] = struct.pack('<8i', *surfedges)
    lumps[14] = struct.pack('<9f3i', -1, -1, -.66 if spatial else .5, 1, 1, .5, 0, 0, 0, -1, 0, 2)
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
    path = Path(root) / ('maps/ios-spatial.bsp' if spatial else 'maps/ios-lightmap.bsp')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(result)


if __name__ == '__main__':
    write_fixture(sys.argv[1])
    write_fixture(sys.argv[1], spatial=True)
