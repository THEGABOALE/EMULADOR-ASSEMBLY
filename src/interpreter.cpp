#include "interpreter.hpp"

#include <cctype>
#include <map>
#include <stdexcept>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace emu {

namespace {

class ExecError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

bool signBit(uint64_t v, int width) { return (v >> (width - 1)) & 1; }

int64_t signExtend(uint64_t v, int width) {
    if (width == 64) return static_cast<int64_t>(v);
    uint64_t m = widthMask(width);
    v &= m;
    if (signBit(v, width)) v |= ~m;
    return static_cast<int64_t>(v);
}

void setZeroSign(Flags& f, uint64_t res, int width) {
    res &= widthMask(width);
    f.zf = res == 0;
    f.sf = signBit(res, width);
}

// Pila simulada: sólo la región [kStackBase, kStackBase + kStackSize) está
// "mapeada", igual que en el backend de Unicorn.
class StackMemory {
public:
    void write64(uint64_t addr, uint64_t v) {
        check(addr, "escritura");
        for (int i = 0; i < 8; ++i) bytes_[addr + i] = static_cast<uint8_t>(v >> (8 * i));
    }
    uint64_t read64(uint64_t addr) {
        check(addr, "lectura");
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            auto it = bytes_.find(addr + i);
            uint64_t b = it == bytes_.end() ? 0 : it->second;
            v |= b << (8 * i);
        }
        return v;
    }

private:
    // Los 8 bytes [addr, addr+8) deben estar dentro de la pila. No se calcula
    // addr + 8: con addr cerca de 2^64 se desbordaría (p. ej. PUSH con RSP=4).
    static void check(uint64_t addr, const char* kind) {
        if (addr < kStackBase || addr - kStackBase > kStackSize - 8) {
            throw ExecError(std::string("acceso a memoria no mapeada (") + kind +
                            " de 8 bytes en " + hex64(addr) + ")");
        }
    }
    std::map<uint64_t, uint8_t> bytes_;
};

uint64_t operandValue(const CpuState& s, const Operand& o, int width) {
    if (o.kind == OperandKind::Register) return readReg(s, o.reg);
    return o.imm & widthMask(width);  // inmediato truncado/extendido al ancho
}

std::string flagsShort(const Flags& f) {
    return "ZF=" + std::to_string(f.zf) + " SF=" + std::to_string(f.sf) +
           " CF=" + std::to_string(f.cf) + " OF=" + std::to_string(f.of);
}

