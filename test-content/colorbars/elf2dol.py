import struct, sys
# Minimal ELF32 big-endian -> GameCube DOL converter (PT_LOAD segments).
d = open(sys.argv[1], 'rb').read()
e_entry, e_phoff = struct.unpack('>II', d[24:32])
e_phentsize, e_phnum = struct.unpack('>HH', d[42:46])
text, data = [], []
for i in range(e_phnum):
    o = e_phoff + i * e_phentsize
    p_type, p_off, p_vaddr, _, p_filesz, p_memsz, p_flags, _ = struct.unpack('>8I', d[o:o+32])
    if p_type != 1 or p_filesz == 0: continue
    (text if p_flags & 1 else data).append((p_vaddr, d[p_off:p_off+p_filesz]))
hdr = bytearray(0x100); body = bytearray(); off = 0x100
def put(idx, addr, blob):
    global off
    struct.pack_into('>I', hdr, idx*4, off); struct.pack_into('>I', hdr, 0x48 + idx*4, addr)
    struct.pack_into('>I', hdr, 0x90 + idx*4, len(blob)); body.extend(blob); off += len(blob)
for i, (a, b) in enumerate(text): put(i, a, b)
for i, (a, b) in enumerate(data): put(7 + i, a, b)
struct.pack_into('>I', hdr, 0xE0, e_entry)
open(sys.argv[2], 'wb').write(hdr + body)
