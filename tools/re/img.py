# Read values from the unpacked game image (made by unxex.py) by guest address.
# Usage from another script or an interactive session:
#   PD_IMAGE=/path/to/image.bin python3 -I -c "import sys; sys.path.insert(0, 'tools/re'); from img import *; print(hex(u32(0x825DC8E8)))"
import os, struct, sys

_path = os.environ.get('PD_IMAGE')
if not _path:
    sys.exit('img.py: set PD_IMAGE to the image.bin written by unxex.py')
d = open(_path, 'rb').read()
B = 0x82000000  # the image's load address: guest address - B = offset in the file

u8 = lambda a: d[a - B]
u16 = lambda a: struct.unpack('>H', d[a - B:a - B + 2])[0]
u32 = lambda a: struct.unpack('>I', d[a - B:a - B + 4])[0]
f32 = lambda a: struct.unpack('>f', d[a - B:a - B + 4])[0]


def find(bs):
    """Every guest address where the bytes `bs` occur."""
    i = d.find(bs)
    r = []
    while i >= 0:
        r.append(B + i)
        i = d.find(bs, i + 1)
    return r


def cstr(a, n=80):
    """The NUL-terminated string at guest address `a` (at most n bytes)."""
    e = d.find(b'\0', a - B)
    return d[a - B:min(e, a - B + n)].decode('latin1')
