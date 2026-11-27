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
