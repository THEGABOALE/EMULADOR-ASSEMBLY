// Estado de la CPU x86-64 modelado por el emulador: registros de propósito
// general, RIP y las banderas ZF, SF, CF y OF.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace emu {

// Índices en el orden de codificación de x86-64 (el mismo que usa la ISA).
enum Gpr {
    RAX = 0, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
    R8, R9, R10, R11, R12, R13, R14, R15,
    NUM_GPR
};

// Orden en que se muestran los registros en la traza.
extern const std::array<int, NUM_GPR> kDisplayOrder;

struct Flags {
    bool zf = false;  // Zero Flag
    bool sf = false;  // Sign Flag
    bool cf = false;  // Carry Flag
    bool of = false;  // Overflow Flag

    bool operator==(const Flags& o) const {
        return zf == o.zf && sf == o.sf && cf == o.cf && of == o.of;
    }
};

struct CpuState {
    std::array<uint64_t, NUM_GPR> gpr{};
    uint64_t rip = 0;
    Flags flags;
};

// Una "vista" de un registro físico: RAX, EAX, AX, AL y AH son vistas de RAX.
struct RegRef {
    int index = 0;         // registro físico (0..15)
    int width = 64;        // 8, 16, 32 o 64 bits
    bool high8 = false;    // AH, CH, DH, BH (bits 8..15)
    bool needsRex = false; // SPL..DIL y R8..R15 requieren prefijo REX
};

// Busca un registro por nombre (sin distinguir mayúsculas). nullopt si no existe.
std::optional<RegRef> lookupRegister(const std::string& name);
std::string registerName(const RegRef& r);
const char* gprName64(int index);

uint64_t widthMask(int width);
uint64_t readReg(const CpuState& s, const RegRef& r);
// Aplica las reglas reales de x86-64: escribir una vista de 32 bits pone a
// cero los 32 bits superiores; escribir 16 u 8 bits conserva el resto.
void writeReg(CpuState& s, const RegRef& r, uint64_t value);

// Asignación de valor inicial, por ejemplo "edi=7".
struct InitAssign {
    RegRef reg;
    uint64_t value = 0;
    std::string text;
};

// Distribución de memoria compartida por los dos motores, para que el estado
// inicial (RSP, dirección de retorno) sea idéntico.
constexpr uint64_t kCodeBase = 0x400000;
constexpr uint64_t kStackBase = 0x7FF00000;
constexpr uint64_t kStackSize = 0x10000;
constexpr uint64_t kInitialRsp = kStackBase + kStackSize - 8;
// Dirección centinela que se deja en la pila como "dirección de retorno":
// cuando RET salta aquí, la ejecución termina de forma controlada.
constexpr uint64_t kReturnSentinel = 0xDEAD0000;

// Estado inicial común: registros a cero, RSP apuntando a la dirección de
// retorno centinela y los valores iniciales indicados por el usuario.
CpuState makeInitialState(const std::vector<InitAssign>& init, uint64_t rip);

}  // namespace emu
