"""Genera los iconos de NEORecomp a partir de NEOGEO.png (mando del Neo Geo AES, serie TriMui) de
KyleBing/retro-game-console-icons (https://github.com/KyleBing/retro-game-console-icons),
con licencia CC BY 4.0:
  src/platform/icono.h   el PNG tal cual, como arreglo de C (icono de la ventana; main_sdl.c lo
                         recorta al dibujo)
  src/platform/icono.ico el dibujo recortado, con un margen y llevado a 256x256 (icono del .exe)
Solo usa la biblioteca estándar:
  python3 tools/make_icon.py ruta/a/NEOGEO.png"""
import pathlib, struct, sys, zlib

def read_png(data):
    """PNG RGBA de 8 bits sin entrelazar -> (ancho, alto, pixeles RGBA)"""
    pos, idat = 8, b''
    while pos < len(data):
        n, t = struct.unpack('>I4s', data[pos:pos + 8])
        c = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if t == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', c)
            if depth != 8 or ctype != 6 or inter:
                sys.exit('se necesita un PNG RGBA de 8 bits sin entrelazar')
        elif t == b'IDAT':
            idat += c
    raw = zlib.decompress(idat)
    stride, out, prev, i = w * 4, bytearray(), bytearray(w * 4), 0
    for _ in range(h):
        f = raw[i]; line = bytearray(raw[i + 1:i + 1 + stride]); i += 1 + stride
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line; prev = line
    return w, h, out

def write_png(w, h, px):
    raw = b''.join(b'\0' + bytes(px[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, c):
        return struct.pack('>I', len(c)) + t + c + struct.pack('>I', zlib.crc32(t + c) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def crop_square(w, h, px):
    """Recorta al dibujo (alfa > 16) y lo centra en un cuadrado con ~6 % de margen por lado"""
    xs = [x for y in range(h) for x in range(w) if px[(y * w + x) * 4 + 3] > 16]
    ys = [y for y in range(h) for x in range(w) if px[(y * w + x) * 4 + 3] > 16]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    cw, ch = x1 - x0 + 1, y1 - y0 + 1
    side = max(cw, ch) + max(cw, ch) // 8
    sq = bytearray(side * side * 4)
    ox, oy = (side - cw) // 2, (side - ch) // 2
    for y in range(ch):
        s = ((y + y0) * w + x0) * 4
        d = ((y + oy) * side + ox) * 4
        sq[d:d + cw * 4] = px[s:s + cw * 4]
    return side, sq

def resize(side, px, n):
    """Promedio por área (con alfa premultiplicado) de side x side a n x n"""
    out = bytearray(n * n * 4)
    for Y in range(n):
        sy0, sy1 = Y * side / n, (Y + 1) * side / n
        for X in range(n):
            sx0, sx1 = X * side / n, (X + 1) * side / n
            acc = [0.0] * 4; area = 0.0
            for y in range(int(sy0), min(side, int(sy1) + 1)):
                wy = min(sy1, y + 1) - max(sy0, y)
                if wy <= 0: continue
                for x in range(int(sx0), min(side, int(sx1) + 1)):
                    wx = min(sx1, x + 1) - max(sx0, x)
                    if wx <= 0: continue
                    k = (y * side + x) * 4; a = px[k + 3] / 255.0; wgt = wx * wy
                    acc[0] += px[k] * a * wgt; acc[1] += px[k + 1] * a * wgt; acc[2] += px[k + 2] * a * wgt
                    acc[3] += a * wgt; area += wgt
            k = (Y * n + X) * 4
            if acc[3] > 0:
                out[k:k + 3] = bytes(min(255, round(acc[i] / acc[3])) for i in range(3))
                out[k + 3] = min(255, round(acc[3] / area * 255))
    return out

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = pathlib.Path(__file__).resolve().parents[1]
    data = pathlib.Path(sys.argv[1]).read_bytes()
    lines = ['/*',
             ' * Icono de la ventana: mando del Neo Geo AES ("NEOGEO.png", serie TriMui, de KyleBing/retro-game-console-icons',
             ' * en GitHub: https://github.com/KyleBing/retro-game-console-icons), con licencia CC BY 4.0. PNG RGBA de',
             ' * 300x300 incluido tal cual (main_sdl.c lo recorta al dibujo); lo genera tools/make_icon.py.',
             ' */', '#ifndef ICONO_H', '#define ICONO_H', '', 'static const unsigned char icono_png[] = {']
    for i in range(0, len(data), 16):
        lines.append('    ' + ''.join('0x%02X,' % b for b in data[i:i + 16]))
    lines += ['};', '', '#endif', '']
    (root / 'src/platform/icono.h').write_text('\n'.join(lines), encoding='utf-8')
    w, h, px = read_png(data)
    side, sq = crop_square(w, h, px)
    ico_png = write_png(256, 256, resize(side, sq, 256))
    # ICO con una imagen PNG de 256x256 (ancho y alto 0 = 256)
    ico = struct.pack('<HHH', 0, 1, 1) + struct.pack('<BBBBHHII', 0, 0, 0, 0, 1, 32, len(ico_png), 22) + ico_png
    (root / 'src/platform/icono.ico').write_bytes(ico)
    print('icono.h (%d bytes) e icono.ico (%d bytes)' % (len(data), len(ico)))

if __name__ == '__main__':
    main()
