#include "filtros.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

Imagen gris_secuencial(const Imagen& e) {
    if (e.canales != 3) throw std::runtime_error("gris espera una imagen de tres canales");
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = 1;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto);
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            const double v = 0.299 * e.en(x, y, 0) + 0.587 * e.en(x, y, 1) + 0.114 * e.en(x, y, 2);
            s.poner(x, y, 0, redondear(v));
        }
    }
    return s;
}

Imagen gris_paralelo(const Imagen& e) {
    if (e.canales != 3) throw std::runtime_error("gris espera una imagen de tres canales");
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = 1;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto);
    // Cada fila es independiente de las demás: no hay nada que compartir
    // entre hilos ni dependencias entre iteraciones.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            const double v = 0.299 * e.en(x, y, 0) + 0.587 * e.en(x, y, 1) + 0.114 * e.en(x, y, 2);
            s.poner(x, y, 0, redondear(v));
        }
    }
    return s;
}

namespace {

// El promedio de la vecindad de 3x3 de un canal, con las coordenadas
// recortadas al rectángulo de la imagen.
double promedio_vecindad(const Imagen& e, int x, int y, int c) {
    double suma = 0.0;
    for (int j = -1; j <= 1; ++j) {
        const int fila = recortar(y + j, e.alto);
        for (int i = -1; i <= 1; ++i) {
            suma += e.en(recortar(x + i, e.ancho), fila, c);
        }
    }
    return suma / 9.0;
}

}  // namespace

Imagen caja_secuencial(const Imagen& e) {
    Imagen s = e;
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                s.poner(x, y, c, redondear(promedio_vecindad(e, x, y, c)));
            }
        }
    }
    return s;
}

Imagen caja_paralelo(const Imagen& e) {
    Imagen s = e;
    // El ciclo externo recorre filas y el interno columnas, que es el orden
    // en que la imagen está en memoria. Repartir por filas deja a cada hilo
    // con una banda contigua.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                s.poner(x, y, c, redondear(promedio_vecindad(e, x, y, c)));
            }
        }
    }
    return s;
}

namespace {

// Núcleo binomial [1 4 6 4 1]/16. Los pesos son enteros y se divide entre 16
// al final de cada pasada, así que las sumas en double son exactas y el
// resultado no depende de cómo se reparta el trabajo.
const int NUCLEO[5] = {1, 4, 6, 4, 1};

// Pasada horizontal de la fila y: deja en destino ancho*canales valores sin
// redondear.
void horizontal_fila(const Imagen& e, int y, double* destino) {
    for (int x = 0; x < e.ancho; ++x) {
        for (int c = 0; c < e.canales; ++c) {
            double suma = 0.0;
            for (int i = -2; i <= 2; ++i) {
                suma += NUCLEO[i + 2] * e.en(recortar(x + i, e.ancho), y, c);
            }
            destino[x * e.canales + c] = suma / 16.0;
        }
    }
}

// Pasada vertical de la fila y de salida, a partir de cinco filas ya
// filtradas en horizontal (de y-2 a y+2, con los bordes ya recortados).
void vertical_fila(Imagen& s, int y, const double* const f[5]) {
    const std::size_t n = static_cast<std::size_t>(s.ancho) * s.canales;
    unsigned char* destino = s.datos.data() + static_cast<std::size_t>(y) * n;
    for (std::size_t k = 0; k < n; ++k) {
        const double v = NUCLEO[0] * f[0][k] + NUCLEO[1] * f[1][k] + NUCLEO[2] * f[2][k] +
                         NUCLEO[3] * f[3][k] + NUCLEO[4] * f[4][k];
        destino[k] = redondear(v / 16.0);
    }
}

Imagen vacia_como(const Imagen& e) {
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = e.canales;
    s.datos.resize(e.datos.size());
    return s;
}

// Las cinco filas de la intermedia que necesita la fila y de salida.
void filas_vecinas(const double* base, std::size_t n, int y, int alto, const double* f[5]) {
    for (int j = -2; j <= 2; ++j) f[j + 2] = base + static_cast<std::size_t>(recortar(y + j, alto)) * n;
}

}  // namespace

