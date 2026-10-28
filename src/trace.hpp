// Estructuras de traza comunes a los dos motores y su presentación en consola.
#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "cpu.hpp"

namespace emu {

// Registro de una instrucción ejecutada.
struct StepRecord {
    uint64_t step = 0;             // número de paso (1-based)
    uint64_t ripBefore = 0;        // RIP antes de ejecutar
    std::string text;              // instrucción y operandos
    std::vector<uint8_t> bytes;    // código máquina (vacío en el intérprete educativo)
    int line = 0;                  // línea del fuente (sólo intérprete educativo)
    CpuState after;                // estado DESPUÉS de ejecutar la instrucción
    std::string flow;              // salto tomado / no tomado / fin de ejecución
};

enum class StopReason { Returned, EndOfCode, Halted, InstructionLimit, Error };

struct RunResult {
    std::string engine;            // nombre del motor que produjo la traza
    bool logicalRip = false;       // true: RIP es un índice de instrucción, no una dirección
    CpuState initial;
    CpuState final;
    std::vector<StepRecord> steps;
    uint64_t executed = 0;
    StopReason reason = StopReason::Error;
    std::string message;           // motivo de finalización o descripción del error

    bool ok() const {
        return reason == StopReason::Returned || reason == StopReason::EndOfCode ||
               reason == StopReason::Halted;
    }
};

struct RunOptions {
    uint64_t maxInstructions = 1000;  // límite para evitar ciclos infinitos
};

struct TraceOptions {
    bool compact = false;  // sólo registros que cambian (opcional)
};

// Color en la consola (códigos ANSI). Apagado por defecto: main lo activa sólo
// cuando la salida es una consola, así las tuberías y las pruebas reciben texto limpio.
void setColorEnabled(bool on);
bool colorEnabled();
struct Paint {
    const char* code;
};
std::ostream& operator<<(std::ostream& out, Paint p);
namespace paint {
constexpr Paint reset{"\x1b[0m"}, bold{"\x1b[1m"}, red{"\x1b[91m"}, green{"\x1b[92m"},
    yellow{"\x1b[93m"}, cyan{"\x1b[96m"}, dim{"\x1b[90m"};
}

std::string hex64(uint64_t v);
std::string formatRip(uint64_t rip, bool logical);
std::string formatBytes(const std::vector<uint8_t>& bytes);
std::string flagsText(const Flags& f);
const char* stopReasonText(StopReason r);

void printInitialState(std::ostream& out, const RunResult& r);
void printStep(std::ostream& out, const StepRecord& s, const CpuState& prev, bool logicalRip,
               const TraceOptions& opt);
void printFinalState(std::ostream& out, const RunResult& r);
// Estado inicial + traza paso a paso + estado final.
void printRun(std::ostream& out, const RunResult& r, const TraceOptions& opt);

// Compara el resultado de dos motores: forma de terminar, número de
// instrucciones, los 16 registros generales y ZF/SF/CF/OF. RIP no se compara
// porque el intérprete educativo usa un RIP lógico. Devuelve las diferencias.
std::vector<std::string> compareResults(const RunResult& a, const RunResult& b);

}  // namespace emu
