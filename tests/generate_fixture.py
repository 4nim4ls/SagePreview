"""Optional: regenerate the synthetic .clip sample using only Python stdlib.

This constructs a structurally plausible CSFCHUNK container with a real
SQLite database and a PNG in CanvasPreview.ImageData. It is NOT a replacement
for testing against real Clip Studio Paint documents.
"""
import sqlite3
import struct
from pathlib import Path

root = Path(__file__).resolve().parent.parent
sample = root / 'samples'
png = (sample / 'preview-test.png').read_bytes()
temp_db = sample / '_fixture_database.sqlite'
try:
    conn = sqlite3.connect(temp_db)
    conn.execute('CREATE TABLE CanvasPreview (MainId INTEGER, CanvasId INTEGER, ImageType INTEGER, ImageWidth INTEGER, ImageHeight INTEGER, ImageData BLOB)')
    conn.execute('INSERT INTO CanvasPreview VALUES (1,1,1,500,300,?)', (png,))
    conn.commit()
    conn.close()
    database = temp_db.read_bytes()
finally:
    temp_db.unlink(missing_ok=True)

def chunk(name, data):
    return name + struct.pack('>Q', len(data)) + data

first = 24
head_len = 40
extra = chunk(b'CHNKExta', b'\x00' * 16)  # synthetic layer-like chunk
sqli_position = first + 16 + head_len + len(extra)
head_data = bytearray(40)
head_data[8:16] = struct.pack('>Q', sqli_position)
body = chunk(b'CHNKHead', head_data) + extra + chunk(b'CHNKSQLi', database) + chunk(b'CHNKFoot', b'')
total = 24 + len(body)
output = b'CSFCHUNK' + struct.pack('>Q', total) + struct.pack('>Q', first) + body
(sample / 'synthetic-test.clip').write_bytes(output)
# Deliberately damage the fast SQLi pointer. The chunk walk must still work.
fallback = bytearray(output)
fallback[48:56] = struct.pack('>Q', 0xFFFFFFFFFFFFFFFF)
(sample / 'synthetic-fallback.clip').write_bytes(fallback)
print('Wrote synthetic .clip fixtures:', len(output), 'bytes each')
