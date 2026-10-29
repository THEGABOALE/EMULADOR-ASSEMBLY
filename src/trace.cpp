#include "trace.hpp"

#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace emu {

namespace {
bool g_color = false;
}

void setColorEnabled(bool on) { g_color = on; }
bool colorEnabled() { return g_color; }
std::ostream& operator<<(std::ostream& out, Paint p) {
    if (g_color) out << p.code;
    return out;
}

std::string hex64(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%016llX", static_cast<unsigned long long>(v));
    return buf;
}

std::string formatRip(uint64_t rip, bool logical) {
    if (rip == kReturnSentinel) return hex64(rip) + " (centinela de retorno)";
    if (logical) return "#" + std::to_string(rip);
    return hex64(rip);
}

std::string formatBytes(const std::vector<uint8_t>& bytes) {
    std::ostringstream o;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i) o << ' ';
        o << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<int>(bytes[i]);
    }
    return o.str();
}

std::string flagsText(const Flags& f) {
    return "ZF=" + std::to_string(f.zf) + " SF=" + std::to_string(f.sf) +
           " CF=" + std::to_string(f.cf) + " OF=" + std::to_string(f.of);
}

const char* stopReasonText(StopReason r) {
    switch (r) {
        case StopReason::Returned: return "RET al llamador";
        case StopReason::EndOfCode: return "fin del código";
        case StopReason::Halted: return "HLT";
        case StopReason::InstructionLimit: return "límite de instrucciones";
        case StopReason::Error: return "error";
    }
    return "?";
}

namespace {

std::string regLabel(int idx) {
    std::string n = gprName64(idx);
    while (n.size() < 3) n += ' ';
    return n;
}

// Dónde deja su resultado una instrucción: en el registro destino (el primer
// operando), en ningún lado (CMP y TEST sólo cambian banderas) o no aplica
// (saltos, CALL, RET, PUSH, NOP...).
struct StepResult {
    enum Kind { None, Register, FlagsOnly } kind = None;
    RegRef reg;
    std::string mnemonic;
};

StepResult stepResult(const std::string& text) {
    StepResult r;
    size_t sp = text.find(' ');
    for (char c : text.substr(0, sp))
        r.mnemonic += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (r.mnemonic == "cmp" || r.mnemonic == "test") {
        r.kind = StepResult::FlagsOnly;
        return r;
    }
    if (sp == std::string::npos || r.mnemonic[0] == 'j' || r.mnemonic == "call" ||
        r.mnemonic == "push" || r.mnemonic.rfind("loop", 0) == 0)
        return r;
    std::string ops = text.substr(sp + 1);
    size_t comma = ops.find(',');
    // MUL, DIV e IMUL de un operando escriben RDX:RAX, no su operando.
    if (comma == std::string::npos &&
        (r.mnemonic == "mul" || r.mnemonic == "div" || r.mnemonic == "idiv" || r.mnemonic == "imul"))
        return r;
    std::string dst = ops.substr(0, comma);
    dst.erase(0, dst.find_first_not_of(' '));
    dst.erase(dst.find_last_not_of(' ') + 1);
    if (auto reg = lookupRegister(dst)) {
        r.kind = StepResult::Register;
        r.reg = *reg;
    }
    return r;
}

std::string upperName(const RegRef& reg) {
    std::string n = registerName(reg);
    for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return n;
}

// Valor de un registro en su tamaño: decimal con signo, hex y, si es negativo,
// también sin signo. Ej.: "11 (0x0000000B)" o "-1 (0xFFFFFFFF; sin signo 4294967295)".
std::string valueText(uint64_t v, int width) {
    uint64_t mask = widthMask(width);
    v &= mask;
    int64_t sv = static_cast<int64_t>(v);
    if (width < 64 && (v >> (width - 1)) & 1) sv = static_cast<int64_t>(v | ~mask);
    char hex[32];
    std::snprintf(hex, sizeof hex, "0x%0*llX", width / 4, static_cast<unsigned long long>(v));
    if (sv < 0) return std::to_string(sv) + " (" + hex + "; sin signo " + std::to_string(v) + ")";
    return std::to_string(v) + " (" + hex + ")";
}

void printRegisters(std::ostream& out, const CpuState& s, const CpuState* prev) {
    for (size_t i = 0; i < kDisplayOrder.size(); ++i) {
        int idx = kDisplayOrder[i];
        bool changed = prev && prev->gpr[idx] != s.gpr[idx];
        out << "  " << regLabel(idx) << "=";
        if (changed)
            out << paint::yellow << hex64(s.gpr[idx]) << "*" << paint::reset;
        else
            out << hex64(s.gpr[idx]) << " ";
        if (i % 4 == 3) out << "\n";
    }
}

}  // namespace

void printInitialState(std::ostream& out, const RunResult& r) {
    out << "\n" << paint::bold << paint::cyan << "=== Motor: " << r.engine << " ===" << paint::reset
        << "\n";
    if (r.logicalRip)
        out << "RIP = número de instrucción (#0, #1...), no una dirección de memoria\n";
    out << "Estado inicial:\n";
    printRegisters(out, r.initial, nullptr);
    out << "  RIP=" << formatRip(r.initial.rip, r.logicalRip) << "\n";
    out << "  Banderas: " << flagsText(r.initial.flags) << "\n";
}

