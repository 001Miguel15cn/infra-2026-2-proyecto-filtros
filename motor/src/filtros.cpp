#include "filtros.hpp"

#include <stdexcept>
#include <cstdint>
#include <cmath>

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

Imagen gaussiano_secuencial(const Imagen&) {
    throw std::runtime_error("gaussiano: sin implementar");
}

Imagen gaussiano_paralelo(const Imagen&) {
    throw std::runtime_error("gaussiano: sin implementar");
}
 
//funciones auxiliares para sobel y histograma
//esta funcion se encuentra en el namespace 
//anonimo para que no sea visible fuera de este archivo
namespace {
 
void exigir_un_canal(const Imagen& e, const char* nombre) {
    if (e.canales != 1) {
        throw std::runtime_error(std::string(nombre) + " espera una imagen de un canal");
    }
}
 
 
namespace {
 
// Núcleo binomial [1 4 6 4 1]/16. Todos los pesos son múltiplos exactos de
// 1/16 y los datos son enteros de 0 a 255, así que las sumas en double son
// exactas: separable y directo dan el mismo resultado, y el orden fijo de
// las sumas hace idéntica la salida con cualquier número de hilos.
constexpr int KB[5] = {1, 4, 6, 4, 1};
//funcion auxiliar para calcular el valor de un pixel en sobel
// Sobel en un pixel, con las coordenadas
// recortadas al rectángulo de la
// imagen. La salida es un entero de 0 a 255.
Imagen vacia_como(const Imagen& e, int canales) {
    Imagen s;
    s.ancho = e.ancho;
    s.alto = e.alto;
    s.canales = canales;
    s.datos.resize(static_cast<std::size_t>(e.ancho) * e.alto * canales);
    return s;
}


Imagen sobel_secuencial(const Imagen& e) {
    exigir_un_canal(e, "sobel");
    Imagen s = vacia_como(e, 1);
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) s.poner(x, y, 0, sobel_pixel(e, x, y));
    }
    return s;
}
 
// Redondea un double a un entero de 0 a 255. La entrada puede ser
// negativa o mayor que 255, pero la salida siempre está en el rango.
// La función std::round devuelve un double, así que hay que convertirlo a
// entero. La conversión a entero de un double fuera del rango de int es
// indefinida, así que hay que recortar antes de convertir.
// Un píxel de Sobel: gx y gy salen de la misma lectura de la vecindad.
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


Imagen sobel_paralelo(const Imagen& e) {
    exigir_un_canal(e, "sobel");
    Imagen s = vacia_como(e, 1);
    #pragma omp parallel for schedule(static)
    for (int y = 0; y < e.alto; ++y) {
        for (int x = 0; x < e.ancho; ++x) s.poner(x, y, 0, sobel_pixel(e, x, y));
    }
    return s;
}


Imagen otsu_secuencial(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}

Imagen otsu_paralelo(const Imagen&) {
    throw std::runtime_error("otsu: sin implementar");
}
