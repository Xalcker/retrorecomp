#!/usr/bin/env python3
"""
Genera los íconos de los juegos recompilados a partir de assets/iconos/*.png
(Retro game console icons de KyleBing, CC BY 4.0; ver assets/iconos/LEEME.md).

    python3 tools/iconos.py

Cada PNG (300x300, con el dibujo arriba y espacio vacío abajo) se recorta al
dibujo, se centra en un cuadrado con un margen y se escala. Escribe:

    src/platform/iconos.c / iconos.h    RGBA de 64x64 para el ícono de la ventana
    src/platform/icono_<x>.ico          ícono del .exe de Windows (16 a 256 px)

Los resultados se guardan en el repositorio: compilar no necesita Python.
Solo usa la biblioteca estándar.
"""
import os
import struct
import sys
import zlib

RAIZ = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
ICONOS = [('gb', 'GB.png'), ('gbc', 'GBC.png'), ('sgb', 'SGB.png')]
LADO_VENTANA = 64
TAMANOS_ICO = [16, 24, 32, 48, 64, 128, 256]
MARGEN = 0.06       # fracción del lado que queda libre alrededor del dibujo


def leer_png(ruta):
    """RGBA de 8 bits por canal: (ancho, alto, filas de bytearray)"""
    d = open(ruta, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit(f'{ruta}: no es un PNG')
    i, idat = 8, b''
    while i < len(d):
        n = struct.unpack('>I', d[i:i + 4])[0]
        tipo, datos = d[i + 4:i + 8], d[i + 8:i + 8 + n]
        if tipo == b'IHDR':
            w, h, bits, color, _, _, entrelazado = struct.unpack('>IIBBBBB', datos)
            if bits != 8 or color != 6 or entrelazado:
                sys.exit(f'{ruta}: se espera RGBA de 8 bits sin entrelazar')
        elif tipo == b'IDAT':
            idat += datos
        i += 12 + n
    crudo = zlib.decompress(idat)
    paso, filas, previa = w * 4, [], bytearray(w * 4)
    for y in range(h):
        filtro = crudo[y * (paso + 1)]
        f = bytearray(crudo[y * (paso + 1) + 1:(y + 1) * (paso + 1)])
        for x in range(paso):
            a = f[x - 4] if x >= 4 else 0
            b = previa[x]
            c = previa[x - 4] if x >= 4 else 0
            if filtro == 1:
                f[x] = (f[x] + a) & 255
            elif filtro == 2:
                f[x] = (f[x] + b) & 255
            elif filtro == 3:
                f[x] = (f[x] + (a + b) // 2) & 255
            elif filtro == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                f[x] = (f[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        filas.append(f)
        previa = f
    return w, h, filas


def escribir_png(w, h, rgba):
    crudo = b''.join(b'\x00' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def bloque(tipo, datos):
        return struct.pack('>I', len(datos)) + tipo + datos + struct.pack('>I', zlib.crc32(tipo + datos))
    return (b'\x89PNG\r\n\x1a\n' + bloque(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            bloque(b'IDAT', zlib.compress(crudo, 9)) + bloque(b'IEND', b''))


def cuadrado(w, h, filas):
    """Recorta al dibujo (alfa > 0) y lo centra en un cuadrado transparente"""
    xs = [x for y in range(h) for x in range(w) if filas[y][x * 4 + 3]]
    ys = [y for y in range(h) for x in range(w) if filas[y][x * 4 + 3]]
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    lado = int(max(x1 - x0, y1 - y0) / (1 - 2 * MARGEN))
    ox, oy = (lado - (x1 - x0)) // 2, (lado - (y1 - y0)) // 2
    out = bytearray(lado * lado * 4)
    for y in range(y0, y1):
        for x in range(x0, x1):
            d = ((oy + y - y0) * lado + ox + x - x0) * 4
            out[d:d + 4] = filas[y][x * 4:x * 4 + 4]
    return lado, out


def escalar(lado, rgba, n):
    """Promedio de área con alfa premultiplicado (sin halos en los bordes)"""
    out = bytearray(n * n * 4)
    f = lado / n
    for y in range(n):
        sy0, sy1 = y * f, (y + 1) * f
        for x in range(n):
            sx0, sx1 = x * f, (x + 1) * f
            acc = [0.0, 0.0, 0.0, 0.0]
            peso_total = 0.0
            for sy in range(int(sy0), min(lado, int(sy1 + 0.999))):
                py = min(sy + 1, sy1) - max(sy, sy0)
                for sx in range(int(sx0), min(lado, int(sx1 + 0.999))):
                    p = py * (min(sx + 1, sx1) - max(sx, sx0))
                    if p <= 0:
                        continue
                    i = (sy * lado + sx) * 4
                    a = rgba[i + 3] / 255
                    acc[0] += rgba[i] * a * p
                    acc[1] += rgba[i + 1] * a * p
                    acc[2] += rgba[i + 2] * a * p
                    acc[3] += a * p
                    peso_total += p
            d = (y * n + x) * 4
            if acc[3] > 0:
                for c in range(3):
                    out[d + c] = min(255, round(acc[c] / acc[3]))
            out[d + 3] = min(255, round(255 * acc[3] / peso_total))
    return out


def escribir_ico(ruta, imagenes):
    """ICO con una entrada PNG por tamaño (Windows Vista en adelante)"""
    cab = struct.pack('<HHH', 0, 1, len(imagenes))
    datos, entradas, pos = b'', b'', 6 + 16 * len(imagenes)
    for n, png in imagenes:
        entradas += struct.pack('<BBBBHHII', n % 256, n % 256, 0, 0, 1, 32, len(png), pos)
        datos += png
        pos += len(png)
    open(ruta, 'wb').write(cab + entradas + datos)


def main():
    plataforma = os.path.join(RAIZ, 'src', 'platform')
    c = ['/*',
         ' * Íconos de la ventana (RGBA, %dx%d), generados por tools/iconos.py.' % (LADO_VENTANA, LADO_VENTANA),
         ' * Retro game console icons de KyleBing',
         ' * (https://github.com/KyleBing/retro-game-console-icons), CC BY 4.0;',
         ' * recortados, centrados y escalados. No editar.',
         ' */',
         '#include "iconos.h"', '']
    for nombre, archivo in ICONOS:
        w, h, filas = leer_png(os.path.join(RAIZ, 'assets', 'iconos', archivo))
        lado, cuad = cuadrado(w, h, filas)
        ventana = escalar(lado, cuad, LADO_VENTANA)
        c.append('const unsigned char icono_%s[ICONO_LADO * ICONO_LADO * 4] = {' % nombre)
        for i in range(0, len(ventana), 16):
            c.append('    ' + ','.join('0x%02X' % v for v in ventana[i:i + 16]) + ',')
        c.append('};')
        c.append('')
        imagenes = [(n, escribir_png(n, n, escalar(lado, cuad, n))) for n in TAMANOS_ICO]
        escribir_ico(os.path.join(plataforma, 'icono_%s.ico' % nombre), imagenes)
        print('icono_%s.ico: %s px' % (nombre, ', '.join(str(n) for n in TAMANOS_ICO)))
    open(os.path.join(plataforma, 'iconos.c'), 'w').write('\n'.join(c))
    open(os.path.join(plataforma, 'iconos.h'), 'w').write(
        '/*\n'
        ' * Íconos de la ventana: Game Boy, Game Boy Color y Super Game Boy (un control\n'
        ' * de SNES). RGBA de ICONO_LADO x ICONO_LADO. Ver iconos.c y tools/iconos.py.\n'
        ' */\n'
        '#ifndef ICONOS_H\n#define ICONOS_H\n\n'
        '#define ICONO_LADO %d\n\n'
        'extern const unsigned char icono_gb[ICONO_LADO * ICONO_LADO * 4];\n'
        'extern const unsigned char icono_gbc[ICONO_LADO * ICONO_LADO * 4];\n'
        'extern const unsigned char icono_sgb[ICONO_LADO * ICONO_LADO * 4];\n\n'
        '#endif\n' % LADO_VENTANA)
    print('iconos.c / iconos.h: %dx%d' % (LADO_VENTANA, LADO_VENTANA))


if __name__ == '__main__':
    main()
