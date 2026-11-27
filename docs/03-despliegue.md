# Despliegue

Esta página cubre el despliegue local con Docker Compose. Las secciones de
Kubernetes, GitHub Actions y SonarQube están al final y se completan cuando
esas partes estén hechas.

## Imágenes

Cada componente tiene su Dockerfile. Las dos imágenes se construyen con la
**raíz del repositorio como contexto** (`context: ../..` en Compose): la imagen
del trabajador compila el motor, que vive en `motor/`, fuera de
`trabajador/`. Un `.dockerignore` en la raíz deja fuera `.git`, las imágenes de
medición, los binarios y las pruebas, para que el contexto sea pequeño.

### Trabajador (`trabajador/Dockerfile`)

Construcción en dos etapas. La primera compila el motor con
`-O2 -Wall -Wextra -fopenmp` (lo fija el `Makefile`); la segunda es una imagen
de Python sin compilador, con solo `libgomp1` (el entorno de ejecución de
OpenMP) y el binario copiado. Las dos etapas usan Debian *bookworm*, para que
el binario encuentre la misma libc. Corre con un usuario sin privilegios.
`HEALTHCHECK` consulta `/readyz`, y Compose espera a ese estado antes de
arrancar el despachador.

```dockerfile
# Trabajador: el motor en C++ y la capa HTTP en Python, en un solo contenedor.
# Se construye con la raíz del repositorio como contexto (la imagen necesita
# motor/ y trabajador/):
#   docker build -f trabajador/Dockerfile -t trabajador .

# --- Etapa 1: compilar el motor con -Wall -Wextra (lo fija el Makefile) -----
FROM debian:bookworm-slim AS compilar
RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /motor
COPY motor/Makefile ./
COPY motor/src ./src
RUN make motor

# --- Etapa 2: imagen final, sin compilador -----------------------------------
# Misma base Debian que la etapa 1, para que el binario encuentre su libc.
FROM python:3.12-slim-bookworm
# libgomp1 es el entorno de ejecución de OpenMP que el motor necesita.
RUN apt-get update \
    && apt-get install -y --no-install-recommends libgomp1 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --no-create-home trabajador
WORKDIR /app
COPY --from=compilar /motor/motor /app/motor
COPY trabajador/trabajador.py /app/trabajador.py

# OMP_NUM_THREADS se sobrescribe desde Compose o el ConfigMap; X-Hilos lo
# devuelve en cada respuesta.
ENV MOTOR=/app/motor \
    PUERTO=8000 \
    OMP_NUM_THREADS=2 \
    PYTHONUNBUFFERED=1
USER trabajador
EXPOSE 8000

# Readiness: el binario del motor está presente y responde. Compose espera a
# este estado antes de arrancar el despachador.
HEALTHCHECK --interval=5s --timeout=4s --start-period=5s --retries=5 \
    CMD ["python", "-c", "import os, urllib.request; urllib.request.urlopen('http://127.0.0.1:' + os.environ.get('PUERTO', '8000') + '/readyz', timeout=3)"]

CMD ["python", "/app/trabajador.py"]
```

### Despachador (`despachador/Dockerfile`)

Imagen de Python con un solo archivo: el despachador usa solo la biblioteca
estándar, así que no hay `pip install`. `/datos` pertenece al usuario sin
privilegios, y un volumen nuevo hereda ese dueño.

```dockerfile
# Despachador: programa de Python (solo biblioteca estándar) que lee el lote
# del volumen, lo reparte entre los trabajadores y reporta el throughput.
# Contexto de construcción: la raíz del repositorio.
#   docker build -f despachador/Dockerfile -t despachador .
FROM python:3.12-slim-bookworm
# /datos pertenece al usuario sin privilegios: un volumen nuevo hereda ese dueño.
RUN useradd --system --uid 10001 --no-create-home despachador \
    && mkdir /datos \
    && chown despachador /datos
WORKDIR /app
COPY despachador/despachador.py /app/despachador.py

ENV URL_TRABAJADOR=http://trabajador:8000 \
    CONCURRENCIA=8 \
    DATOS=/datos \
    PYTHONUNBUFFERED=1
USER despachador
CMD ["python", "/app/despachador.py"]
```

## Docker Compose

`deploy/local/docker-compose.yml` levanta el servicio completo. Con una sola
orden, desde `deploy/local/`:

```console
$ docker compose up --build --scale trabajador=1
$ docker compose up --build --scale trabajador=4
```

El despachador genera el lote la primera vez, espera a que los trabajadores
estén listos, procesa las 64 imágenes de 1024x1024 e imprime el *throughput*.
`HILOS`, `CONCURRENCIA` y `FILTROS` se pueden fijar en el entorno (por defecto
2, 8 y `gris,gaussiano,sobel,otsu`).

