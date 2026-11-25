# Generates res/blockcraft.ico: an isometric grass block drawn in code (PNG-in-ICO, 16/32/48/256).
import struct, zlib, random, sys
def png(w, h, px):
    raw = b''.join(b'\x00' + bytes(px[y*w*4:(y+1)*w*4]) for y in range(h))
    def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
def tex(kind, x, y):
    r = random.Random(x * 131 + y * 7 + kind * 977)
    if kind == 0:  # grass top
        g = [(74,127,42),(85,144,47),(94,156,53),(104,168,59)][r.randrange(4)]; return g
    if y < 4 + (x * 7 % 3):  # side: grass lip
        return [(85,144,47),(94,156,53)][r.randrange(2)]
    return [(94,63,38),(109,74,45),(122,84,52),(134,96,60)][r.randrange(4)]
def draw(S):
    px = [0] * (S * S * 4)
    s = S * 0.94; cx = S / 2; cy = S / 2 + S * 0.03
    def put(x, y, c, shade):
        if 0 <= x < S and 0 <= y < S:
            i = (y * S + x) * 4; px[i:i+4] = [int(c[0]*shade), int(c[1]*shade), int(c[2]*shade), 255]
    for y in range(S):
        for x in range(S):
            fx, fy = x + 0.5 - cx, y + 0.5 - cy
            # inverse isometric: top face
            a = fx / (s / 2); b = (fy + s / 4) / (s / 4)
            u = (a - b) / 2 * -1; v = (a + b) / 2
            # top face test via barycentric on rhombus
            tx = (fx / (s/2) + (fy + s/4) / (s/4)) / 2; ty = (-fx / (s/2) + (fy + s/4) / (s/4)) / 2
            if -1 <= tx <= 0 or 0 <= tx <= 1:
                pass
            # simpler: classify by region
            top = abs(fx) / (s / 2) + abs(fy + s / 4) / (s / 4) <= 1
            if top:
                uu = (fx / (s/2) - (fy + s/4) / (s/4) + 1) / 2; vv = (fx / (s/2) + (fy + s/4) / (s/4) + 1) / 2
                put(x, y, tex(0, int(uu * 15.99), int(vv * 15.99)), 1.0); continue
            if -s/2 <= fx <= 0:
                yt = -s/4 + (fx + s/2) * 0.5; yb = yt + s / 2
                if yt <= fy <= yb:
                    uu = (fx + s/2) / (s/2); vv = (fy - yt) / (s/2)
                    put(x, y, tex(1, int(uu * 15.99), int(vv * 15.99)), 0.85); continue
            if 0 <= fx <= s/2:
                yt = 0 - fx * 0.5; yb = yt + s / 2
                if yt <= fy <= yb:
                    uu = fx / (s/2); vv = (fy - yt) / (s/2)
                    put(x, y, tex(1, int(uu * 15.99), int(vv * 15.99)), 0.65); continue
    return px
sizes = [16, 32, 48, 256]
imgs = [png(S, S, draw(S)) for S in sizes]
out = struct.pack('<HHH', 0, 1, len(sizes)); off = 6 + 16 * len(sizes)
for S, d in zip(sizes, imgs):
    out += struct.pack('<BBBBHHII', S % 256, S % 256, 0, 0, 1, 32, len(d), off); off += len(d)
out += b''.join(imgs)
open(sys.argv[1] if len(sys.argv) > 1 else 'res/blockcraft.ico', 'wb').write(out)
