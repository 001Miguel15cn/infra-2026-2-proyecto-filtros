#!/usr/bin/env python3
"""Lee mediciones/motor.csv y escribe en Markdown la tabla T(p), S(p), E(p)
de un filtro. Sigue la metodología del enunciado: se descarta la primera
repetición de cada configuración y se reporta la mediana de las restantes.

    python3 bench/tabla.py ../mediciones/motor.csv sobel
"""
import csv
import statistics
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit("uso: tabla.py <motor.csv> <filtro>")
    ruta, filtro = sys.argv[1], sys.argv[2]

    por_hilos = {}
    with open(ruta, newline="") as f:
        for fila in csv.DictReader(f):
            if fila["filtro"] != filtro or int(fila["repeticion"]) == 1:
                continue
            por_hilos.setdefault(int(fila["hilos"]), []).append(float(fila["milisegundos"]))
    if 1 not in por_hilos:
        sys.exit(f"no hay mediciones con 1 hilo para '{filtro}'")

    t = {p: statistics.median(v) for p, v in sorted(por_hilos.items())}
    t1 = t[1]
    print(f"| p | T(p) [ms] | S(p) | E(p) | Karp-Flatt e |")
    print("|---|---|---|---|---|")
    for p, tp in t.items():
        s = t1 / tp
        e = s / p
        kf = (1 / s - 1 / p) / (1 - 1 / p) if p > 1 else float("nan")
        kf_txt = "-" if p == 1 else f"{kf:.3f}"
        print(f"| {p} | {tp:.1f} | {s:.2f} | {e:.2f} | {kf_txt} |")


if __name__ == "__main__":
    main()