```yaml
# Servicio completo en local, con una sola orden desde esta carpeta:
#
#   docker compose up --build --scale trabajador=4
#
# El despachador genera el lote de 64 imágenes de 1024x1024 la primera vez
# (entrada/ vacía), espera a que los trabajadores estén listos, procesa el lote,
# imprime el throughput y agrega una fila a servicio.csv dentro del volumen.
# Las variables de ejemplo (HILOS, CONCURRENCIA, FILTROS) se pueden fijar en el
# entorno o en un archivo .env junto a este archivo.
name: filtros

services:
  trabajador:
    build:
      # El contexto es la raíz del repositorio: la imagen compila el motor, que
      # vive en ../../motor, fuera de la carpeta del trabajador.
      context: ../..
      dockerfile: trabajador/Dockerfile
    environment:
      # Hilos de OpenMP por réplica. Con N réplicas, N x este valor es la
      # carga total de hilos sobre la máquina.
      OMP_NUM_THREADS: "${HILOS:-2}"
    # Sin "ports" ni "container_name": con --scale hay varias réplicas y cada
    # una se alcanza por el nombre del servicio dentro de la red de Compose.
    # La sonda de readiness (/readyz) viene definida en el Dockerfile.
    # Los trabajadores NO montan ningún volumen: no comparten estado.

  despachador:
    build:
      context: ../..
      dockerfile: despachador/Dockerfile
    environment:
      # Siempre el mismo nombre: la resolución de nombres de Compose reparte
      # entre las réplicas. Subir el número de trabajadores no cambia nada aquí.
      URL_TRABAJADOR: "http://trabajador:8000"
      CONCURRENCIA: "${CONCURRENCIA:-8}"
      FILTROS: "${FILTROS:-gris,gaussiano,sobel,otsu}"
      GENERAR: "64"
      TAM: "1024x1024"
      ENTORNO: "local"
      # Solo es la etiqueta de la columna "trabajadores" del reporte; el
      # despachador no la usa para repartir. medir.sh la fija.
      TRABAJADORES_REPORTE: "${TRABAJADORES:-1}"
    volumes:
      # El almacenamiento (entrada/ y salida/) lo monta solo el despachador.
      - lote:/datos
    depends_on:
      trabajador:
        condition: service_healthy

volumes:
  lote:
```

Decisiones:

- **Sin `ports` ni `container_name` en el trabajador.** Con `--scale` hay varias
  réplicas, y un puerto publicado o un nombre fijo harían chocar a la segunda.
  Se alcanzan por el nombre del servicio dentro de la red de Compose.
- **El volumen `lote` solo lo monta el despachador.** Los trabajadores no
  comparten estado (ver `01-arquitectura.md`).
- **`depends_on` con `service_healthy`:** un `depends_on` simple solo ordena el
  arranque de los contenedores, no espera a que el motor responda; con la sonda
  de *readiness* el despachador arranca cuando las réplicas están listas
  (y además espera `/readyz` por su cuenta).
- **`OMP_NUM_THREADS` por réplica.** Con 4 réplicas de 2 hilos la carga total
  son 8 hilos sobre la máquina.

Para medir el *throughput* con 1 y 4 trabajadores y dejar la tabla en
`mediciones/servicio.csv`:

```console
$ bash deploy/local/medir.sh
```

## Throughput local

Lote de 64 imágenes de 1024x1024, cadena `gris,gaussiano,sobel,otsu`,
concurrencia 8, 2 hilos por trabajador. El reporte sale de
`mediciones/servicio.csv`.

| Entorno | Trabajadores | Hilos por trabajador | Segundos | Imágenes/s |
|---|---|---|---|---|
| local | 1 | 2 | _PENDIENTE_ | _PENDIENTE_ |
| local | 4 | 2 | _PENDIENTE_ | _PENDIENTE_ |

> **PENDIENTE:** correr `bash deploy/local/medir.sh` en la máquina declarada en
> `02-motor.md`, con Docker, y copiar aquí las medianas. Hay que explicar la
> diferencia entre 1 y 4 trabajadores: con 4 trabajadores de 2 hilos en una
> máquina de 8 hilos el reparto no crea capacidad nueva, y lo que se observa es
> cómo se comporta el servicio cuando el cómputo ya ocupa la máquina
> (cambios de contexto, ancho de banda de memoria compartido, la
> serialización de un trabajador a la vez por réplica). Estimar cuánto del
> tiempo de cada petición no es cómputo (transferencia de 3 MB de ida y de
> vuelta, escritura a disco temporal) con `X-Tiempo-Ms` contra el tiempo total.

## Despliegue en GKE

> Pendiente (otra parte del proyecto): manifiestos en `deploy/nube/`, `requests`
> y `limits`, evidencia de `kubectl`, *throughput* con 1 y 3 réplicas, gasto del
> crédito y evidencia del apagado.

## Flujo de GitHub Actions

> Pendiente: flujo en `.github/workflows/` (compilar y probar el motor, construir
> y publicar con la etiqueta `${{ github.sha }}`, autenticación y `rollout`).

## SonarQube

> Pendiente: `sonar-project.properties`, evidencia del análisis y al menos dos
> hallazgos corregidos con el antes y el después.