void printStep(std::ostream& out, const StepRecord& s, const CpuState& prev, bool logicalRip,
               const TraceOptions& opt) {
    out << paint::dim << "--------------------------------------------------------------------------------"
        << paint::reset << "\n";
    out << paint::bold << "Paso " << s.step << paint::reset << " | RIP antes: "
        << formatRip(s.ripBefore, logicalRip);
    if (s.line > 0) out << " (línea " << s.line << ")";
    out << " | " << paint::bold << paint::cyan << s.text << paint::reset << "\n";
    if (!s.bytes.empty()) out << "  Bytes: " << formatBytes(s.bytes) << "\n";
    if (opt.compact) {
        bool any = false;
        out << "  Cambios:";
        for (int idx : kDisplayOrder) {
            if (prev.gpr[idx] != s.after.gpr[idx]) {
                out << " " << gprName64(idx) << ": " << hex64(prev.gpr[idx]) << " -> "
                    << paint::yellow << hex64(s.after.gpr[idx]) << paint::reset << ";";
                any = true;
            }
        }
        if (!any) out << " (ningún registro general)";
        out << "\n";
    } else {
        out << "  Registros después de ejecutar (* = cambió):\n";
        printRegisters(out, s.after, &prev);
    }
    StepResult res = stepResult(s.text);
    if (res.kind == StepResult::Register)
        out << "  " << paint::bold << paint::green << "Resultado: " << upperName(res.reg) << " = "
            << valueText(readReg(s.after, res.reg), res.reg.width) << paint::reset << "\n";
    else if (res.kind == StepResult::FlagsOnly)
        out << "  Resultado: no se guarda (" << (res.mnemonic == "cmp" ? "CMP" : "TEST")
            << " sólo actualiza las banderas)\n";
    out << "  Banderas después: " << flagsText(s.after.flags) << "\n";
    out << "  RIP siguiente: " << formatRip(s.after.rip, logicalRip);
    if (!s.flow.empty()) {
        // "NO TOMADO" en rojo y "TOMADO" en verde.
        size_t no = s.flow.find("NO TOMADO"), si = s.flow.find("TOMADO");
        out << " | ";
        if (no != std::string::npos)
            out << s.flow.substr(0, no) << paint::red << "NO TOMADO" << paint::reset
                << s.flow.substr(no + 9);
        else if (si != std::string::npos)
            out << s.flow.substr(0, si) << paint::green << "TOMADO" << paint::reset
                << s.flow.substr(si + 6);
        else
            out << s.flow;
    }
    out << "\n";
}

void printFinalState(std::ostream& out, const RunResult& r) {
    out << "================================================================================\n";
    out << paint::bold << "ESTADO FINAL (" << r.engine << ")" << paint::reset << "\n";
    for (int idx : kDisplayOrder) {
        uint64_t v = r.final.gpr[idx];
        bool changed = v != r.initial.gpr[idx];
        out << "  " << regLabel(idx) << " = " << (changed ? paint::yellow : paint::reset) << hex64(v)
            << paint::reset << "  dec="
            << static_cast<int64_t>(v) << "  (32 bits con signo: "
            << static_cast<int32_t>(static_cast<uint32_t>(v)) << ")\n";
    }
    out << "  RIP = " << formatRip(r.final.rip, r.logicalRip) << "\n";
    out << "  Banderas: ZF=" << r.final.flags.zf << "  SF=" << r.final.flags.sf
        << "  CF=" << r.final.flags.cf << "  OF=" << r.final.flags.of << "\n";
    out << "  Instrucciones ejecutadas: " << r.executed << "\n";
    // El valor de retorno de una función va en EAX (convención System V); además
    // se muestra la última instrucción que dejó un resultado en un registro.
    RegRef eax = *lookupRegister("eax");
    out << "  " << paint::bold << paint::green << "Resultado (EAX, valor de retorno): "
        << valueText(readReg(r.final, eax), 32) << paint::reset << "\n";
    for (auto it = r.steps.rbegin(); it != r.steps.rend(); ++it) {
        StepResult res = stepResult(it->text);
        if (res.kind != StepResult::Register) continue;
        out << "  Última operación con resultado: paso " << it->step << " '" << it->text << "' -> "
            << upperName(res.reg) << " = " << valueText(readReg(it->after, res.reg), res.reg.width)
            << "\n";
        break;
    }
    out << "  Finalización: " << (r.ok() ? paint::green : paint::red) << (r.ok() ? "" : "ERROR - ")
        << stopReasonText(r.reason) << ": " << r.message << paint::reset << "\n";
    out << "================================================================================\n";
}

void printRun(std::ostream& out, const RunResult& r, const TraceOptions& opt) {
    printInitialState(out, r);
    const CpuState* prev = &r.initial;
    for (const auto& s : r.steps) {
        printStep(out, s, *prev, r.logicalRip, opt);
        prev = &s.after;
    }
    if (r.reason == StopReason::Error) {
        out << "--------------------------------------------------------------------------------\n";
        out << paint::red << "*** ERROR: " << r.message << paint::reset << "\n";
    }
    printFinalState(out, r);
}

std::vector<std::string> compareResults(const RunResult& a, const RunResult& b) {
    std::vector<std::string> diffs;
    if (a.reason != b.reason)
        diffs.push_back(std::string("finalización: ") + stopReasonText(a.reason) + " vs " +
                        stopReasonText(b.reason));
    if (a.executed != b.executed)
        diffs.push_back("instrucciones ejecutadas: " + std::to_string(a.executed) + " vs " +
                        std::to_string(b.executed));
    for (int idx : kDisplayOrder) {
        if (a.final.gpr[idx] != b.final.gpr[idx])
            diffs.push_back(std::string(gprName64(idx)) + ": " + hex64(a.final.gpr[idx]) + " vs " +
                            hex64(b.final.gpr[idx]));
    }
    if (!(a.final.flags == b.final.flags))
        diffs.push_back("banderas: " + flagsText(a.final.flags) + " vs " + flagsText(b.final.flags));
    return diffs;
}

}  // namespace emu