// Ejecuta una instrucción. Actualiza s.rip y describe el flujo en 'flow'.
void execute(const Program& prog, const Instruction& ins, CpuState& s, StackMemory& mem,
             std::string& flow) {
    const int w = ins.width;
    uint64_t next = s.rip + 1;
    auto dst = [&]() -> const RegRef& { return ins.ops[0].reg; };

    switch (ins.op) {
        case Op::MOV:  // no modifica banderas
            writeReg(s, dst(), operandValue(s, ins.ops[1], w));
            break;
        case Op::ADD: {
            auto r = aluAdd(readReg(s, dst()), operandValue(s, ins.ops[1], w), w);
            writeReg(s, dst(), r.value);
            s.flags = r.flags;
            break;
        }
        case Op::SUB: {
            auto r = aluSub(readReg(s, dst()), operandValue(s, ins.ops[1], w), w);
            writeReg(s, dst(), r.value);
            s.flags = r.flags;
            break;
        }
        case Op::CMP: {
            // Calcula A - B sólo para actualizar banderas; el destino no cambia.
            auto r = aluSub(readReg(s, dst()), operandValue(s, ins.ops[1], w), w);
            s.flags = r.flags;
            break;
        }
        case Op::XOR:
        case Op::AND:
        case Op::OR:
        case Op::TEST: {
            uint64_t a = readReg(s, dst()), b = operandValue(s, ins.ops[1], w);
            uint64_t v = ins.op == Op::XOR ? (a ^ b) : ins.op == Op::OR ? (a | b) : (a & b);
            auto r = aluLogic(v, w);
            if (ins.op != Op::TEST) writeReg(s, dst(), r.value);
            s.flags = r.flags;
            break;
        }
        case Op::IMUL: {
            const Operand& src = ins.ops.size() == 3 ? ins.ops[1] : ins.ops[0];
            const Operand& factor = ins.ops.size() == 3 ? ins.ops[2] : ins.ops[1];
            auto r = aluImul(operandValue(s, src, w), operandValue(s, factor, w), w);
            writeReg(s, dst(), r.value);
            s.flags = r.flags;
            break;
        }
        case Op::INC:
        case Op::DEC: {
            // INC y DEC no modifican CF.
            bool cf = s.flags.cf;
            auto r = ins.op == Op::INC ? aluAdd(readReg(s, dst()), 1, w)
                                       : aluSub(readReg(s, dst()), 1, w);
            writeReg(s, dst(), r.value);
            s.flags = r.flags;
            s.flags.cf = cf;
            break;
        }
        case Op::NEG: {
            // NEG = 0 - x; CF=1 salvo que x sea 0.
            auto r = aluSub(0, readReg(s, dst()), w);
            writeReg(s, dst(), r.value);
            s.flags = r.flags;
            break;
        }
        case Op::NOT:  // no modifica banderas
            writeReg(s, dst(), ~readReg(s, dst()));
            break;
        case Op::PUSH: {
            uint64_t v = ins.ops[0].kind == OperandKind::Register ? readReg(s, ins.ops[0].reg)
                                                                  : ins.ops[0].imm;
            mem.write64(s.gpr[RSP] - 8, v);
            s.gpr[RSP] -= 8;
            break;
        }
        case Op::POP: {
            uint64_t v = mem.read64(s.gpr[RSP]);
            s.gpr[RSP] += 8;
            writeReg(s, dst(), v);
            break;
        }
        case Op::JMP:
            next = ins.target;
            flow = "Salto incondicional JMP a '" + ins.ops[0].label + "': TOMADO";
            break;
        case Op::JCC: {
            bool taken = evalCondition(ins.cond, s.flags);
            std::string up = ins.mnemonic;
            for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            flow = "Salto condicional " + up + " a '" + ins.ops[0].label + "': " +
                   (taken ? "TOMADO" : "NO TOMADO") + " (requiere " + condRequirement(ins.cond) +
                   "; " + flagsShort(s.flags) + ")";
            if (taken) next = ins.target;
            break;
        }
        case Op::CALL:
            mem.write64(s.gpr[RSP] - 8, next);
            s.gpr[RSP] -= 8;
            flow = "CALL a '" + ins.ops[0].label + "': se apila la dirección de retorno #" +
                   std::to_string(next);
            next = ins.target;
            break;
        case Op::RET: {
            uint64_t target = mem.read64(s.gpr[RSP]);
            s.gpr[RSP] += 8;
            if (target == kReturnSentinel) {
                flow = "RET: regresa al llamador (dirección centinela " + hex64(target) +
                       "). Fin de la ejecución";
            } else if (target <= prog.code.size()) {
                flow = "RET: regresa a la instrucción #" + std::to_string(target);
            } else {
                throw ExecError("RET extrajo de la pila una dirección de retorno inválida (" +
                                hex64(target) + ")");
            }
            next = target;
            break;
        }
        case Op::NOP:
            break;
    }
    s.rip = next;
}

}  // namespace

AluOut aluAdd(uint64_t a, uint64_t b, int width) {
    uint64_t m = widthMask(width);
    a &= m;
    b &= m;
    uint64_t res = (a + b) & m;
    Flags f;
    setZeroSign(f, res, width);
    f.cf = res < a;                                   // acarreo sin signo
    f.of = signBit((a ^ res) & (b ^ res), width);     // mismos signos -> signo distinto
    return {res, f};
}

AluOut aluSub(uint64_t a, uint64_t b, int width) {
    uint64_t m = widthMask(width);
    a &= m;
    b &= m;
    uint64_t res = (a - b) & m;
    Flags f;
    setZeroSign(f, res, width);
    f.cf = a < b;                                     // préstamo sin signo
    f.of = signBit((a ^ b) & (a ^ res), width);       // signos distintos -> cambia el de A
    return {res, f};
}

AluOut aluLogic(uint64_t result, int width) {
    Flags f;  // AND/OR/XOR/TEST: CF=0 y OF=0
    uint64_t res = result & widthMask(width);
    setZeroSign(f, res, width);
    return {res, f};
}

