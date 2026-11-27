# El motor

## Filtro implementado: desenfoque gaussiano separable

Núcleo binomial `[1 4 6 4 1]/16` en una pasada horizontal y otra vertical. Se
respetan las convenciones del README: bordes por repetición del píxel más
cercano, acumulación en `double` y redondeo solo al final. La intermedia no se
redondea. Como los pesos son enteros y se divide entre 16, las sumas en
`double` son exactas, y por eso las tres versiones dan la misma salida byte a
byte.

Las versiones, en `motor/src/filtros.cpp`:

| Versión | Qué hace |
|---|---|
| `gaussiano_secuencial` | las dos pasadas con un hilo |
| `gaussiano_paralelo` | las dos pasadas con `omp parallel for` por filas, con la intermedia completa en memoria |
| `gaussiano_paralelo_bandas` | pedida como `gaussiano_bandas`; ver la optimización de localidad |

## Paralelización

Cada fila de la intermedia sale de una sola fila de la entrada, y cada fila de
salida lee cinco filas de la intermedia y escribe solo la suya. No hay datos
compartidos que se escriban, así que basta repartir filas con
`schedule(static)`. Entre las dos pasadas hay una barrera, que es la del final
del primer `for`.

## Corrección

`make pruebas` compara:

- la salida contra `tests/esperado_gaussiano.py`, una implementación aparte
  que aplica el núcleo 2D de 5x5 directo (no separable), sobre la imagen de
  4x3, sobre un canal y sobre una de 37x41 (no es múltiplo de la banda);
- paralelo, dos pasadas y bandas contra la secuencial en 512x512;
- tamaños de 1x1, 2x3, 5x1, 1x7, 16x16 y 17x33, donde todo o casi todo es borde.

## Metodología de medición

- Imagen de 4096x4096 a color, escrita por `generar`.
- Se cronometra solo la llamada al filtro, con la imagen ya en memoria
  (`omp_get_wtime`).
- 5 repeticiones por filtro e hilos; se descarta la primera y se toma la
  mediana de las otras cuatro.
- Hilos: 1, 2, 4 y 8, con `OMP_NUM_THREADS`.
- Speedup = T(1 hilo) / T(p hilos) del mismo filtro; eficiencia = speedup / p.
- `bash bench/medir.sh imagen.ppm gaussiano gaussiano_bandas` deja
  `mediciones/motor.csv`, y `python3 bench/resumir.py` saca las tablas.

Máquina: Lenovo LOQ 15IAX9E, Intel Core i5-XXXXX (N núcleos físicos, 12 hilos),
16 GB de RAM, SSD de 512 GB, Windows 11 Pro, g++ 16.2.0 (MSYS2 UCRT64) con
`-O2 -fopenmp`. El
reloj de `omp_get_wtime` en esta plataforma avanza de a 0.5 ms, que no
afecta a tiempos de 80 ms o más.

## Resultados

Dos pasadas (antes de optimizar):

| hilos | mediana (ms) | speedup | eficiencia |
|---|---|---|---|
| 1 | 461.0 | 1.00 | 1.00 |
| 2 | 266.5 | 1.73 | 0.86 |
| 4 | 189.0 | 2.44 | 0.61 |
| 8 | 190.0 | 2.43 | 0.30 |

Por bandas (después):

| hilos | mediana (ms) | speedup | eficiencia |
|---|---|---|---|
| 1 | 318.0 | 1.00 | 1.00 |
| 2 | 161.5 | 1.97 | 0.98 |
| 4 | 91.5 | 3.48 | 0.87 |
| 8 | 82.0 | 3.88 | 0.48 |

Antes y después a igual número de hilos:

| hilos | antes (ms) | después (ms) | mejora |
|---|---|---|---|
| 1 | 461.0 | 318.0 | 1.45x |
| 2 | 266.5 | 161.5 | 1.65x |
| 4 | 189.0 | 91.5 | 2.07x |
| 8 | 190.0 | 82.0 | 2.32x |

La versión de dos pasadas deja de mejorar a partir de 4 hilos (189 ms con 4 y
190 con 8). La de bandas sigue bajando hasta 8 hilos y con 4 mantiene una
eficiencia de 0.87.

## Optimización de localidad: fusión por bandas

**Problema.** Con dos pasadas completas, la intermedia en `double` ocupa
4096 x 4096 x 3 x 8 bytes, unos 400 MB. Se escribe entera en la primera pasada
y se vuelve a leer entera en la segunda. El filtro hace pocas cuentas por byte
que mueve, así que lo limita el ancho de banda de memoria, y ese es un recurso
que los hilos comparten: con 2 hilos el speedup es 1.73x, con 4 llega a
2.44x y con 8 ya no mejora.

**Cambio.** La imagen se parte en bandas de 32 filas. Cada hilo filtra en
horizontal las 36 filas que necesita (32 más 2 de borde arriba y 2 abajo) hacia
un búfer propio, y con eso hace la pasada vertical de su banda. El búfer tiene
36 x 4096 x 3 x 8 = 3.5 MB y se reutiliza, así que la intermedia casi nunca
sale de caché. A cambio, las 4 filas de borde de cada banda se filtran dos
veces (cerca de 12 % más de cuentas horizontales).

**Efecto.** Con un hilo ya baja de 461 a 318 ms, porque se evita el viaje de
ida y vuelta a memoria principal. El speedup con 8 hilos pasa de 2.43 a 3.88,
y el tiempo absoluto de 190 a 82 ms (2.32x). A 8 hilos la eficiencia sigue
siendo baja (0.48); lo más probable es que el ancho de banda y el número de
núcleos físicos de este equipo ya sean el límite, pero no se midió por
separado.

**Tamaño de banda.** El valor de 32 filas se escogió probando 4, 8, 16, 32, 64
y 128 en otra máquina (de 2 CPU): 4 y 8 fueron peores y de 32 en adelante casi
no cambió. No se repitió el barrido en este equipo, y el valor óptimo depende
del tamaño de la caché por núcleo (`BANDA_FILAS` en `filtros.hpp`).
