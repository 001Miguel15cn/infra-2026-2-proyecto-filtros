# Conclusiones

> Borrador de la parte del gaussiano y del servicio local. Las cifras del motor
> salen de `mediciones/motor.csv` (i5-12450HX, ver `02-motor.md`); las del
> servicio, entre corchetes, se llenan cuando se mida `mediciones/servicio.csv`.

## Lectura de las cifras

- **Algoritmo antes que hilos.** Con un solo hilo, la forma separable hace 10
  multiplicaciones por píxel contra 25 de la directa; la mejora medida es
  2.59x (450.5 ms contra 174.0 ms), casi la razón teórica de 2.5, porque con un
  hilo el filtro está limitado por el cómputo.
- **El gaussiano de dos pasadas escala poco.** Su speedup se aplana a partir de
  4 hilos ($S(4)=2.66$, $S(8)=2.76$, $E(8)=0.35$). La intermedia en `double`
  ocupa unos 134 MB, no cabe en caché y se escribe y relee entera; es
  coherente con un límite de memoria compartida, pendiente de confirmar con el
  perfilado.
- **La optimización por bandas** sube el speedup con 8 hilos de 2.76 a 3.73 y
  baja el tiempo de 63.0 a 32.0 ms (1.97x); ya con 1 hilo baja de 174.0 a
  119.5 ms (1.46x), porque la intermedia casi no sale de caché. La evidencia de
  `perf stat` (`mediciones/perfilado.txt`) es [fallos de caché antes/después].
  Con 8 hilos la eficiencia sigue en 0.47: no se midió qué la limita.
- **Servicio local.** Con 1 trabajador el *throughput* fue [..] img/s y con 4
  [..] img/s. En una máquina de 8 hilos, 4 trabajadores de 2 hilos no crean
  capacidad nueva: [explicar con las cifras].

## Qué se intentó y no funcionó

- [Completar con lo que el equipo haya probado, por ejemplo tamaños de banda
  que no mejoraron o la concurrencia del despachador que no subió el
  *throughput*.]

## Límites encontrados

- El trabajador atiende una imagen a la vez, así que `CONCURRENCIA` mayor que
  el número de trabajadores solo encola peticiones.
- Las imágenes viajan completas por HTTP: con imágenes pequeñas el costo de
  transferencia domina sobre el cómputo.
- Las mediciones salen de una sola máquina, con ruido de otros procesos.

## Qué se haría distinto con más tiempo

- Medir con más repeticiones y reportar dispersión, no solo la mediana.
- Barrer el tamaño de banda en la máquina declarada.
