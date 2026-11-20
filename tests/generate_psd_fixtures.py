"""Small synthetic Photoshop PSD v1 composite fixtures for reproducible tests.
Only stdlib modules. Not a Photoshop emulator; these test the composite parser.
"""
from pathlib import Path
import struct, zlib

DEST = Path(__file__).resolve().parents[1] / 'samples'
DEST.mkdir(exist_ok=True)
W, H = 3, 2
RGB = [
    [255, 0, 0, 80, 45, 128],
    [0, 255, 0, 90, 64, 128],
    [0, 0, 255, 100, 220, 128],
]
A = [255, 255, 128, 0, 255, 96]
GRAY = [0, 127, 255, 20, 70, 230]

def big16(n): return struct.pack('>H', n)
def big32(n): return struct.pack('>I', n)

def packbits(row):
    # Literal blocks (not ideal compression, but valid PackBits syntax).
    return bytes([len(row) - 1]) + row

def psd(planes, mode=3, compression=0, depth=8, layer_blob=b''):
    channels = len(planes)
    buf = bytearray(b'8BPS' + big16(1) + b'\0'*6)
    buf += big16(channels) + big32(H) + big32(W) + big16(depth) + big16(mode)
    buf += big32(0) # color mode data
    buf += big32(0) # resources
    buf += big32(len(layer_blob)) + layer_blob  # skip this entire section
    buf += big16(compression)
    rows = [bytes(plane[y*W*(depth//8):(y+1)*W*(depth//8)])
            for plane in planes for y in range(H)]
    data = b''.join(rows)
    if compression == 0:
        buf += data
    elif compression == 1:
        chunks = [packbits(row) for row in rows]
        buf += b''.join(big16(len(x)) for x in chunks)
        buf += b''.join(chunks)
    elif compression == 2:
        buf += zlib.compress(data)
    elif compression == 3:
        predicted = b''.join(bytes([row[0]]) + bytes((row[i] - row[i-1]) & 255
            for i in range(1, len(row))) for row in rows)
        buf += zlib.compress(predicted)
    return bytes(buf)

rgb = [bytes(p) for p in RGB]
for variant in range(4):
    (DEST / f'synthetic-{["raw", "rle", "zip", "zip-pred"][variant]}.psd').write_bytes(
        psd(rgb, compression=variant, layer_blob=b'SKIP' * 128 if variant == 1 else b''))
(DEST / 'synthetic-rgba.psd').write_bytes(psd(rgb + [bytes(A)], compression=1))
(DEST / 'synthetic-gray.psd').write_bytes(psd([bytes(GRAY)], mode=1, compression=0))
(DEST / 'synthetic-gray-alpha.psd').write_bytes(psd([bytes(GRAY), bytes(A)], mode=1, compression=2))
planes16 = [b''.join(big16(v * 257) for v in plane) for plane in RGB]
(DEST / 'synthetic-rgb16.psd').write_bytes(psd(planes16, compression=2, depth=16))
(DEST / 'synthetic-truncated.psd').write_bytes((DEST / 'synthetic-rle.psd').read_bytes()[:-7])
(DEST / 'synthetic-psb.psd').write_bytes(b'8BPS' + big16(2) + b'\0'*40)
bad_mode = bytearray(psd(rgb))
bad_mode[24:26] = big16(4)  # unsupported CMYK marker
(DEST / 'synthetic-bad-mode.psd').write_bytes(bad_mode)
(DEST / 'synthetic-too-big.psd').write_bytes((lambda b: b[:18]+big32(30000)+b[22:])(psd(rgb)))
(DEST / 'synthetic-zip-pred16.psd').write_bytes(psd(planes16, compression=3, depth=16))
(DEST / 'synthetic-bad-rle.psd').write_bytes((lambda b: b[:40]+b'\xff\xff'+b[42:])(psd(rgb,compression=1)))
print('Generated PSD test fixtures:', len(list(DEST.glob('synthetic-*.psd'))))
