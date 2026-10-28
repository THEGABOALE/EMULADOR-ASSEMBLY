#include "machine.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "interpreter.hpp"
#include "parser.hpp"

#ifdef HAVE_UNICORN
#include <unicorn/unicorn.h>
#endif
#ifdef HAVE_CAPSTONE
#include <capstone/capstone.h>
#endif
#ifdef HAVE_KEYSTONE
#include <keystone/keystone.h>
#endif

namespace emu {

bool unicornAvailable() {
#ifdef HAVE_UNICORN
    return true;
#else
    return false;
#endif
}

bool capstoneAvailable() {
#ifdef HAVE_CAPSTONE
    return true;
#else
    return false;
#endif
}

bool keystoneAvailable() {
#ifdef HAVE_KEYSTONE
    return true;
#else
    return false;
#endif
}

std::string libraryVersions() {
    std::ostringstream o;
#ifdef HAVE_UNICORN
    unsigned int umaj, umin;
    uc_version(&umaj, &umin);
    o << "Unicorn " << umaj << "." << umin;
#else
    o << "Unicorn: no disponible";
#endif
#ifdef HAVE_CAPSTONE
    int cmaj, cmin;
    cs_version(&cmaj, &cmin);
    o << " | Capstone " << cmaj << "." << cmin;
#else
    o << " | Capstone: no disponible";
#endif
#ifdef HAVE_KEYSTONE
    unsigned int kmaj, kmin;
    ks_version(&kmaj, &kmin);
    o << " | Keystone " << kmaj << "." << kmin;
#else
    o << " | Keystone: no disponible";
#endif
    return o.str();
}

// ---------------------------------------------------------------- Capstone

#ifdef HAVE_CAPSTONE
namespace {

struct Disassembler {
    csh handle = 0;
    bool ok = false;
    Disassembler() {
        ok = cs_open(CS_ARCH_X86, CS_MODE_64, &handle) == CS_ERR_OK;
        if (ok) cs_option(handle, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
    }
    ~Disassembler() {
        if (ok) cs_close(&handle);
    }
    // Decodifica una instrucción. Devuelve false si los bytes no forman una.
    bool one(const uint8_t* data, size_t size, uint64_t addr, DisasmLine& out) {
        cs_insn* insn = nullptr;
        size_t n = cs_disasm(handle, data, size, addr, 1, &insn);
        if (n == 0) return false;
        out.address = insn[0].address;
        out.bytes.assign(insn[0].bytes, insn[0].bytes + insn[0].size);
        out.text = insn[0].mnemonic;
        if (insn[0].op_str[0]) out.text += std::string(" ") + insn[0].op_str;
        cs_free(insn, n);
        return true;
    }
};

}  // namespace
#endif

std::vector<DisasmLine> disassemble(const std::vector<uint8_t>& code, uint64_t base,
                                    std::string& error) {
    std::vector<DisasmLine> lines;
    error.clear();
#ifdef HAVE_CAPSTONE
    Disassembler d;
    if (!d.ok) {
        error = "no se pudo inicializar Capstone";
        return lines;
    }
    size_t off = 0;
    while (off < code.size()) {
        DisasmLine l;
        if (!d.one(code.data() + off, code.size() - off, base + off, l)) {
            error = "bytes no decodificables a partir de " + hex64(base + off);
            break;
        }
        off += l.bytes.size();
        lines.push_back(l);
    }
#else
    (void)code;
    (void)base;
    error = "Capstone no está disponible en esta compilación";
#endif
    return lines;
}

// ---------------------------------------------------------------- Unicorn

#if defined(HAVE_UNICORN) && defined(HAVE_CAPSTONE)
namespace {

const int kUcRegs[NUM_GPR] = {
    UC_X86_REG_RAX, UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_RBX,
    UC_X86_REG_RSP, UC_X86_REG_RBP, UC_X86_REG_RSI, UC_X86_REG_RDI,
    UC_X86_REG_R8,  UC_X86_REG_R9,  UC_X86_REG_R10, UC_X86_REG_R11,
    UC_X86_REG_R12, UC_X86_REG_R13, UC_X86_REG_R14, UC_X86_REG_R15};

CpuState readState(uc_engine* uc) {
    CpuState s;
    for (int i = 0; i < NUM_GPR; ++i) uc_reg_read(uc, kUcRegs[i], &s.gpr[i]);
    uc_reg_read(uc, UC_X86_REG_RIP, &s.rip);
    uint64_t rflags = 0;
    uc_reg_read(uc, UC_X86_REG_RFLAGS, &rflags);
    s.flags.cf = rflags & (1u << 0);
    s.flags.zf = rflags & (1u << 6);
    s.flags.sf = rflags & (1u << 7);
    s.flags.of = rflags & (1u << 11);
    return s;
}

void writeState(uc_engine* uc, const CpuState& s) {
    for (int i = 0; i < NUM_GPR; ++i) uc_reg_write(uc, kUcRegs[i], &s.gpr[i]);
    uc_reg_write(uc, UC_X86_REG_RIP, &s.rip);
    uint64_t rflags = 0x2;  // el bit 1 de RFLAGS siempre vale 1
    uc_reg_write(uc, UC_X86_REG_RFLAGS, &rflags);
}

// Información del último acceso inválido a memoria (para el mensaje de error).
struct MemFault {
    bool happened = false;
    uc_mem_type type = UC_MEM_READ_UNMAPPED;
    uint64_t address = 0;
    int size = 0;
};

bool onInvalidMemory(uc_engine*, uc_mem_type type, uint64_t address, int size, int64_t,
                     void* user) {
    auto* f = static_cast<MemFault*>(user);
    f->happened = true;
    f->type = type;
    f->address = address;
    f->size = size;
    return false;  // no se recupera: uc_emu_start devuelve error
}

std::string describeFault(const MemFault& f) {
    std::string kind;
    switch (f.type) {
        case UC_MEM_READ_UNMAPPED: kind = "lectura de memoria no mapeada"; break;
        case UC_MEM_WRITE_UNMAPPED: kind = "escritura en memoria no mapeada"; break;
        case UC_MEM_FETCH_UNMAPPED: kind = "búsqueda de instrucción en memoria no mapeada"; break;
        case UC_MEM_WRITE_PROT: kind = "escritura en memoria protegida"; break;
        case UC_MEM_READ_PROT: kind = "lectura de memoria protegida"; break;
        case UC_MEM_FETCH_PROT: kind = "ejecución de memoria no ejecutable"; break;
        default: kind = "acceso inválido a memoria"; break;
    }
    return kind + " (" + std::to_string(f.size) + " bytes en " + hex64(f.address) + ")";
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string mnemonicOf(const std::string& text) {
    return text.substr(0, text.find(' '));
}

bool isSystemInstruction(const std::string& m) {
    return m == "syscall" || m == "sysenter" || m == "sysexit" || m == "sysret" ||
           m == "int" || m == "int1" || m == "int3" || m == "into" || m == "in" ||
           m == "out" || m == "insb" || m == "outsb";
}

// Describe el efecto sobre el flujo de control, comparando el RIP siguiente
// con la dirección de la instrucción que sigue en memoria.
std::string describeFlow(const std::string& text, uint64_t rip, size_t size, const Flags& before,
                         uint64_t nextRip) {
    std::string m = mnemonicOf(text);
    uint64_t fall = rip + size;
    Cond c;
    if (m == "ret") {
        if (nextRip == kReturnSentinel)
            return "RET: regresa al llamador (dirección centinela " + hex64(nextRip) +
                   "). Fin de la ejecución";
        return "RET: regresa a " + hex64(nextRip);
    }
    if (m == "jmp") return "Salto incondicional JMP a " + hex64(nextRip) + ": TOMADO";
    if (m == "call") return "CALL a " + hex64(nextRip) + ": se apila la dirección de retorno " + hex64(fall);
    if (conditionForMnemonic(m, c)) {
        // Un Jcc no modifica banderas: si se toma lo deciden las banderas de antes.
        // Comparar nextRip con la continuación fallaría cuando el destino es justo
        // la instrucción siguiente (se toma, pero RIP queda igual).
        bool taken = evalCondition(c, before);
        std::string s = "Salto condicional " + upper(m) + ": " + (taken ? "TOMADO" : "NO TOMADO") +
                        " (requiere " + condRequirement(c) + "; banderas antes: " +
                        flagsText(before) + ")";
        // Comprobación cruzada con Unicorn: el RIP real debe ser el destino o la continuación.
        uint64_t target = 0;
        size_t sp = text.find(' ');
        bool haveTarget = false;
        if (sp != std::string::npos) {
            try {
                target = std::stoull(text.substr(sp + 1), nullptr, 16);
                haveTarget = true;
            } catch (...) {
            }
        }
        if (haveTarget && nextRip != (taken ? target : fall))
            s += " [AVISO: el RIP de Unicorn no coincide con la regla teórica]";
        return s;
    }
    if (m.rfind("loop", 0) == 0 || m == "jrcxz" || m == "jecxz")
        return "Salto " + upper(m) + ": " + (nextRip != fall ? "TOMADO" : "NO TOMADO");
    if (m == "hlt") return "HLT: la CPU se detiene. Fin de la ejecución";
    return "";
}

}  // namespace
#endif

RunResult runMachineCode(const std::vector<uint8_t>& code, const std::vector<InitAssign>& init,
                         const RunOptions& opt) {
    RunResult r;
    r.engine = "CPU emulada con Unicorn";
    r.logicalRip = false;
    r.initial = r.final = makeInitialState(init, kCodeBase);
#if defined(HAVE_UNICORN) && defined(HAVE_CAPSTONE)
    if (code.empty()) {
        r.message = "no hay bytes que ejecutar";
        return r;
    }
    if (code.size() > 1024 * 1024) {
        r.message = "el código supera el máximo admitido de 1 MiB";
        return r;
    }

    uc_engine* uc = nullptr;
    uc_err err = uc_open(UC_ARCH_X86, UC_MODE_64, &uc);  // modo x86-64 explícito
    if (err != UC_ERR_OK) {
        r.message = std::string("no se pudo inicializar Unicorn: ") + uc_strerror(err);
        return r;
    }
    Disassembler dis;

    const uint64_t codeEnd = kCodeBase + code.size();
    const size_t mapSize = (code.size() + 0xFFF) & ~static_cast<size_t>(0xFFF);
    // El código se mapea sólo con lectura y ejecución: al ejecutar paso a paso,
    // Unicorn repite indefinidamente una instrucción que escribe en su propia
    // página, así que el código automodificable se rechaza con un error claro.
    uint64_t sentinel = kReturnSentinel;
    uc_err setup = uc_mem_map(uc, kCodeBase, mapSize, UC_PROT_READ | UC_PROT_EXEC);
    if (setup == UC_ERR_OK) setup = uc_mem_write(uc, kCodeBase, code.data(), code.size());
    if (setup == UC_ERR_OK) setup = uc_mem_map(uc, kStackBase, kStackSize, UC_PROT_READ | UC_PROT_WRITE);
    if (setup == UC_ERR_OK) setup = uc_mem_write(uc, kInitialRsp, &sentinel, sizeof sentinel);
    if (setup != UC_ERR_OK) {
        r.message = std::string("no se pudo preparar la memoria emulada: ") + uc_strerror(setup);
        uc_close(uc);
        return r;
    }

    MemFault fault;
    uc_hook hook;
    uc_hook_add(uc, &hook, UC_HOOK_MEM_INVALID, reinterpret_cast<void*>(onInvalidMemory), &fault,
                1, 0);

    writeState(uc, r.initial);
    r.initial = readState(uc);
    CpuState prev = r.initial;

    while (true) {
        uint64_t rip = prev.rip;
        if (rip == kReturnSentinel) {
            r.reason = StopReason::Returned;
            r.message = "RET devolvió el control al llamador (dirección centinela " +
                        hex64(kReturnSentinel) + ")";
            break;
        }
        if (rip == codeEnd) {
            r.reason = StopReason::EndOfCode;
            r.message = "se alcanzó el final de los bytes sin ejecutar RET";
            break;
        }
        if (rip < kCodeBase || rip > codeEnd) {
            r.reason = StopReason::Error;
            r.message = "RIP salió de la región de código cargada (" + hex64(rip) + ")";
            break;
        }
        // Decodificar la instrucción en RIP sólo con los bytes realmente cargados.
        DisasmLine line;
        size_t avail = static_cast<size_t>(codeEnd - rip);
        const uint8_t* at = code.data() + (rip - kCodeBase);
        if (!dis.one(at, std::min<size_t>(avail, 15), rip, line)) {
            std::vector<uint8_t> padded(at, at + std::min<size_t>(avail, 15));
            padded.resize(15, 0);
            DisasmLine probe;
            std::vector<uint8_t> shown(at, at + std::min<size_t>(avail, 8));
            r.reason = StopReason::Error;
            if (dis.one(padded.data(), padded.size(), rip, probe) && probe.bytes.size() > avail) {
                r.message = "instrucción incompleta en " + hex64(rip) + ": '" + probe.text +
                            "' necesita " + std::to_string(probe.bytes.size()) +
                            " bytes y sólo quedan " + std::to_string(avail) + " (" +
                            formatBytes(shown) + ")";
            } else {
                r.message = "instrucción inválida en " + hex64(rip) +
                            ": los bytes " + formatBytes(shown) +
                            " no forman una instrucción x86-64 válida";
            }
            break;
        }
        if (r.executed >= opt.maxInstructions) {
            r.reason = StopReason::InstructionLimit;
            r.message = "se alcanzó el límite de " + std::to_string(opt.maxInstructions) +
                        " instrucciones (¿ciclo infinito?); la siguiente era '" + line.text +
                        "' en " + hex64(rip);
            break;
        }
        std::string m = mnemonicOf(line.text);
        if (isSystemInstruction(m)) {
            r.reason = StopReason::Error;
            r.message = "'" + line.text + "' en " + hex64(rip) +
                        ": las llamadas al sistema, interrupciones y E/S no se emulan";
            break;
        }

        // Ejecutar EXACTAMENTE una instrucción y capturar el estado posterior.
        fault = MemFault{};
        err = uc_emu_start(uc, rip, kReturnSentinel, 0, 1);
        CpuState after = readState(uc);

        StepRecord st;
        st.step = r.executed + 1;
        st.ripBefore = rip;
        st.text = line.text;
        st.bytes = line.bytes;
        st.after = after;

        if (err == UC_ERR_FETCH_UNMAPPED && after.rip != rip && fault.address == after.rip) {
            // La instrucción (normalmente un salto) se ejecutó, pero llevó RIP a
            // una dirección sin memoria: se registra el paso y se informa.
            st.flow = describeFlow(line.text, rip, line.bytes.size(), prev.flags, after.rip);
            r.steps.push_back(st);
            ++r.executed;
            prev = after;
            r.reason = StopReason::Error;
            r.message = "'" + line.text + "' en " + hex64(rip) + " llevó RIP a " +
                        hex64(after.rip) + ", que salió de la región de código cargada "
                        "(memoria no mapeada)";
            break;
        }
        if (err != UC_ERR_OK) {
            r.reason = StopReason::Error;
            std::string detail = fault.happened ? describeFault(fault) : uc_strerror(err);
            if (err == UC_ERR_INSN_INVALID) detail = "instrucción inválida o no soportada por la CPU emulada";
            r.message = "error en el paso " + std::to_string(st.step) + " (" + hex64(rip) + ", '" +
                        line.text + "'): " + detail;
            prev = after;
            break;
        }
        st.flow = describeFlow(line.text, rip, line.bytes.size(), prev.flags, after.rip);
        r.steps.push_back(st);
        ++r.executed;
        prev = after;
        if (m == "hlt") {
            r.reason = StopReason::Halted;
            r.message = "la CPU ejecutó HLT";
            break;
        }
    }
    r.final = prev;
    uc_close(uc);
#else
    (void)code;
    (void)opt;
    r.reason = StopReason::Error;
    r.message = "el backend real no está disponible: esta compilación no incluye Unicorn y Capstone";
#endif
    return r;
}

// ---------------------------------------------------------------- Keystone

#ifdef HAVE_KEYSTONE
namespace {

// Keystone 0.9.2 interpreta a veces los literales decimales como hexadecimales
// ("mov eax, 255" -> 0x255): basta con que algún ks_option(KS_OPT_SYNTAX, ...)
// se haya llamado en el proceso para que la base 16 quede activa de forma
// global. Lo detectaron las pruebas de equivalencia entre motores. Para evitar
// la ambigüedad, todos los literales numéricos se reescriben como 0x...,
// que se lee igual en base 10 y en base 16.
std::string numbersToHex(const std::string& src) {
    std::string out;
    size_t i = 0;
    while (i < src.size()) {
        unsigned char c = static_cast<unsigned char>(src[i]);
        if (std::isalnum(c) || c == '_') {
            size_t j = i;
            while (j < src.size() &&
                   (std::isalnum(static_cast<unsigned char>(src[j])) || src[j] == '_'))
                ++j;
            std::string tok = src.substr(i, j - i);
            uint64_t mag;
            bool neg;
            if (std::isdigit(c) && parseNumber(tok, mag, neg)) {
                std::ostringstream h;
                h << "0x" << std::hex << mag;
                out += h.str();
            } else {
                out += tok;
            }
            i = j;
        } else {
            out += src[i++];
        }
    }
    return out;
}

// Keystone 0.9.2 entra en un ciclo infinito si una sentencia empieza con un
// signo como '[', ',', '-', '\' o ':' (p. ej. "[rax]" o "etiqueta::"); lo
// detectó el fuzzing del ejecutable. Antes de llamarlo se exige que cada
// sentencia, tras sus etiquetas, empiece con una letra o '_'.
bool safeForKeystone(const std::string& src, std::string& error) {
    auto isLabelChar = [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.' || c == '$' || c == '@' || c == '?';
    };
    std::istringstream in(src);
    std::string line;
    while (std::getline(in, line)) {
        size_t i = 0;
        while (true) {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            size_t j = i;
            while (j < line.size() && isLabelChar(static_cast<unsigned char>(line[j]))) ++j;
            if (j > i && j < line.size() && line[j] == ':') {
                i = j + 1;  // etiqueta "nombre:"
                continue;
            }
            break;
        }
        if (i >= line.size()) continue;
        unsigned char c = static_cast<unsigned char>(line[i]);
        size_t b = line.find_first_not_of(" \t");
        const std::string stmt = line.substr(b == std::string::npos ? 0 : b);
        if (c == '.') {  // ".zzz -1" también cuelga a Keystone
            error = "directiva no admitida: '" + stmt + "' (el emulador ejecuta instrucciones)";
            return false;
        }
        if (!std::isalpha(c) && c != '_') {
            error = "sentencia no válida: '" + stmt +
                    "' (cada instrucción debe empezar con su mnemónico, p. ej. 'mov')";
            return false;
        }
        // AH..DH con un registro que exige prefijo REX no es codificable, y
        // Keystone no lo informa como error: termina el proceso ("LLVM ERROR").
        bool high8 = false, rex = false;
        int depth = 0;
        for (size_t k = i; k < line.size();) {
            unsigned char ch = static_cast<unsigned char>(line[k]);
            if (ch == '[') ++depth;
            if (ch == ']') --depth;
            if (!std::isalnum(ch) && ch != '_') {
                ++k;
                continue;
            }
            size_t e = k;
            while (e < line.size() &&
                   (std::isalnum(static_cast<unsigned char>(line[e])) || line[e] == '_'))
                ++e;
            std::string t = line.substr(k, e - k);
            std::transform(t.begin(), t.end(), t.begin(),
                           [](unsigned char x) { return static_cast<char>(std::tolower(x)); });
            k = e;
            auto reg = lookupRegister(t);
            if (!reg) continue;
            if (reg->high8) high8 = true;
            if (reg->needsRex && reg->width == 8) rex = true;               // SIL, R8B...
            if (reg->index >= 8) rex = true;                                // R8..R15
            if (reg->width == 64 && depth == 0) rex = true;                 // REX.W
        }
        if (high8 && rex) {
            error = "sentencia no codificable: '" + stmt +
                    "' (AH, BH, CH y DH no pueden combinarse con registros que requieren "
                    "prefijo REX: R8..R15, SPL, BPL, SIL, DIL o un registro de 64 bits)";
            return false;
        }
    }
    return true;
}

}  // namespace
#endif

AssembleResult assembleIntel(const std::string& sourceIn, uint64_t address) {
    AssembleResult res;
#ifdef HAVE_KEYSTONE
    const std::string source = numbersToHex(sourceIn);
    if (!safeForKeystone(source, res.error)) return res;
    ks_engine* ks = nullptr;
    ks_err e = ks_open(KS_ARCH_X86, KS_MODE_64, &ks);
    if (e != KS_ERR_OK) {
        res.error = std::string("no se pudo inicializar Keystone: ") + ks_strerror(e);
        return res;
    }
    // La sintaxis Intel es la predeterminada en X86; no se llama a ks_option
    // porque activa el problema de base numérica descrito arriba.
    unsigned char* enc = nullptr;
    size_t size = 0, count = 0;
    if (ks_asm(ks, source.c_str(), address, &enc, &size, &count) != 0) {
        res.error = std::string("Keystone no pudo ensamblar el código: ") +
                    ks_strerror(ks_errno(ks));
        // Localizar la línea problemática ensamblando cada línea por separado.
        std::istringstream in(source);
        std::string l;
        while (std::getline(in, l)) {
            if (l.find(':') != std::string::npos) continue;  // etiquetas
            unsigned char* e2 = nullptr;
            size_t s2 = 0, c2 = 0;
            if (ks_asm(ks, l.c_str(), address, &e2, &s2, &c2) != 0) {
                ks_err le = ks_errno(ks);
                if (le != KS_ERR_ASM_SYMBOL_MISSING) {
                    res.error += " (probablemente en la instrucción '" + l + "')";
                    break;
                }
            } else {
                ks_free(e2);
            }
        }
    } else if (size == 0) {
        res.error = "no hay instrucciones que ensamblar";
        ks_free(enc);
    } else {
        res.ok = true;
        res.bytes.assign(enc, enc + size);
        res.statements = count;
        ks_free(enc);
    }
    ks_close(ks);
#else
    (void)sourceIn;
    (void)address;
    res.error = "Keystone no está disponible en esta compilación";
#endif
    return res;
}

}  // namespace emu
