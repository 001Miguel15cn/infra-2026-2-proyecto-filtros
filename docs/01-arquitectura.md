# Arquitectura del servicio

```
despachador  --HTTP-->  trabajador 1  -->  motor (OpenMP)
(Python)     --HTTP-->  trabajador 2  -->  motor (OpenMP)
             --HTTP-->  trabajador N  -->  motor (OpenMP)
```

## Trabajador (`trabajador/trabajador.py`)

Servidor HTTP en Python (solo biblioteca estándar) que llama al binario del
motor con `subprocess`. Cada petición escribe la imagen en una carpeta
temporal, corre el motor y devuelve el resultado.

| Ruta | Descripción |
|---|---|
| `GET /salud` | 200 si el trabajador está vivo |
| `POST /procesar?filtros=gris,gaussiano` | cuerpo: PPM o PGM en crudo; respuesta: la imagen resultante (P5 o P6) |

| Código | Cuándo |
|---|---|
| 200 | listo; trae `X-Milisegundos` con lo que tardó el motor |
| 400 | cadena inválida, filtro desconocido o imagen ilegible; el cuerpo trae el motivo del motor |
| 411 / 413 | falta `Content-Length` / imagen mayor que `MAX_BYTES` |
| 501 | filtro sin implementar |
| 504 | el motor pasó de `TIEMPO_MAX` |
| 500 | el motor terminó de forma inesperada |

Atiende una imagen a la vez (`MAX_CONCURRENTES=1`), porque OpenMP ya usa todos
los núcleos. El nombre del filtro se valida con `^[a-z_]+(,[a-z_]+)*$` antes
de pasarlo al motor, y el motor se llama sin shell.

Variables: `PUERTO` (8080), `MOTOR`, `HILOS` (fija `OMP_NUM_THREADS`),
`TIEMPO_MAX` (120 s), `MAX_BYTES`, `MAX_CONCURRENTES`.

## Despachador (`despachador/despachador.py`)

Lee las imágenes de una carpeta y las reparte con una cola común: hay un hilo
por trabajador y cada uno toma la siguiente imagen apenas termina la anterior,
así el trabajador más rápido atiende más.

- Antes de empezar consulta `/salud` y omite a los que no responden.
- Si un trabajador deja de contestar a mitad del lote, su imagen vuelve a la
  cola (hasta `--reintentos` veces) y a ese trabajador no se le envía más.
- Un 400 o un 501 no se reintenta: otro trabajador daría la misma respuesta.
- Escribe las salidas en la carpeta de resultados, imprime una tabla y, con
  `--informe`, deja un CSV.

```console
$ python3 despachador/despachador.py --trabajadores http://localhost:8081,http://localhost:8082 \
      --filtros gris,gaussiano --entrada lote/ --salida resultado/
```

`bash despachador/pruebas.sh` levanta dos trabajadores y revisa el reparto, la
recuperación ante una caída y los errores; cada salida se compara byte a byte
con la del motor corrido directamente.