AluOut aluImul(uint64_t a, uint64_t b, int width) {
    int64_t sa = signExtend(a, width), sb = signExtend(b, width);
    uint64_t res;
    bool overflow;
    if (width < 64) {
        int64_t full = sa * sb;  // |a|,|b| <= 2^31: el producto cabe en 64 bits
        res = static_cast<uint64_t>(full) & widthMask(width);
        overflow = signExtend(res, width) != full;
    } else {
#if defined(_MSC_VER)
        int64_t high;
        int64_t low = _mul128(sa, sb, &high);
        res = static_cast<uint64_t>(low);
        overflow = high != (low < 0 ? -1 : 0);
#else
        __int128 full = static_cast<__int128>(sa) * sb;
        res = static_cast<uint64_t>(full);
        overflow = full != static_cast<__int128>(static_cast<int64_t>(res));
#endif
    }
    Flags f;
    // Intel: CF=OF=1 si el resultado con signo no cabe en el destino.
    // SF y ZF quedan "indefinidas" según el manual; aquí se calculan sobre el
    // resultado truncado, igual que hace Unicorn/QEMU, para poder comparar motores.
    setZeroSign(f, res, width);
    f.cf = f.of = overflow;
    return {res, f};
}

bool evalCondition(Cond c, const Flags& f) {
    switch (c) {
        case Cond::E: return f.zf;
        case Cond::NE: return !f.zf;
        case Cond::G: return !f.zf && f.sf == f.of;
        case Cond::GE: return f.sf == f.of;
        case Cond::L: return f.sf != f.of;
        case Cond::LE: return f.zf || f.sf != f.of;
        case Cond::A: return !f.cf && !f.zf;
        case Cond::AE: return !f.cf;
        case Cond::B: return f.cf;
        case Cond::BE: return f.cf || f.zf;
        case Cond::S: return f.sf;
        case Cond::NS: return !f.sf;
        case Cond::O: return f.of;
        case Cond::NO: return !f.of;
    }
    return false;
}

std::string condRequirement(Cond c) {
    switch (c) {
        case Cond::E: return "ZF=1";
        case Cond::NE: return "ZF=0";
        case Cond::G: return "ZF=0 y SF=OF, con signo";
        case Cond::GE: return "SF=OF, con signo";
        case Cond::L: return "SF<>OF, con signo";
        case Cond::LE: return "ZF=1 o SF<>OF, con signo";
        case Cond::A: return "CF=0 y ZF=0, sin signo";
        case Cond::AE: return "CF=0, sin signo";
        case Cond::B: return "CF=1, sin signo";
        case Cond::BE: return "CF=1 o ZF=1, sin signo";
        case Cond::S: return "SF=1";
        case Cond::NS: return "SF=0";
        case Cond::O: return "OF=1";
        case Cond::NO: return "OF=0";
    }
    return "?";
}

RunResult runEducational(const Program& prog, const std::vector<InitAssign>& init,
                         const RunOptions& opt) {
    RunResult r;
    r.engine = "Intérprete propio";
    r.logicalRip = true;

    CpuState s = makeInitialState(init, 0);
    StackMemory mem;
    mem.write64(kInitialRsp, kReturnSentinel);
    r.initial = s;

    while (true) {
        if (s.rip == kReturnSentinel) {
            r.reason = StopReason::Returned;
            r.message = "RET devolvió el control al llamador (dirección centinela " +
                        hex64(kReturnSentinel) + ")";
            break;
        }
        if (s.rip >= prog.code.size()) {
            r.reason = StopReason::EndOfCode;
            r.message = "se alcanzó el final del programa sin ejecutar RET";
            break;
        }
        const Instruction& ins = prog.code[s.rip];
        if (r.executed >= opt.maxInstructions) {
            r.reason = StopReason::InstructionLimit;
            r.message = "se alcanzó el límite de " + std::to_string(opt.maxInstructions) +
                        " instrucciones (¿ciclo infinito?); la siguiente era '" + ins.text() +
                        "' en la línea " + std::to_string(ins.line);
            break;
        }
        StepRecord st;
        st.step = r.executed + 1;
        st.ripBefore = s.rip;
        st.text = ins.text();
        st.line = ins.line;
        try {
            execute(prog, ins, s, mem, st.flow);
        } catch (const ExecError& e) {
            r.reason = StopReason::Error;
            r.message = "error en el paso " + std::to_string(st.step) + " (línea " +
                        std::to_string(ins.line) + ", '" + ins.text() + "'): " + e.what();
            break;
        }
        st.after = s;
        r.steps.push_back(st);
        ++r.executed;
    }
    r.final = s;
    return r;
}

}  // namespace emu
