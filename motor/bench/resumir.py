#!/usr/bin/env python3
"""Resume mediciones/motor.csv: mediana por filtro e hilos, speedup y eficiencia.

La mediana descarta la primera repetición (igual que el arnés). El speedup de
cada filtro es su tiempo con 1 hilo entre su tiempo con p hilos, y la
eficiencia es el speedup entre p.

    python3 bench/resumir.py [../mediciones/motor.csv]
"""
import csv
import statistics
import sys
from collections import defaultdict

ruta = sys.argv[1] if len(sys.argv) > 1 else "../mediciones/motor.csv"
tiempos = defaultdict(list)
with open(ruta, newline="") as f:
    for fila in csv.DictReader(f):
        if int(fila["repeticion"]) > 1:
            tiempos[(fila["filtro"], int(fila["hilos"]))].append(float(fila["milisegundos"]))

mediana = {k: statistics.median(v) for k, v in tiempos.items()}
filtros = sorted({k[0] for k in mediana})

print("| filtro | hilos | mediana (ms) | speedup | eficiencia |")
print("|---|---|---|---|---|")
for filtro in filtros:
    t1 = mediana.get((filtro, 1))
    for (fl, p), t in sorted(mediana.items()):
        if fl != filtro:
            continue
        s = t1 / t if t1 else float("nan")
        print(f"| {filtro} | {p} | {t:.1f} | {s:.2f} | {s / p:.2f} |")

# Antes y después de la optimización de localidad, a igual número de hilos.
for base in filtros:
    nuevo = base + "_bandas"
    if nuevo in filtros:
        print(f"\n{base} contra {nuevo}:\n")
        print("| hilos | antes (ms) | después (ms) | mejora |")
        print("|---|---|---|---|")
        for (fl, p), t in sorted(mediana.items()):
            if fl == base and (nuevo, p) in mediana:
                d = mediana[(nuevo, p)]
                print(f"| {p} | {t:.1f} | {d:.1f} | {t / d:.2f}x |")

# Un solo hilo: forma directa de 5x5 contra la separable. La mejora algorítmica
# (25 multiplicaciones contra 10 por píxel) se mide antes de hablar de hilos.
if "gaussiano_directo" in filtros and ("gaussiano", 1) in mediana:
    td, ts = mediana[("gaussiano_directo", 1)], mediana[("gaussiano", 1)]
    print("\nUn solo hilo, forma directa contra separable:\n")
    print("| forma | mediana (ms) | frente a la directa |")
    print("|---|---|---|")
    print(f"| directa 5x5 | {td:.1f} | 1.00x |")
    print(f"| separable | {ts:.1f} | {td / ts:.2f}x más rápida |")
