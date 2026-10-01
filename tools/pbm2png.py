import struct
import sys
import zlib


def conv(src, dst):
    d = open(src, 'rb').read()
    parts = d.split(b'\n', 2)
    w, h = map(int, parts[1].split())
    body = parts[2]
    rb = (w + 7) // 8
    raw = b''.join(b'\x00' + bytes((~b) & 0xFF for b in body[y * rb:(y + 1) * rb]) for y in range(h))

    def chunk(t, c):
        return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 1, 0, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw))
           + chunk(b'IEND', b''))
    open(dst, 'wb').write(png)


for s in sys.argv[1:]:
    conv(s, s.replace('.pbm', '.png'))
