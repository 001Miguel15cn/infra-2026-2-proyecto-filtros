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
