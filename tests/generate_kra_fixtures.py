#!/usr/bin/env python3
"""Make synthetic Krita archives for testing. Not actual Krita-authored files."""
from pathlib import Path
import io
import zipfile
import struct

root = Path(__file__).resolve().parent.parent / 'samples'
png = (root / 'preview-test.png').read_bytes()

def make(name, compression, entries=None, zip64local=False):
    with zipfile.ZipFile(root / name, 'w', compression=compression, allowZip64=True) as archive:
        archive.writestr('mimetype', 'application/x-krita', compress_type=zipfile.ZIP_STORED)
        if entries is None:
            entries = [('mergedimage.png', png)]
        for filename, content in entries:
            if zip64local:
                with archive.open(filename, 'w', force_zip64=True) as output:
                    output.write(content)
            else:
                archive.writestr(filename, content)

make('synthetic-deflate.kra', zipfile.ZIP_DEFLATED)
make('synthetic-stored.kra', zipfile.ZIP_STORED)
make('synthetic-zip64local.kra', zipfile.ZIP_DEFLATED, zip64local=True)
make('synthetic-missing.kra', zipfile.ZIP_STORED, [('other.png', png)])
make('synthetic-duplicate.kra', zipfile.ZIP_DEFLATED,
     [('mergedimage.png', png), ('mergedimage.png', png)])
corrupt = bytearray((root / 'synthetic-stored.kra').read_bytes())
pos = corrupt.find(png)
assert pos > 0
corrupt[pos + 10] ^= 0x20
(root / 'synthetic-crc.kra').write_bytes(corrupt)
(root / 'synthetic-truncated.kra').write_bytes((root / 'synthetic-deflate.kra').read_bytes()[:-10])
print('Created synthetic Krita ZIP fixtures.')

# Small but fully valid ZIP64 central-directory record/locator so the parser's
# large-file metadata path is covered without generating a multi-gigabyte file.
original = (root / 'synthetic-stored.kra').read_bytes()
eocd_pos = original.rfind(b'PK\x05\x06')
assert eocd_pos == len(original) - 22
end = bytearray(original[eocd_pos:])
number_entries = struct.unpack_from('<H', end, 10)[0]
directory_size = struct.unpack_from('<I', end, 12)[0]
directory_offset = struct.unpack_from('<I', end, 16)[0]
struct.pack_into('<H', end, 8, 0xffff)
struct.pack_into('<H', end, 10, 0xffff)
struct.pack_into('<I', end, 12, 0xffffffff)
struct.pack_into('<I', end, 16, 0xffffffff)
zip64_eocd = struct.pack('<IQHHIIQQQQ', 0x06064b50, 44, 45, 45,
                         0, 0, number_entries, number_entries,
                         directory_size, directory_offset)
zip64_locator = struct.pack('<IIQI', 0x07064b50, 0, eocd_pos, 1)
(root / 'synthetic-zip64eocd.kra').write_bytes(original[:eocd_pos] + zip64_eocd + zip64_locator + end)
