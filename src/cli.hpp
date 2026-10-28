// Interfaz de consola: menú interactivo y opciones de línea de comandos.
#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "trace.hpp"

namespace emu {

enum class Engine { Educational, Real, Both };

struct Settings {
    RunOptions run;
    TraceOptions trace;
};

// Ejecuta un fragmento de assembly con el motor elegido. Los valores iniciales
// del archivo ("; init:") se aplican primero y después los de 'initText'.
// Devuelve true si la ejecución terminó correctamente (y, con Engine::Both,
// si ambos motores coinciden).
bool runAssembly(const std::string& source, const std::string& initText, Engine engine,
                 const Settings& settings, std::ostream& out);

// Ejecuta bytes de código máquina x86-64 en Unicorn.
bool runBytes(const std::vector<uint8_t>& code, const std::string& initText,
              const Settings& settings, std::ostream& out);

// Lee líneas de assembly hasta una línea "FIN" o el final de la entrada.
std::string readAsmFromStream(std::istream& in, std::ostream& out, bool prompt);

void interactiveMenu(std::istream& in, std::ostream& out, Settings& settings);

// Punto de entrada: sin argumentos abre el menú. Devuelve el código de salida.
int runCommandLine(const std::vector<std::string>& args, std::istream& in, std::ostream& out);

}  // namespace emu
