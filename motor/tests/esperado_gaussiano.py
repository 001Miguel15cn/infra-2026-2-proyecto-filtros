#!/usr/bin/env python3
"""Calcula la salida esperada del gaussiano con una implementación aparte.

No es separable: aplica directamente el núcleo 2D de 5x5 (producto externo de
[1 4 6 4 1] consigo mismo, entre 256), con los bordes repetidos, y redondea al
final con los empates hacia arriba. Así la prueba compara dos formas de
llegar al mismo resultado.

    python3 tests/esperado_gaussiano.py entrada.ppm salida.ppm
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
    canales = 3 if magico == b"P6" else 1
    img = np.frombuffer(datos[i:], dtype=np.uint8).reshape(alto, ancho, canales)
    return magico, img


def gaussiano(img):
    k = np.array([1, 4, 6, 4, 1], dtype=np.float64)
    nucleo = np.outer(k, k) / 256.0
    alto, ancho, canales = img.shape
    p = np.pad(img.astype(np.float64), ((2, 2), (2, 2), (0, 0)), mode="edge")
    out = np.zeros((alto, ancho, canales))
    for dy in range(5):
        for dx in range(5):
            out += nucleo[dy, dx] * p[dy:dy + alto, dx:dx + ancho, :]
    return np.clip(np.floor(out + 0.5), 0, 255).astype(np.uint8)


if __name__ == "__main__":
    magico, img = leer(sys.argv[1])
    salida = gaussiano(img)
    with open(sys.argv[2], "wb") as f:
        f.write(magico + b"\n%d %d\n255\n" % (img.shape[1], img.shape[0]))
        f.write(salida.tobytes())
