#!/usr/bin/env python3
"""Despachador: reparte un lote de imágenes entre los trabajadores y mide el throughput.

Lee las imágenes de <datos>/entrada, las envía con varias peticiones en vuelo
al servicio de los trabajadores y escribe las salidas en <datos>/salida.

El despachador no sabe cuántos trabajadores hay ni dónde están: pide siempre
al mismo nombre (URL_TRABAJADOR, por defecto http://trabajador:8000) y la capa
de red escoge la réplica. Con Docker Compose lo hace la resolución de nombres
del servicio; en Kubernetes, el Service. Subir el número de trabajadores no
cambia una línea de este programa.

Configuración por variables de entorno (cada una tiene su opción de línea de
comandos equivalente):

    URL_TRABAJADOR   dirección del servicio           http://trabajador:8000
    CONCURRENCIA     peticiones en vuelo a la vez     8
    FILTROS          cadena de filtros                gris,gaussiano,sobel,otsu
    DATOS            carpeta con entrada/ y salida/   /datos
    GENERAR          si entrada/ está vacía, genera   64   (imágenes del lote)
    TAM              tamaño de las imágenes generadas 1024x1024
    REPORTE          CSV donde se agrega una fila     <datos>/servicio.csv
    ENTORNO          valor de la columna "entorno"    local
    TRABAJADORES_REPORTE  valor de la columna "trabajadores" (solo etiqueta: el
                     despachador no lo usa para repartir; lo fija quien lanza
                     la medición)

El CSV tiene las columnas entorno,trabajadores,concurrencia,imagenes,segundos.

Código de salida: 0 si todas las imágenes salieron, 1 si alguna falló, 2 si
los argumentos están mal o los trabajadores nunca estuvieron listos.
"""
import argparse
import csv
import os
import random
import sys
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor

CODIGOS_SIN_REINTENTO = (400, 411, 413, 501)


def esperar_listos(url, limite):
    """Espera a que el servicio conteste /readyz con 200. Devuelve True si lo logra."""
    fin = time.time() + limite
    while True:
        try:
            with urllib.request.urlopen(url.rstrip("/") + "/readyz", timeout=3) as r:
                if r.status == 200:
                    return True
        except (urllib.error.URLError, OSError):
            pass
        if time.time() >= fin:
            return False
        time.sleep(1)


def generar_lote(carpeta, cuantas, ancho, alto):
    """Escribe `cuantas` PPM de color con ruido reproducible (semilla = número
    de imagen). El lote sale igual en cada corrida, y así dos mediciones
    procesan exactamente los mismos bytes."""
    os.makedirs(carpeta, exist_ok=True)
    cabecera = b"P6\n%d %d\n255\n" % (ancho, alto)
    for i in range(1, cuantas + 1):
        datos = random.Random(i).randbytes(ancho * alto * 3)
        with open(os.path.join(carpeta, "img%03d.ppm" % i), "wb") as f:
            f.write(cabecera + datos)


def enviar(url, filtros, datos, tiempo_max):
    """POST /procesar. Devuelve (código, cuerpo, cabeceras). Lanza OSError si no
    se logra hablar con el servicio."""
    peticion = urllib.request.Request(
        url.rstrip("/") + "/procesar?filtros=" + filtros, data=datos, method="POST",
        headers={"Content-Type": "application/octet-stream"})
    try:
        with urllib.request.urlopen(peticion, timeout=tiempo_max) as r:
            return r.status, r.read(), r.headers
    except urllib.error.HTTPError as e:
        return e.code, e.read(), e.headers
    except urllib.error.URLError as e:
        raise OSError(str(e.reason))


def procesar_imagen(nombre, a):
    """Envía una imagen y guarda su salida. Devuelve (nombre, trabajador, estado, ms).

    Un fallo de red o un 5xx se reintenta: con el Service de por medio, la
    siguiente petición puede caer en otra réplica. Un 400, 413 o 501 no se
    reintenta, porque cualquier réplica daría lo mismo."""
    with open(os.path.join(a.entrada, nombre), "rb") as f:
        datos = f.read()
    estado = "sin intentar"
    for intento in range(a.reintentos + 1):
        if intento:
            time.sleep(min(2 ** intento * 0.25, 4))
        inicio = time.time()
        try:
            codigo, cuerpo, cab = enviar(a.url, a.filtros, datos, a.tiempo_max)
        except OSError as e:
            estado = "falla de red: %s" % e
            continue
        ms = (time.time() - inicio) * 1000.0
        if codigo == 200:
            ext = ".pgm" if cuerpo[:2] == b"P5" else ".ppm"
            destino = os.path.join(a.salida, os.path.splitext(nombre)[0] + ext)
            with open(destino, "wb") as f:
                f.write(cuerpo)
            return nombre, cab.get("X-Trabajador", "?"), "ok", ms
        estado = "falla %d: %s" % (codigo, cuerpo.decode("utf-8", "replace").strip())
        if codigo in CODIGOS_SIN_REINTENTO:
            return nombre, cab.get("X-Trabajador", "?"), estado, ms
    return nombre, "-", estado, 0.0


def entero_positivo(texto):
    n = int(texto)
    if n < 1:
        raise argparse.ArgumentTypeError("debe ser al menos 1")
    return n


