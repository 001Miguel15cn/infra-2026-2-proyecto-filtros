#include "filtros.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <cmath>
#include <omp.h>
#include <string>

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

namespace {

void exigir_un_canal(const Imagen& e, const char* nombre) {
    if (e.canales != 1) {
        throw std::runtime_error(std::string(nombre) + " espera una imagen de un canal");
    }
}

Imagen vacia_como(const Imagen& e, int canales) {
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = canales;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto * canales);
    return s;
}

unsigned char sobel_pixel(const Imagen& e, int x, int y) {
    int v[3][3];
    for (int j = -1; j <= 1; ++j) {
        const int fila = recortar(y + j, e.alto);
        for (int i = -1; i <= 1; ++i) {
            v[j + 1][i + 1] = e.en(recortar(x + i, e.ancho), fila, 0);
        }
    }
    const int gx = -v[0][0] + v[0][2] - 2 * v[1][0] + 2 * v[1][2] - v[2][0] + v[2][2];
    const int gy = -v[0][0] - 2 * v[0][1] - v[0][2] + v[2][0] + 2 * v[2][1] + v[2][2];
    return redondear(std::sqrt(static_cast<double>(gx * gx + gy * gy)));
}
}  // namespace

Imagen sobel_secuencial(const Imagen& e) {
    exigir_un_canal(e, "sobel");
    Imagen s = vacia_como(e, 1);
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) s.poner(x, y, 0, sobel_pixel(e, x, y));
    }
    return s;
}

Imagen sobel_paralelo(const Imagen& e) {
    exigir_un_canal(e, "sobel");
    Imagen s = vacia_como(e, 1);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) s.poner(x, y, 0, sobel_pixel(e, x, y));
    }
    return s;
}


namespace {

const int NIVELES = 256;

// Histograma de un hilo. 256 contadores de 8 bytes son 2048 bytes (32 líneas
// de caché de 64) y la estructura está alineada a 64, así que dos hilos nunca
// escriben en la misma línea de caché aunque los histogramas queden contiguos
// en el vector (sin false sharing).
struct alignas(64) Contadores {
    std::uint64_t h[NIVELES];
};

// Barrido de los 255 umbrales. Es secuencial en las dos versiones: el mismo
// código sobre el mismo histograma entero, así que el umbral no puede diferir.
// Maximiza w0 * w1 * (mu0 - mu1)^2 con pesos en píxeles y no en fracciones: eso
// difiere de la varianza del enunciado en el factor constante 1/N^2, que no
// cambia el argmax. En un empate gana el primer umbral (comparación estricta).
// Si la imagen tiene un solo nivel no hay división posible y devuelve 0.
int umbral_otsu(const std::uint64_t h[NIVELES], std::uint64_t total) {
    std::uint64_t suma_total = 0;
    for (int v = 0; v < NIVELES; ++v) suma_total += static_cast<std::uint64_t>(v) * h[v];

    std::uint64_t peso0 = 0, suma0 = 0;
    double mejor = -1.0;
    int umbral = 0;
    for (int t = 0; t < NIVELES - 1; ++t) {
        peso0 += h[t];
        suma0 += static_cast<std::uint64_t>(t) * h[t];
        if (peso0 == 0) continue;
        const std::uint64_t peso1 = total - peso0;
        if (peso1 == 0) break;
        const double mu0 = static_cast<double>(suma0) / static_cast<double>(peso0);
        const double mu1 = static_cast<double>(suma_total - suma0) / static_cast<double>(peso1);
        const double d = mu0 - mu1;
        const double varianza = static_cast<double>(peso0) * static_cast<double>(peso1) * d * d;
        if (varianza > mejor) {
            mejor = varianza;
            umbral = t;
        }
    }
    return umbral;
}

// O(x,y) = 0 si I(x,y) <= umbral, 255 en otro caso.
Imagen binarizar_secuencial(const Imagen& e, int umbral) {
    Imagen s = vacia_como(e);
    for (std::size_t k = 0; k < e.datos.size(); ++k) s.datos[k] = e.datos[k] <= umbral ? 0 : 255;
    return s;
}

Imagen binarizar_paralelo(const Imagen& e, int umbral) {
    Imagen s = vacia_como(e);
    const std::size_t n = static_cast<std::size_t>(e.ancho);
    // Map por filas: cada hilo escribe solo las suyas.
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        const std::size_t base = static_cast<std::size_t>(y) * n;
        for (std::size_t k = 0; k < n; ++k) s.datos[base + k] = e.datos[base + k] <= umbral ? 0 : 255;
    }
    return s;
}

}  // namespace

Imagen otsu_secuencial(const Imagen& e) {
    exigir_un_canal(e, "otsu");
    std::uint64_t h[NIVELES] = {};
    for (unsigned char v : e.datos) ++h[v];
    const int t = umbral_otsu(h, e.datos.size());
    return binarizar_secuencial(e, t);
}

// Reducción con un histograma privado por hilo y suma al final. Los contadores
// son enteros: la suma no depende del orden de los hilos y la salida es
// idéntica a la secuencial.
Imagen otsu_paralelo(const Imagen& e) {
    exigir_un_canal(e, "otsu");
    const int hilos = omp_get_max_threads();
    std::vector<Contadores> privados(static_cast<std::size_t>(hilos));  // en cero
    #pragma omp parallel num_threads(hilos)
    {
        std::uint64_t* mio = privados[static_cast<std::size_t>(omp_get_thread_num())].h;
        #pragma omp for schedule(static)
        for (int y = 0; y < e.alto; ++y) {
            const unsigned char* fila = e.datos.data() + static_cast<std::size_t>(y) * e.ancho;
            for (int x = 0; x < e.ancho; ++x) ++mio[fila[x]];
        }
    }
    std::uint64_t h[NIVELES] = {};
    for (const Contadores& c : privados) {
        for (int v = 0; v < NIVELES; ++v) h[v] += c.h[v];
    }
    const int t = umbral_otsu(h, e.datos.size());
    return binarizar_paralelo(e, t);
}

// Versión ingenua, solo para la comparación que pide el enunciado: un único
// histograma compartido protegido con atomic. Da la misma salida, pero los
// hilos compiten por los mismos 256 contadores.
Imagen otsu_atomico(const Imagen& e) {
    exigir_un_canal(e, "otsu");
    std::uint64_t h[NIVELES] = {};
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        const unsigned char* fila = e.datos.data() + static_cast<std::size_t>(y) * e.ancho;
        for (int x = 0; x < e.ancho; ++x) {
            #pragma omp atomic
            ++h[fila[x]];
        }
    }
    const int t = umbral_otsu(h, e.datos.size());
    return binarizar_paralelo(e, t);
}