Imagen gaussiano_secuencial(const Imagen& e) {
    Imagen s = vacia_como(e);
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    std::vector<double> inter(n * e.alto);
    for (int y = 0; y < e.alto; ++y) horizontal_fila(e, y, inter.data() + y * n);
    for (int y = 0; y < e.alto; ++y) {
        const double* f[5];
        filas_vecinas(inter.data(), n, y, e.alto, f);
        vertical_fila(s, y, f);
    }
    return s;
}

// Forma directa del enunciado: 25 multiplicaciones por píxel y por canal. Los
// pesos NUCLEO[j]*NUCLEO[i] son enteros y se divide entre 256 al final, así que
// la suma en double es exacta y da el mismo resultado que la separable.
Imagen gaussiano_directo(const Imagen& e) {
    Imagen s = vacia_como(e);
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) {
            for (int c = 0; c < e.canales; ++c) {
                double suma = 0.0;
                for (int j = -2; j <= 2; ++j) {
                    const int fila = recortar(y + j, e.alto);
                    for (int i = -2; i <= 2; ++i) {
                        suma += NUCLEO[j + 2] * NUCLEO[i + 2] * e.en(recortar(x + i, e.ancho), fila, c);
                    }
                }
                s.poner(x, y, c, redondear(suma / 256.0));
            }
        }
    }
    return s;
}

// Versión paralela directa: dos pasadas sobre toda la imagen, con la imagen
// intermedia completa en memoria (en double, 8 bytes por componente).
Imagen gaussiano_paralelo(const Imagen& e) {
    Imagen s = vacia_como(e);
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    std::vector<double> inter(n * e.alto);
    // Cada fila de la intermedia sale solo de una fila de la entrada.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) horizontal_fila(e, y, inter.data() + y * n);
    // Cada fila de salida lee cinco de la intermedia y escribe solo la suya.
    // La barrera implícita del for anterior garantiza que ya está completa.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        const double* f[5];
        filas_vecinas(inter.data(), n, y, e.alto, f);
        vertical_fila(s, y, f);
    }
    return s;
}

// Misma cuenta con mejor localidad: la imagen se parte en bandas de filas y
// cada hilo hace las dos pasadas sobre su banda con un búfer pequeño que
// cabe en caché, en vez de escribir y volver a leer la intermedia completa.
// Las cuatro filas de borde de cada banda se filtran dos veces.
Imagen gaussiano_paralelo_bandas(const Imagen& e) {
    Imagen s = vacia_como(e);
    const std::size_t n = static_cast<std::size_t>(e.ancho) * e.canales;
    const int bandas = (e.alto + BANDA_FILAS - 1) / BANDA_FILAS;
    #pragma omp parallel
    {
        std::vector<double> buf(n * (BANDA_FILAS + 4));
        #pragma omp for schedule(static)
        for (int b = 0; b < bandas; ++b) {
            const int y0 = b * BANDA_FILAS;
            const int y1 = std::min(y0 + BANDA_FILAS, e.alto);
            // La fila r del búfer es la fila recortada(y0-2+r) de la
            // intermedia, que es justo la que pide la pasada vertical.
            const int filas = y1 - y0 + 4;
            for (int r = 0; r < filas; ++r) {
                horizontal_fila(e, recortar(y0 - 2 + r, e.alto), buf.data() + r * n);
            }
            for (int y = y0; y < y1; ++y) {
                const double* f[5];
                for (int j = 0; j < 5; ++j) f[j] = buf.data() + static_cast<std::size_t>(y - y0 + j) * n;
                vertical_fila(s, y, f);
            }
        }
    }
    return s;
}

Imagen sobel_secuencial(const Imagen&) {
    throw std::runtime_error("sobel: sin implementar");
}

Imagen sobel_paralelo(const Imagen&) {
    throw std::runtime_error("sobel: sin implementar");
}

Imagen otsu_secuencial(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}

Imagen otsu_paralelo(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}
