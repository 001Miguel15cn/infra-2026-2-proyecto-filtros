#!/usr/bin/env python3
"""Calcula la salida esperada de Sobel con una implementación aparte.

No comparte código con el motor: arma las dos convoluciones con sumas de
arreglos desplazados (bordes repetidos con np.pad modo "edge") y calcula
min(255, redondeo(sqrt(gx^2 + gy^2))) con los empates hacia arriba. La
entrada tiene que ser un PGM (un canal).

    python3 tests/esperado_sobel.py entrada.pgm salida.pgm
"""
import sys
import numpy as np


def leer(ruta):
    datos = open(ruta, "rb").read()
    campos, i = [], 0
    while len(campos) < 4:
        while datos[i:i + 1].isspace():
            i += 1
        if datos[i:i + 1] == b"#":
            i = datos.index(b"\n", i) + 1
            continue
        j = i
        while not datos[j:j + 1].isspace():
            j += 1
        campos.append(datos[i:j])
        i = j
    i += 1  # un solo separador antes de los píxeles
    magico, ancho, alto = campos[0], int(campos[1]), int(campos[2])
    if magico != b"P5":
        sys.exit("esperado_sobel: la entrada tiene que ser P5 (un canal)")
    return np.frombuffer(datos[i:], dtype=np.uint8).reshape(alto, ancho)


def sobel(img):
    alto, ancho = img.shape
    p = np.pad(img.astype(np.int64), 1, mode="edge")

    def v(dy, dx):  # vecino (x+dx, y+dy) de todos los píxeles a la vez
        return p[1 + dy:1 + dy + alto, 1 + dx:1 + dx + ancho]

    gx = (-v(-1, -1) + v(-1, 1) - 2 * v(0, -1) + 2 * v(0, 1) - v(1, -1) + v(1, 1))
    gy = (-v(-1, -1) - 2 * v(-1, 0) - v(-1, 1) + v(1, -1) + 2 * v(1, 0) + v(1, 1))
    mag = np.sqrt((gx * gx + gy * gy).astype(np.float64))
    return np.clip(np.floor(mag + 0.5), 0, 255).astype(np.uint8)


if __name__ == "__main__":
    img = leer(sys.argv[1])
    with open(sys.argv[2], "wb") as f:
        f.write(b"P5\n%d %d\n255\n" % (img.shape[1], img.shape[0]))
        f.write(sobel(img).tobytes())
