// Analizador de assembly x86-64 en sintaxis Intel (destino primero) para el
// subconjunto educativo. Reconoce etiquetas, comentarios y valores iniciales.
#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "cpu.hpp"

namespace emu {

enum class Op {
    MOV, ADD, SUB, IMUL, CMP, XOR, AND, OR, TEST,
    INC, DEC, NEG, NOT, PUSH, POP,
    JMP, JCC, CALL, RET, NOP
};

// Condiciones de los saltos Jcc.
enum class Cond { E, NE, G, GE, L, LE, A, AE, B, BE, S, NS, O, NO };

enum class OperandKind { Register, Immediate, Label };

struct Operand {
    OperandKind kind = OperandKind::Register;
    RegRef reg;
    uint64_t imm = 0;   // valor ya convertido a 64 bits (complemento a 2)
    std::string label;
    std::string text;   // texto original del operando
};

struct Instruction {
    Op op = Op::NOP;
    Cond cond = Cond::E;          // sólo para Op::JCC
    std::string mnemonic;         // mnemónico escrito por el usuario (en minúsculas)
    std::vector<Operand> ops;
    int line = 0;                 // línea del fuente (1-based)
    size_t target = 0;            // índice de destino para saltos y CALL
    int width = 0;                // ancho del operando principal en bits

    std::string text() const;     // representación legible "add eax, 3"
};

struct Program {
    std::vector<Instruction> code;
    std::map<std::string, size_t> labels;   // etiqueta -> índice de instrucción
    std::vector<InitAssign> init;            // valores iniciales leídos de "; init:"
    std::vector<std::string> warnings;
};

class AsmError : public std::runtime_error {
public:
    AsmError(int line, const std::string& msg)
        : std::runtime_error(line > 0 ? "línea " + std::to_string(line) + ": " + msg : msg),
          line_(line) {}
    int line() const { return line_; }

private:
    int line_;
};

// Analiza el programa completo. Lanza AsmError con la línea del problema.
Program parseProgram(const std::string& source);

// Analiza una lista de valores iniciales: "edi=7 esi=4" o "edi=7, esi=-2".
std::vector<InitAssign> parseInitList(const std::string& text);

// Busca comentarios "; init: edi=7 esi=4" (o "; inicial:") en el fuente.
std::vector<InitAssign> parseInitComments(const std::string& source);

// Convierte un literal numérico (decimal, 0x.., ..h, 0b..). Admite signo.
// Devuelve false si no es un número válido.
bool parseNumber(const std::string& text, uint64_t& magnitude, bool& negative);

// Prepara el texto para Keystone: elimina comentarios, directivas y la línea
// de valores iniciales, conservando etiquetas e instrucciones.
std::string prepareSourceForAssembler(const std::string& source);

const char* condName(Cond c);

// Traduce un mnemónico de salto condicional ("jg", "jnz"...) a su condición.
bool conditionForMnemonic(const std::string& mnemonicLower, Cond& c);

}  // namespace emu
