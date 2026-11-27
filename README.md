# Procesamiento de imágenes por lotes

Motor de filtros en C++ con OpenMP, un trabajador que lo expone por HTTP y un
despachador que reparte el lote entre varios trabajadores.

## Integrantes

| Nombre completo | Código | Correo institucional |
|---|---|---|
| Miguel Angel Arboleda Grueso | 2160253 | miguel.arboleda@correounivalle.edu.co |
| Daniel Felipe Gonzalez Perez | 2459631 | daniel.gonzalez.perez@correounivalle.edu.co |
| Sebastian Devia Acosta|202459664| devia.sebastian@correounivalle.edu.co|
|  |  |  |

El informe está en [`docs/`](docs/).

## Qué trae el repositorio

```
motor/            el motor en C++ con OpenMP
  src/            lectura y escritura de PPM y PGM, y los filtros
  tests/          pruebas de corrección y el comparador byte a byte
  bench/          arnés de medición y generador de la imagen de medición
trabajador/       capa HTTP sobre el motor
despachador/      reparto del lote
mediciones/       las tablas que alimentan el informe
deploy/local/     el servicio completo con Docker Compose
deploy/nube/      los manifiestos del clúster
docs/             el informe, en cuatro archivos más el índice
```

Están resueltos la escala de grises, el desenfoque de caja de 3x3, el
desenfoque gaussiano separable (este último con una variante por bandas,
`gaussiano_bandas`) y Sobel; ver `docs/02-motor.md`. El histograma con Otsu
está declarado en `motor/src/filtros.hpp` y sin implementar.

## Construir y correr el motor

```console
$ cd motor
$ make
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o motor src/main.cpp src/filtros.cpp src/imagen.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o comparar tests/comparar.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o generar bench/generar.cpp src/imagen.cpp
g++ -std=c++17 -O2 -Wall -Wextra -fopenmp -Isrc -o arnes bench/arnes.cpp src/filtros.cpp src/imagen.cpp
```

El motor recibe la cadena de filtros separados por comas, la imagen de entrada
y la de salida. Con `--secuencial` corre la versión de un hilo:

```console
$ ./motor gris tests/datos/entrada.ppm /tmp/salida.pgm
$ ./motor caja,caja tests/datos/entrada.ppm /tmp/suave.ppm --secuencial
```

La cadena del lote es `gris,gaussiano,sobel,otsu`: entra un PPM de color y
sale un PGM binario. Cada filtro necesita cierto número de canales y entrega
otro, así que el motor recorre la cadena llevando la cuenta y la rechaza antes
de aplicar nada cuando un filtro no va a recibir lo que necesita. `gris` pide
tres canales y devuelve uno; `sobel` y `otsu` piden uno; `caja` y `gaussiano`
trabajan con cualquiera de los dos.

## Las pruebas

```console
$ make pruebas
Salida esperada, calculada aparte:
  ok    gris sobre la imagen de prueba
  ok    caja sobre la imagen de prueba
  ok    gaussiano sobre la imagen de prueba
  ok    gaussiano sobre un canal
  ok    gaussiano sobre 37x41, contra la implementación aparte
  ok    gaussiano_bandas sobre 37x41, contra la implementación aparte
Lectura y escritura:
  ok    gris rechaza una imagen de un canal
  ok    caja sobre un canal escribe P5
Paralelo contra secuencial, imagen de 512x512:
  ok    gris idéntico con uno y con varios hilos
  ok    caja idéntico con uno y con varios hilos
  ok    gaussiano idéntico con uno y con varios hilos
  ok    gaussiano_bandas idéntico con uno y con varios hilos
Tamaños pequeños y bordes, gaussiano:
  ok    gaussiano 1x1: dos pasadas y bandas iguales a la secuencial
  ok    gaussiano 2x3: dos pasadas y bandas iguales a la secuencial
  ok    gaussiano 5x1: dos pasadas y bandas iguales a la secuencial
  ok    gaussiano 1x7: dos pasadas y bandas iguales a la secuencial
  ok    gaussiano 16x16: dos pasadas y bandas iguales a la secuencial
  ok    gaussiano 17x33: dos pasadas y bandas iguales a la secuencial
Sobel:
  ok    sobel sobre la imagen de prueba de 4x3
  ok    sobel sobre 37x41, contra la implementación aparte
  ok    sobel de una imagen constante da cero en todos los píxeles
Sobel, paralelo contra secuencial, imagen de 512x512:
  ok    sobel idéntico a la secuencial con 1 hilos
  ok    gris,gaussiano,sobel idéntica a la secuencial con 1 hilos
  ok    sobel idéntico a la secuencial con 8 hilos
  ok    gris,gaussiano,sobel idéntica a la secuencial con 8 hilos
Tamaños pequeños y bordes, sobel:
  ok    sobel 1x1: paralelo igual a la secuencial
  ok    sobel 2x3: paralelo igual a la secuencial
  ok    sobel 5x1: paralelo igual a la secuencial
  ok    sobel 1x7: paralelo igual a la secuencial
  ok    sobel 16x16: paralelo igual a la secuencial
  ok    sobel 17x33: paralelo igual a la secuencial
Filtro desconocido:
  ok    la cadena con un filtro inexistente se rechaza
Cadenas que no se pueden aplicar:
  ok    sobel se rechaza antes de aplicar nada
  ok    gris,gris se rechaza antes de aplicar nada
  ok    otsu se rechaza antes de aplicar nada

todas las pruebas pasaron
```

