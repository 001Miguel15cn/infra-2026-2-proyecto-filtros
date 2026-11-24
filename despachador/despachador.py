#!/usr/bin/env python3
"""Despachador: reparte un lote de imágenes entre los trabajadores.

    python3 despachador.py --trabajadores http://h1:8080,http://h2:8080 \\
        --filtros gris,gaussiano --entrada lote/ --salida resultado/

Cada trabajador tiene su hilo, que toma la siguiente imagen de una cola común
apenas termina la anterior: el que es más rápido atiende más imágenes. Si un
trabajador no responde, su imagen vuelve a la cola para otro y no se le
vuelve a enviar nada. Las imágenes que el trabajador rechaza (400, 501) no se
reintentan, porque con otro trabajador darían lo mismo.

Código de salida: 0 si todas salieron, 1 si alguna falló, 2 si no hay
trabajadores disponibles o los argumentos están mal.
"""
import argparse
import csv
import os
import queue
import sys
import threading
import time
import urllib.error
import urllib.request


def consultar_salud(url, tiempo=3):
    try:
        with urllib.request.urlopen(url.rstrip("/") + "/salud", timeout=tiempo) as r:
            return r.status == 200
    except (urllib.error.URLError, OSError):
        return False


def enviar(url, filtros, datos, tiempo_max):
    """Devuelve (código, cuerpo). Lanza OSError si no se logra hablar con el trabajador."""
    peticion = urllib.request.Request(
        url.rstrip("/") + "/procesar?filtros=" + filtros, data=datos, method="POST",
        headers={"Content-Type": "application/octet-stream"})
    try:
        with urllib.request.urlopen(peticion, timeout=tiempo_max) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
    except urllib.error.URLError as e:
        raise OSError(str(e.reason))


def main():
    ap = argparse.ArgumentParser(description="Reparte un lote de imágenes entre trabajadores.")
    ap.add_argument("--trabajadores", default=os.environ.get("TRABAJADORES", ""),
                    help="URLs separadas por comas (o variable TRABAJADORES)")
    ap.add_argument("--filtros", required=True, help="cadena de filtros, por ejemplo gris,gaussiano")
    ap.add_argument("--entrada", required=True, help="carpeta con los .ppm y .pgm del lote")
    ap.add_argument("--salida", required=True, help="carpeta donde se escriben los resultados")
    ap.add_argument("--informe", help="archivo CSV con una fila por imagen")
    ap.add_argument("--reintentos", type=int, default=2,
                    help="veces que una imagen vuelve a la cola si su trabajador cae")
    ap.add_argument("--tiempo-max", type=float, default=300, help="segundos por petición")
    a = ap.parse_args()

    urls = [u.strip() for u in a.trabajadores.split(",") if u.strip()]
    if not urls:
        print("despachador: faltan los trabajadores", file=sys.stderr)
        return 2
    imagenes = sorted(f for f in os.listdir(a.entrada) if f.lower().endswith((".ppm", ".pgm")))
    if not imagenes:
        print("despachador: no hay imágenes en %s" % a.entrada, file=sys.stderr)
        return 2
    os.makedirs(a.salida, exist_ok=True)

    vivos = []
    for u in urls:
        if consultar_salud(u):
            vivos.append(u)
        else:
            print("despachador: %s no responde, se omite" % u, file=sys.stderr)
    if not vivos:
        print("despachador: ningún trabajador disponible", file=sys.stderr)
        return 2

    cola = queue.Queue()
    for nombre in imagenes:
        cola.put((nombre, 0))
    pendientes = [len(imagenes)]  # imágenes sin resultado definitivo
    candado = threading.Lock()
    filas = {}  # nombre -> (trabajador, estado, ms)

    def terminar(nombre, trabajador, estado, ms):
        with candado:
            filas[nombre] = (trabajador, estado, ms)
            pendientes[0] -= 1

    def hilo(url):
        while True:
            try:
                nombre, intentos = cola.get(timeout=0.2)
            except queue.Empty:
                if pendientes[0] == 0:
                    return
                continue
            with open(os.path.join(a.entrada, nombre), "rb") as f:
                datos = f.read()
            inicio = time.time()
            try:
                codigo, cuerpo = enviar(url, a.filtros, datos, a.tiempo_max)
            except OSError as e:
                # El trabajador no contestó: la imagen vuelve a la cola y
                # este hilo se retira.
                if intentos < a.reintentos:
                    cola.put((nombre, intentos + 1))
                else:
                    terminar(nombre, url, "falla: %s" % e, 0)
                print("despachador: %s cayó (%s)" % (url, e), file=sys.stderr)
                return
            ms = (time.time() - inicio) * 1000.0
            if codigo == 200:
                ext = ".pgm" if cuerpo[:2] == b"P5" else ".ppm"
                destino = os.path.join(a.salida, os.path.splitext(nombre)[0] + ext)
                with open(destino, "wb") as f:
                    f.write(cuerpo)
                terminar(nombre, url, "ok", ms)
            elif codigo in (400, 413, 501):
                terminar(nombre, url, "falla %d: %s" % (codigo, cuerpo.decode("utf-8", "replace").strip()), ms)
            elif intentos < a.reintentos:
                cola.put((nombre, intentos + 1))
            else:
                terminar(nombre, url, "falla %d: %s" % (codigo, cuerpo.decode("utf-8", "replace").strip()), ms)

    inicio = time.time()
    hilos = [threading.Thread(target=hilo, args=(u,)) for u in vivos]
    for h in hilos:
        h.start()
    for h in hilos:
        h.join()
    total = time.time() - inicio

    # Si cayeron todos los trabajadores, lo que quedó en la cola no se hizo.
    while True:
        try:
            nombre, _ = cola.get_nowait()
        except queue.Empty:
            break
        terminar(nombre, "-", "falla: sin trabajadores disponibles", 0)

    fallas = 0
    por_trabajador = {}
    print("%-24s %-28s %10s  %s" % ("imagen", "trabajador", "ms", "estado"))
    for nombre in imagenes:
        t, estado, ms = filas[nombre]
        print("%-24s %-28s %10.1f  %s" % (nombre, t, ms, estado))
        if estado == "ok":
            por_trabajador[t] = por_trabajador.get(t, 0) + 1
        else:
            fallas += 1
    print("\n%d imágenes, %d ok, %d con falla, %.2f s en total" %
          (len(imagenes), len(imagenes) - fallas, fallas, total))
    for t, n in sorted(por_trabajador.items()):
        print("  %s: %d" % (t, n))

    if a.informe:
        with open(a.informe, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["imagen", "trabajador", "milisegundos", "estado"])
            for nombre in imagenes:
                t, estado, ms = filas[nombre]
                w.writerow([nombre, t, "%.1f" % ms, estado])
    return 1 if fallas else 0


if __name__ == "__main__":
    sys.exit(main())