def main():
    env = os.environ.get
    ap = argparse.ArgumentParser(description="Reparte un lote de imágenes entre los trabajadores.")
    ap.add_argument("--url", default=env("URL_TRABAJADOR", "http://trabajador:8000"),
                    help="dirección del servicio de trabajadores (URL_TRABAJADOR)")
    ap.add_argument("--concurrencia", type=entero_positivo, default=int(env("CONCURRENCIA", "8")),
                    help="peticiones en vuelo a la vez (CONCURRENCIA)")
    ap.add_argument("--filtros", default=env("FILTROS", "gris,gaussiano,sobel,otsu"),
                    help="cadena de filtros (FILTROS)")
    ap.add_argument("--datos", default=env("DATOS", "/datos"),
                    help="carpeta con entrada/ y salida/ (DATOS)")
    ap.add_argument("--generar", type=int, default=int(env("GENERAR", "64")),
                    help="si entrada/ está vacía, cuántas imágenes generar; 0 no genera (GENERAR)")
    ap.add_argument("--tam", default=env("TAM", "1024x1024"), help="tamaño de las generadas (TAM)")
    ap.add_argument("--reporte", default=env("REPORTE"), help="CSV del throughput (REPORTE)")
    ap.add_argument("--entorno", default=env("ENTORNO", "local"), help="columna entorno (ENTORNO)")
    ap.add_argument("--trabajadores", default=env("TRABAJADORES_REPORTE", "?"),
                    help="columna trabajadores, solo etiqueta (TRABAJADORES_REPORTE)")
    ap.add_argument("--reintentos", type=int, default=int(env("REINTENTOS", "3")),
                    help="veces que se repite una petición que falló por red o con 5xx")
    ap.add_argument("--tiempo-max", type=float, default=float(env("TIEMPO_MAX", "300")),
                    help="segundos por petición")
    ap.add_argument("--espera-listos", type=float, default=float(env("ESPERA_LISTOS", "120")),
                    help="segundos que se espera a que el servicio conteste /readyz")
    a = ap.parse_args()

    a.entrada = os.path.join(a.datos, "entrada")
    a.salida = os.path.join(a.datos, "salida")
    reporte = a.reporte or os.path.join(a.datos, "servicio.csv")

    os.makedirs(a.entrada, exist_ok=True)
    os.makedirs(a.salida, exist_ok=True)
    imagenes = sorted(f for f in os.listdir(a.entrada) if f.lower().endswith((".ppm", ".pgm")))
    if not imagenes and a.generar > 0:
        try:
            ancho, alto = (int(v) for v in a.tam.lower().split("x"))
        except ValueError:
            print("despachador: TAM debe ser AnchoxAlto, por ejemplo 1024x1024", file=sys.stderr)
            return 2
        print("despachador: entrada/ vacía, generando %d imágenes de %dx%d" % (a.generar, ancho, alto),
              flush=True)
        generar_lote(a.entrada, a.generar, ancho, alto)
        imagenes = sorted(os.listdir(a.entrada))
    if not imagenes:
        print("despachador: no hay imágenes en %s" % a.entrada, file=sys.stderr)
        return 2

    print("despachador: esperando a %s" % a.url, flush=True)
    if not esperar_listos(a.url, a.espera_listos):
        print("despachador: el servicio no estuvo listo en %.0f s" % a.espera_listos, file=sys.stderr)
        return 2

    print("despachador: %d imágenes, filtros %s, concurrencia %d" %
          (len(imagenes), a.filtros, a.concurrencia), flush=True)
    inicio = time.time()
    with ThreadPoolExecutor(max_workers=a.concurrencia) as pool:
        resultados = list(pool.map(lambda n: procesar_imagen(n, a), imagenes))
    segundos = time.time() - inicio

    fallas = 0
    por_trabajador = {}
    print("%-14s %-22s %10s  %s" % ("imagen", "trabajador", "ms", "estado"))
    for nombre, trabajador, estado, ms in resultados:
        print("%-14s %-22s %10.1f  %s" % (nombre, trabajador, ms, estado))
        if estado == "ok":
            por_trabajador[trabajador] = por_trabajador.get(trabajador, 0) + 1
        else:
            fallas += 1
    ok = len(imagenes) - fallas
    print("\n%d imágenes, %d ok, %d con falla, %.2f s en total" % (len(imagenes), ok, fallas, segundos))
    print("throughput: %.2f imágenes/s" % (ok / segundos if segundos > 0 else 0.0))
    for t, n in sorted(por_trabajador.items()):
        print("  %s: %d" % (t, n))

    # Una fila por corrida. Si hubo fallas el tiempo no es comparable, así que
    # no se registra: una cifra de throughput con imágenes perdidas engaña.
    if fallas == 0:
        nuevo = not os.path.exists(reporte)
        with open(reporte, "a", newline="") as f:
            w = csv.writer(f, lineterminator="\n")
            if nuevo:
                w.writerow(["entorno", "trabajadores", "concurrencia", "imagenes", "segundos"])
            w.writerow([a.entorno, a.trabajadores, a.concurrencia, len(imagenes), "%.2f" % segundos])
        print("fila agregada a %s" % reporte)
    return 1 if fallas else 0


if __name__ == "__main__":
    sys.exit(main())
