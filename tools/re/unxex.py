# Decrypt + decompress (basic) a retail XEX2 into its memory image. Usage: unxex.py default.xex image.bin
import struct, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
RETAIL = bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')
src, dst = sys.argv[1], sys.argv[2]
d = open(src, 'rb').read()
_, flags, pe_off, _, sec_off, nopt = struct.unpack('>4sIIIII', d[:24])
ff = None
for i in range(nopt):
    k, v = struct.unpack('>II', d[24+i*8:32+i*8])
    if k == 0x3FF: ff = v
ekey = d[sec_off+0x150:sec_off+0x160]
dec = lambda k, data, m: Cipher(algorithms.AES(k), m).decryptor().update(data)
key = dec(RETAIL, ekey, modes.ECB())
body = d[pe_off:]; body = body[:len(body)//16*16]
plain = dec(key, body, modes.CBC(b'\0'*16))
size, enc, comp = struct.unpack('>IHH', d[ff:ff+8])
out = bytearray(); p = 0
for j in range((size-8)//8):
    ds, zs = struct.unpack('>II', d[ff+8+j*8:ff+16+j*8])
    out += plain[p:p+ds]; p += ds; out += b'\0'*zs
open(dst, 'wb').write(out); print('image bytes', hex(len(out)))
