#!/usr/bin/env python3
"""Arma la tabla del perfilado a partir de las salidas de perf stat.

    python3 tabla_perf.py /tmp/perf_sobel_1.txt /tmp/perf_sobel_2.txt ...

Los archivos tienen que haberse generado con LC_ALL=C (punto decimal y sin
separador de miles). El número de hilos se toma del nombre: perf_sobel_<N>.txt.
"""
import re
import sys


def numero(texto):
    return float(texto.replace(",", ""))


def leer(ruta):
    t = open(ruta, encoding="utf-8", errors="replace").read()
    d = {}
    for ev in ("task-clock", "cycles", "instructions", "cache-references", "cache-misses"):
        m = re.search(r"^\s*([\d.,]+)\s+(?:msec\s+)?" + re.escape(ev) + r"\b", t, re.M)
        if not m:
            sys.exit(f"{ruta}: no aparece '{ev}' (¿corriste perf con -e task-clock,...? ¿con LC_ALL=C?)")
        d[ev] = numero(m.group(1))
    m = re.search(r"([\d.]+)\s+CPUs utilized", t)
    d["cpus"] = numero(m.group(1)) if m else None
    m = re.search(r"([\d.]+)\s+\+-\s+[\d.]+\s+seconds time elapsed", t) or \
        re.search(r"([\d.]+)\s+seconds time elapsed", t)
    if not m:
        sys.exit(f"{ruta}: no aparece el tiempo transcurrido")
    d["s"] = numero(m.group(1))
    return d


def main():
    if len(sys.argv) < 2:
        sys.exit("uso: tabla_perf.py perf_sobel_1.txt perf_sobel_2.txt ...")
    filas = []
    for ruta in sys.argv[1:]:
        h = re.search(r"_(\d+)\.txt$", ruta)
        if not h:
            sys.exit(f"{ruta}: el nombre debe terminar en _<hilos>.txt")
        filas.append((int(h.group(1)), leer(ruta)))
    filas.sort()
    print("| hilos | ciclos (mil millones) | instrucciones (mil millones) | IPC | "
          "fallos de caché (millones) | fallos (%) | CPUs utilizadas | tiempo transcurrido (s) |")
    print("|---|---|---|---|---|---|---|---|")
    for h, d in filas:
        ipc = d["instructions"] / d["cycles"]
        pct = 100.0 * d["cache-misses"] / d["cache-references"]
        cpus = "-" if d["cpus"] is None else f'{d["cpus"]:.2f}'
        print(f'| {h} | {d["cycles"]/1e9:.3f} | {d["instructions"]/1e9:.3f} | {ipc:.3f} | '
              f'{d["cache-misses"]/1e6:.3f} | {pct:.2f} | {cpus} | {d["s"]:.4f} |')


if __name__ == "__main__":
    main()
