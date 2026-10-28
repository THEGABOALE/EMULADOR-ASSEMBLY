// Backend de código máquina real: ejecuta bytes x86-64 en una CPU emulada
// (Unicorn Engine), desensambla con Capstone y ensambla con Keystone.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cpu.hpp"
#include "trace.hpp"

namespace emu {

// Disponibilidad de cada biblioteca en esta compilación.
bool unicornAvailable();
bool capstoneAvailable();
bool keystoneAvailable();
std::string libraryVersions();

// Ejecuta 'code' cargado en kCodeBase, una instrucción por vez, capturando el
// estado DESPUÉS de cada instrucción. Requiere Unicorn y Capstone.
RunResult runMachineCode(const std::vector<uint8_t>& code, const std::vector<InitAssign>& init,
                         const RunOptions& opt);

struct DisasmLine {
    uint64_t address = 0;
    std::vector<uint8_t> bytes;
    std::string text;
};

// Desensamblado lineal para mostrar un listado. Si encuentra bytes no
// decodificables se detiene y lo indica en 'error'.
std::vector<DisasmLine> disassemble(const std::vector<uint8_t>& code, uint64_t base,
                                    std::string& error);

struct AssembleResult {
    bool ok = false;
    std::vector<uint8_t> bytes;
    size_t statements = 0;
    std::string error;
};

// Ensambla texto Intel x86-64 con Keystone en la dirección 'address'.
AssembleResult assembleIntel(const std::string& source, uint64_t address);

}  // namespace emu
