// Mide cuánto tarda un filtro sobre una imagen ya cargada en memoria. El
// cronómetro rodea solo la llamada al filtro: la lectura y la escritura del
// archivo quedan fuera.
//
// El primer argumento puede ser un filtro o una cadena separada por comas
// (por ejemplo gris,gaussiano,sobel,otsu): en ese caso se cronometra la cadena
// completa. Con --gris la imagen se pasa a grises antes de arrancar el
// cronómetro, para medir un filtro de un canal (gaussiano, sobel) sobre la
// misma entrada que recibe dentro de la cadena.
//
// El número de hilos es el que fije OMP_NUM_THREADS en el entorno, y viaja
// en la salida para que cada fila diga con cuántos se tomó.
#include "filtros.hpp"
#include "imagen.hpp"

#include <omp.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

Imagen aplicar(const std::string& filtro, const Imagen& entrada, bool secuencial) {
    if (filtro == "gris") return secuencial ? gris_secuencial(entrada) : gris_paralelo(entrada);
    if (filtro == "caja") return secuencial ? caja_secuencial(entrada) : caja_paralelo(entrada);
    if (filtro == "gaussiano") {
        return secuencial ? gaussiano_secuencial(entrada) : gaussiano_paralelo(entrada);
    }
    if (filtro == "gaussiano_directo") return gaussiano_directo(entrada);
    if (filtro == "gaussiano_bandas") {
        return secuencial ? gaussiano_secuencial(entrada) : gaussiano_paralelo_bandas(entrada);
    }
    if (filtro == "sobel") return secuencial ? sobel_secuencial(entrada) : sobel_paralelo(entrada);
    if (filtro == "otsu") return secuencial ? otsu_secuencial(entrada) : otsu_paralelo(entrada);
    throw std::runtime_error("filtro desconocido: " + filtro);
}

Imagen aplicar_cadena(const std::string& cadena, const Imagen& entrada, bool secuencial) {
    Imagen actual = entrada;
    std::istringstream flujo(cadena);
    std::string filtro;
    while (std::getline(flujo, filtro, ',')) {
        if (!filtro.empty()) actual = aplicar(filtro, actual, secuencial);
    }
    return actual;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "uso: arnes <filtro|cadena> <imagen> [repeticiones] [--secuencial]"
                     " [--encabezado] [--gris]\n";
        return 2;
    }
    const std::string filtro = argv[1];
    const std::string ruta = argv[2];
    int repeticiones = 5;
    bool secuencial = false;
    bool encabezado = false;
    bool a_gris = false;
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--secuencial") {
            secuencial = true;
        } else if (arg == "--encabezado") {
            encabezado = true;
        } else if (arg == "--gris") {
            a_gris = true;
        } else {
            repeticiones = std::atoi(arg.c_str());
        }
    }
    if (repeticiones < 1) {
        std::cerr << "arnes: repeticiones tiene que ser al menos 1\n";
        return 2;
    }

    try {
        Imagen entrada = leer(ruta);
        if (a_gris) entrada = gris_secuencial(entrada);
        const int hilos = secuencial ? 1 : omp_get_max_threads();
        if (encabezado) std::cout << "filtro,hilos,repeticion,milisegundos\n";

        // En una cadena las comas se cambian por '+' para no romper el CSV.
        std::string etiqueta = filtro;
        std::replace(etiqueta.begin(), etiqueta.end(), ',', '+');

        std::vector<double> medidas;
        for (int r = 1; r <= repeticiones; ++r) {
            const double inicio = omp_get_wtime();
            const Imagen salida = aplicar_cadena(filtro, entrada, secuencial);
            const double ms = (omp_get_wtime() - inicio) * 1000.0;
            // La imagen de salida se usa aquí para que el compilador no
            // pueda eliminar la llamada al filtro.
            if (salida.datos.empty()) throw std::runtime_error("el filtro devolvió una imagen vacía");
            medidas.push_back(ms);
            std::cout << etiqueta << "," << hilos << "," << r << "," << ms << "\n";
        }

        // La primera corrida paga la primera lectura de la imagen desde
        // memoria principal, así que el resumen la deja fuera.
        if (medidas.size() > 1) {
            std::vector<double> utiles(medidas.begin() + 1, medidas.end());
            std::sort(utiles.begin(), utiles.end());
            const std::size_t mitad = utiles.size() / 2;
            const double mediana = utiles.size() % 2 == 1
                                       ? utiles[mitad]
                                       : (utiles[mitad - 1] + utiles[mitad]) / 2.0;
            std::cerr << etiqueta << " con " << hilos << (hilos == 1 ? " hilo" : " hilos")
                      << ": mediana " << mediana << " ms sobre " << utiles.size()
                      << " corridas\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "arnes: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
