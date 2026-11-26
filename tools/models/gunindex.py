# List every gun's first-person model files and the textures they use.
#
#   python -I gunindex.py <image.bin> <unpacked files dir> [-o models.json]
#
# image.bin  = the game's memory image (tools/re/unxex.py; tools/re/out/image.bin).
# files dir  = pdunpack's output for PackedSegFile (file_XXXX.bin).
#
# The weapon table g_Weapons (0x825D32C0) holds one weapondef pointer per weapon number.
# A weapondef (N64 layout) starts with two u16 file ids: the high-detail and the low-detail
# first-person model. Each model file starts with an N64 modeldef header whose texconfigs
# (12 bytes each, texture number first) name the textures in Textures.raw.
import json, struct, sys

IMAGE_BASE = 0x82000000
G_WEAPONS = 0x825D32C0
FILE_BASE = 0x05000000  # pointers inside an unloaded model file are 0x05000000 + offset

# Weapon numbers 0x01-0x2C (the guns), N64 names (src/include/constants.h).
NAMES = ('UNARMED FALCON2 FALCON2_SILENCER FALCON2_SCOPE MAGSEC4 MAULER PHOENIX DY357MAGNUM '
         'DY357LX CMP150 CYCLONE CALLISTO RCP120 LAPTOPGUN DRAGON K7AVENGER AR34 SUPERDRAGON '
         'SHOTGUN REAPER SNIPERRIFLE FARSIGHT DEVASTATOR ROCKETLAUNCHER SLAYER COMBATKNIFE '
         'CROSSBOW TRANQUILIZER LASER GRENADE NBOMB TIMEDMINE PROXIMITYMINE REMOTEMINE '
         'COMBATBOOST PP9I CC13 KL01313 KF7SPECIAL ZZT DMC AR53 RCP45 PSYCHOSISGUN').split()


def texconfigs(path):
    """Texture numbers and sizes from a model file's texconfig list."""
    d = open(path, 'rb').read()
    count = struct.unpack('>h', d[0x16:0x18])[0]
    ptr = struct.unpack('>I', d[0x18:0x1C])[0] - FILE_BASE
    result = []
    for i in range(max(count, 0)):
        e = ptr + i * 12
        if e + 12 > len(d): break
        result.append({'texture': f'0x{struct.unpack(">i", d[e:e + 4])[0]:04X}', 'w': d[e + 4], 'h': d[e + 5]})
    return result


def main():
    image_path, files_dir = sys.argv[1], sys.argv[2]
    out = sys.argv[sys.argv.index('-o') + 1] if '-o' in sys.argv else 'models.json'
    img = open(image_path, 'rb').read()
    u32 = lambda a: struct.unpack('>I', img[a - IMAGE_BASE:a - IMAGE_BASE + 4])[0]
    u16 = lambda a: struct.unpack('>H', img[a - IMAGE_BASE:a - IMAGE_BASE + 2])[0]

    guns = []
    for number, name in enumerate(NAMES, start=1):
        weapondef = u32(G_WEAPONS + number * 4)
        if not weapondef: continue
        entry = {'weapon': f'0x{number:02X}', 'name': name, 'weapondef': f'0x{weapondef:08X}', 'files': []}
        for detail, file_id in (('hi', u16(weapondef)), ('lo', u16(weapondef + 2))):
            if not file_id: continue
            path = f'{files_dir}/file_{file_id:04X}.bin'
            try:
                textures = texconfigs(path)
            except OSError:
                textures = None  # empty entry in PackedSegFile
            entry['files'].append({'detail': detail, 'file': f'0x{file_id:04X}', 'textures': textures})
        guns.append(entry)
        print(f"{entry['weapon']} {name:16} " + ' '.join(f"{f['detail']} {f['file']}" for f in entry['files']))
    json.dump(guns, open(out, 'w'), indent=1)
    print(f'{len(guns)} weapons written to {out}')


main()
