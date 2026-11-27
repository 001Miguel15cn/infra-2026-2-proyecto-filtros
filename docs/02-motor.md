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
| `gaussiano_directo` | forma directa 2D de 5x5 (25 multiplicaciones por píxel), solo secuencial; es la referencia contra la que se compara la separable |
| `gaussiano_paralelo` | las dos pasadas con `omp parallel for` por filas, con la intermedia completa en memoria |
| `gaussiano_paralelo_bandas` | pedida como `gaussiano_bandas`; ver la optimización de localidad |

## Paralelización

El gaussiano es un *stencil* en dos pasadas. Cada fila de la intermedia sale de
una sola fila de la entrada, y cada fila de salida lee cinco filas de la
intermedia y escribe solo la suya. No hay datos compartidos que se escriban, así
que basta `#pragma omp parallel for schedule(static)` sobre el ciclo de las
filas, en cada pasada. Entre las dos pasadas hay una barrera, que es la del
final del primer `for`: la vertical no puede empezar hasta que la intermedia
esté completa.

**Por qué filas y no columnas.** La imagen está almacenada por filas, así que
los píxeles de una fila son contiguos en memoria. Repartir filas deja a cada
hilo con un bloque contiguo de la entrada y de la salida: dentro de una fila
cada línea de caché de 64 bytes se aprovecha entera (los 64 bytes son
vecinos que se van a leer enseguida) y el prefetcher de hardware sigue el
recorrido. Repartir columnas haría que cada hilo recorriera con paso
`ancho x canales` bytes, trayendo una línea de caché nueva en cada paso para
usar un solo píxel de ella, y que hilos distintos escribieran bytes vecinos de
una misma línea (*false sharing* en la salida). El reparto por filas solo
comparte líneas de caché en la frontera entre dos bloques de filas.

**Política de planificación.** `schedule(static)`: todas las filas cuestan lo
mismo (el núcleo es fijo y no depende de los datos), así que un reparto estático
en bloques contiguos equilibra la carga sin el costo de coordinación de
`dynamic` o `guided`, y mantiene la localidad (cada hilo conserva su bloque).

**Pasada vertical.** Es la que castiga la localidad: para producir una fila de
salida lee cinco filas de la intermedia. La versión de dos pasadas completas
escribe y relee la intermedia entera (unos 134 MB en `double` para la imagen de
medición en grises), y de ahí sale la optimización por bandas de más abajo.

## Corrección

`make pruebas` compara:

- la salida contra `tests/esperado_gaussiano.py`, una implementación aparte
  que aplica el núcleo 2D de 5x5 directo (no separable), sobre la imagen de
  4x3, sobre un canal y sobre una de 37x41 (no es múltiplo de la banda);
- paralelo, dos pasadas y bandas contra la secuencial en 512x512, **con 1 y
  con 8 hilos** (`OMP_NUM_THREADS`), y la cadena `gris,gaussiano` igual;
- la forma directa de 5x5 (`gaussiano_directo`) contra la separable, byte a
  byte: las dos formas dan la misma salida porque todos los pesos son enteros y
  las sumas en `double` son exactas;
- tamaños de 1x1, 2x3, 5x1, 1x7, 16x16 y 17x33, donde todo o casi todo es borde,
  con 8 hilos.

Se corren con `make -C motor test`, que es la orden que usará el flujo de
GitHub Actions.

## Metodología de medición

- Imagen de 4096x4096 a color, escrita por `generar`.
- Se cronometra solo la llamada al filtro, con la imagen ya en memoria
  (`omp_get_wtime`).
- 5 repeticiones por filtro e hilos; se descarta la primera y se toma la
  mediana de las otras cuatro.
- Hilos: 1, 2, 4 y 8, con `OMP_NUM_THREADS`.
- Las métricas, con $T(p)$ el tiempo mediano con $p$ hilos:

  $$S(p) = \frac{T(1)}{T(p)} \qquad E(p) = \frac{S(p)}{p}$$

- Los filtros de un canal se miden sobre la imagen ya en grises, que es lo que
  reciben dentro de la cadena; la conversión se hace antes de arrancar el
  cronómetro (`--gris` del arnés). La cadena (`gris,gaussiano,sobel,otsu`) se
  mide completa.
- `bash bench/medir.sh imagen.ppm gaussiano gaussiano_bandas gaussiano_directo`
  deja `mediciones/motor.csv`, y `python3 bench/resumir.py` saca las tablas
  (`gaussiano_directo` se mide solo con 1 hilo).

Máquina: Lenovo LOQ 15IAX9E, Intel Core i5-12450HX (8 núcleos físicos, 12 hilos
lógicos), 16 GB de RAM, SSD de 512 GB, Windows 11 Pro, g++ 16.2.0 (MSYS2 UCRT64)
con `-O2 -fopenmp`. Todas las tablas de esta página salen de esta máquina y de
una sola corrida de `bench/medir.sh` (`mediciones/motor.csv`). El reloj de
`omp_get_wtime` en esta plataforma avanza de a 0.5 ms, que es como máximo un
1.6 % de error para los 32 ms del tiempo más corto medido.

Los filtros de un canal se miden sobre la imagen en grises (`--gris`), así que
la imagen intermedia del gaussiano es de 4096 x 4096 x 8 bytes, unos 134 MB.

## Un hilo: separable contra forma directa

Antes de hablar de hilos se compara el algoritmo. La forma directa de 5x5 hace
25 multiplicaciones por píxel; la separable, 10 (5 en cada pasada).
`gaussiano_directo` y `gaussiano` con `OMP_NUM_THREADS=1` se miden con el mismo
arnés y la misma imagen.

