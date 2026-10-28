// Intérprete educativo: implementación propia de la semántica de cada
// instrucción del subconjunto x86-64 visto en clase.
#pragma once

#include <string>
#include <vector>

#include "cpu.hpp"
#include "parser.hpp"
#include "trace.hpp"

namespace emu {

// Ejecuta el programa desde la primera instrucción. RIP es lógico: el índice
// de la instrucción dentro del programa (no una dirección de bytes).
RunResult runEducational(const Program& prog, const std::vector<InitAssign>& init,
                         const RunOptions& opt);

// Evalúa la condición de un Jcc con las banderas actuales.
bool evalCondition(Cond c, const Flags& f);
// Texto de la condición, p. ej. "ZF=0 y SF=OF" para JG.
std::string condRequirement(Cond c);

// Operaciones de la ALU con el cálculo de banderas según el ancho del operando.
// Se exponen para poder probarlas de forma aislada.
struct AluOut {
    uint64_t value;
    Flags flags;
};
AluOut aluAdd(uint64_t a, uint64_t b, int width);
AluOut aluSub(uint64_t a, uint64_t b, int width);
AluOut aluLogic(uint64_t result, int width);
AluOut aluImul(uint64_t a, uint64_t b, int width);

}  // namespace emu