## Ambiente de Python

El entorno local incluye NumPy, requerido por las implementaciones aparte que
calculan las salidas esperadas (`tests/esperado_gaussiano.py` y
`tests/esperado_sobel.py`). Solo hace falta para regenerar esos archivos:
`make pruebas` compara contra los que ya están versionados en `tests/datos/`.

```console
$ python3 -m venv .venv
$ .venv/bin/python -m pip install -r requirements.txt
$ cd motor
$ ../.venv/bin/python tests/esperado_gaussiano.py tests/datos/entrada.ppm /tmp/esperado.ppm
$ ../.venv/bin/python tests/esperado_sobel.py tests/datos/gris.pgm /tmp/esperado.pgm
```

Las salidas esperadas de `tests/datos/` se calcularon con una implementación
aparte de las mismas definiciones, así que una prueba que pasa dice que las
dos coinciden. Un filtro nuevo se prueba igual: su salida paralela tiene que
ser idéntica byte a byte a la secuencial, y `comparar` es quien lo dice.

## Medir

La imagen de medición es de 4096x4096 y no se versiona: la escribe el
generador, que produce siempre el mismo archivo.

```console
$ ./generar /tmp/medicion.ppm 4096 4096
escrito /tmp/medicion.ppm: 4096x4096, 50331648 bytes de píxeles
```

El arnés cronometra solo la llamada al filtro, con la imagen ya en memoria, y
escribe una fila por repetición. El número de hilos sale de
`OMP_NUM_THREADS`; el resumen de la mediana descarta la primera corrida.

```console
$ OMP_NUM_THREADS=1 ./arnes gris /tmp/medicion.ppm 3 --encabezado
filtro,hilos,repeticion,milisegundos
gris,1,1,2.51226
gris,1,2,2.46808
gris,1,3,2.09325
gris con 1 hilo: mediana 2.28067 ms sobre 2 corridas
```

`bench/medir.sh` recorre 1, 2, 4 y 8 hilos para los filtros que se le pasen y
deja la tabla completa en `mediciones/motor.csv` (sobrescribe el archivo, así
que se le pasan todos los filtros juntos). Se le da la imagen de color; `sobel`,
`otsu` y `ecualizar` se miden sobre la salida de `gris`, que calcula antes de
empezar a cronometrar:

```console
$ bash bench/medir.sh /tmp/medicion.ppm gris caja
escrito ../mediciones/motor.csv con 40 filas
```

## Trabajador y despachador

```console
$ cd motor && make && cd ..
$ PUERTO=8081 python3 trabajador/trabajador.py &
$ PUERTO=8082 python3 trabajador/trabajador.py &
$ python3 despachador/despachador.py --trabajadores http://localhost:8081,http://localhost:8082 \
      --filtros gris,gaussiano --entrada lote/ --salida resultado/
$ bash despachador/pruebas.sh
```

La API del trabajador y el reparto están en `docs/01-arquitectura.md`. No
hacen falta paquetes de Python fuera de la biblioteca estándar.

## Convenciones que no cambian

De ellas depende que la comparación byte a byte signifique algo.

1. **Bordes por repetición del píxel más cercano.** La vecindad que se sale de
   la imagen usa el píxel del borde: las coordenadas se recortan a
   `[0, ancho-1]` y `[0, alto-1]`. No se rellena con ceros ni se recorta la
   salida.
2. **Acumulación en `double` y redondeo al final**, al entero más cercano con
   los empates hacia arriba, saturando a `[0, 255]`. Están en `redondear()`.
3. **Las reducciones son sobre enteros.** El histograma acumula contadores
   enteros y Otsu trabaja sobre el histograma, así que el resultado no depende
   del orden en que los hilos acumulen.