| forma | mediana con 1 hilo (ms) | frente a la directa |
|---|---|---|
| directa 5x5 | 450.5 | 1.00x |
| separable | 174.0 | 2.59x más rápida |

La razón teórica de multiplicaciones es $25/10 = 2.5$ y la medida es 2.59, casi
igual. Con un solo hilo el filtro está limitado por el cómputo, no por la
memoria, y el tiempo sigue al número de multiplicaciones. Esta mejora
algorítmica (2.59x con un hilo) es del orden de lo que después aportan 4 hilos
a la versión de dos pasadas (2.66x, ver más abajo), y por eso se reporta antes
de los hilos.

## Resultados

Dos pasadas (antes de optimizar):

| hilos | mediana (ms) | speedup | eficiencia |
|---|---|---|---|
| 1 | 174.0 | 1.00 | 1.00 |
| 2 | 94.0 | 1.85 | 0.93 |
| 4 | 65.5 | 2.66 | 0.66 |
| 8 | 63.0 | 2.76 | 0.35 |

Por bandas (después):

| hilos | mediana (ms) | speedup | eficiencia |
|---|---|---|---|
| 1 | 119.5 | 1.00 | 1.00 |
| 2 | 62.0 | 1.93 | 0.96 |
| 4 | 38.0 | 3.14 | 0.79 |
| 8 | 32.0 | 3.73 | 0.47 |

Antes y después a igual número de hilos:

| hilos | antes (ms) | después (ms) | mejora |
|---|---|---|---|
| 1 | 174.0 | 119.5 | 1.46x |
| 2 | 94.0 | 62.0 | 1.52x |
| 4 | 65.5 | 38.0 | 1.72x |
| 8 | 63.0 | 32.0 | 1.97x |

La versión de dos pasadas casi no mejora de 4 a 8 hilos (65.5 ms contra 63.0 ms,
speedup de 2.66 a 2.76) y su eficiencia con 8 hilos es de 0.35. La de bandas
sigue bajando hasta 8 hilos (38.0 a 32.0 ms), con eficiencia de 0.79 con 4
hilos y 0.47 con 8. Ninguna de las dos llega a la recta ideal con 8 hilos.

## Optimización de localidad: fusión por bandas

**Por qué esta optimización.** Con la tabla de eficiencia en la mano, el
gaussiano de dos pasadas es el que peor escala de los medidos: 0.35 con 8 hilos
y un speedup que se aplana desde 4 hilos.

**Problema.** Con dos pasadas completas, la intermedia en `double` ocupa
4096 x 4096 x 8 bytes, unos 134 MB. Se escribe entera en la primera pasada y se
vuelve a leer entera en la segunda. El filtro hace pocas cuentas por byte que
mueve, y esa intermedia no cabe en la caché, así que cada hilo mueve datos de y
hacia la memoria principal, un recurso que los hilos comparten. Esto es
coherente con lo que se ve (speedup que se aplana desde 4 hilos), pero la
medición del perfilado (ver abajo) es la que debe confirmarlo.

**Cambio.** La imagen se parte en bandas de 32 filas. Cada hilo filtra en
horizontal las 36 filas que necesita (32 más 2 de borde arriba y 2 abajo) hacia
un búfer propio, y con eso hace la pasada vertical de su banda. El búfer tiene
36 x 4096 x 8 bytes, unos 1.2 MB, y se reutiliza, así que la intermedia casi
nunca sale de la caché. A cambio, las 4 filas de borde de cada banda se filtran
dos veces (12.5 % más de cuentas horizontales).

**Efecto.** Con un solo hilo baja de 174.0 a 119.5 ms (1.46x): el cambio ya
ayuda sin paralelismo, porque se evita el viaje de ida y vuelta de la
intermedia. La mejora crece con los hilos (1.52x, 1.72x y 1.97x con 2, 4 y 8) y
el speedup con 8 hilos pasa de 2.76 a 3.73. La eficiencia con 8 hilos sigue
siendo 0.47; no se midió por separado qué la limita (núcleos de distinto tipo,
ancho de banda de memoria u otra causa), y no se afirma sin esa medición.

**Tamaño de banda.** El valor de 32 filas se escogió probando 4, 8, 16, 32, 64
y 128 en otra máquina (de 2 CPU): 4 y 8 fueron peores y de 32 en adelante casi
no cambió. El valor óptimo depende del tamaño de la caché por núcleo
(`BANDA_FILAS` en `filtros.hpp`).

> **PENDIENTE:** el barrido de la banda salió de una máquina distinta a la
> declarada, y el enunciado pide que todas las mediciones salgan de la misma.
> Repetirlo en el i5-12450HX o quitar la frase.

## Evidencia del perfilado

> **PENDIENTE:** `bash bench/perfilar.sh imagen.ppm 4` (perf stat, en WSL) deja la evidencia en `mediciones/perfilado.txt`. El enunciado exige **una** herramienta de perfilado, con su
> salida y la conclusión que se saca de ella (`perf stat`, `perf record`,
> `valgrind --tool=callgrind` o `/usr/bin/time -v`). Hay que correrla antes y
> después de la optimización por bandas y concluir qué número de la medición
> explica el cambio (por ejemplo, fallos de caché de `perf stat`, o memoria
> residente máxima de `/usr/bin/time -v`, que baja de ~134 MB de intermedia a
> ~1.2 MB por hilo). En Windows se hace dentro de WSL.
