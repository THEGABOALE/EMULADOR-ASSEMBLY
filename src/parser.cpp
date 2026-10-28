#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace emu {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '@' ||
           c == '$' || c == '?';
}

bool isIdentChar(char c) {
    return isIdentStart(c) || std::isdigit(static_cast<unsigned char>(c));
}

bool isIdentifier(const std::string& s) {
    if (s.empty() || !isIdentStart(s[0])) return false;
    return std::all_of(s.begin(), s.end(), isIdentChar);
}

// Separa el código del comentario (';', '#' o '//').
void splitComment(const std::string& line, std::string& code, std::string& comment) {
    size_t pos = std::string::npos;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ';' || line[i] == '#' ||
            (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/')) {
            pos = i;
            break;
        }
    }
    if (pos == std::string::npos) {
        code = line;
        comment.clear();
    } else {
        code = line.substr(0, pos);
        size_t start = pos + (line[pos] == '/' ? 2 : 1);
        comment = line.substr(start);
    }
}

// Directivas habituales de NASM/GAS que se aceptan y se ignoran.
bool isDirective(const std::string& firstTokenLower) {
    static const std::set<std::string> d = {
        "section", "segment", "global", "globl", ".globl", ".global", "extern", "bits",
        "default", ".text", ".intel_syntax", ".section", ".code64", "use64", ".type",
        ".p2align", ".align", "align", ".file", ".size", ".ident"};
    return d.count(firstTokenLower) > 0;
}

struct MnemonicInfo {
    Op op;
    Cond cond;
};

const std::map<std::string, MnemonicInfo>& mnemonics() {
    static const std::map<std::string, MnemonicInfo> m = {
        {"mov", {Op::MOV, Cond::E}},   {"add", {Op::ADD, Cond::E}},
        {"sub", {Op::SUB, Cond::E}},   {"imul", {Op::IMUL, Cond::E}},
        {"cmp", {Op::CMP, Cond::E}},   {"xor", {Op::XOR, Cond::E}},
        {"and", {Op::AND, Cond::E}},   {"or", {Op::OR, Cond::E}},
        {"test", {Op::TEST, Cond::E}}, {"inc", {Op::INC, Cond::E}},
        {"dec", {Op::DEC, Cond::E}},   {"neg", {Op::NEG, Cond::E}},
        {"not", {Op::NOT, Cond::E}},   {"push", {Op::PUSH, Cond::E}},
        {"pop", {Op::POP, Cond::E}},   {"jmp", {Op::JMP, Cond::E}},
        {"call", {Op::CALL, Cond::E}}, {"ret", {Op::RET, Cond::E}},
        {"nop", {Op::NOP, Cond::E}},
        // Saltos condicionales (sinónimos de la ISA).
        {"je", {Op::JCC, Cond::E}},    {"jz", {Op::JCC, Cond::E}},
        {"jne", {Op::JCC, Cond::NE}},  {"jnz", {Op::JCC, Cond::NE}},
        {"jg", {Op::JCC, Cond::G}},    {"jnle", {Op::JCC, Cond::G}},
        {"jge", {Op::JCC, Cond::GE}},  {"jnl", {Op::JCC, Cond::GE}},
        {"jl", {Op::JCC, Cond::L}},    {"jnge", {Op::JCC, Cond::L}},
        {"jle", {Op::JCC, Cond::LE}},  {"jng", {Op::JCC, Cond::LE}},
        {"ja", {Op::JCC, Cond::A}},    {"jnbe", {Op::JCC, Cond::A}},
        {"jae", {Op::JCC, Cond::AE}},  {"jnb", {Op::JCC, Cond::AE}},
        {"jnc", {Op::JCC, Cond::AE}},  {"jb", {Op::JCC, Cond::B}},
        {"jnae", {Op::JCC, Cond::B}},  {"jc", {Op::JCC, Cond::B}},
        {"jbe", {Op::JCC, Cond::BE}},  {"jna", {Op::JCC, Cond::BE}},
        {"js", {Op::JCC, Cond::S}},    {"jns", {Op::JCC, Cond::NS}},
        {"jo", {Op::JCC, Cond::O}},    {"jno", {Op::JCC, Cond::NO}},
    };
    return m;
}

// ¿Cabe el inmediato en una instrucción de 'width' bits?
// En x86-64 las operaciones de 64 bits sólo aceptan inmediatos de 32 bits con
// extensión de signo; la excepción es MOV r64, imm64.
bool immediateFits(uint64_t mag, bool neg, int width, bool allowImm64) {
    if (width == 64) {
        if (allowImm64) return true;  // parseNumber ya limita a 64 bits
        return neg ? mag <= 0x80000000ULL : mag <= 0x7FFFFFFFULL;
    }
    uint64_t maxUnsigned = widthMask(width);
    uint64_t maxNegative = 1ULL << (width - 1);
    return neg ? mag <= maxNegative : mag <= maxUnsigned;
}

std::string widthText(const RegRef& r) {
    return registerName(r) + ": " + std::to_string(r.width) + " bits";
}

std::string joinOps(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
    return out;
}

// Divide "eax, 5" respetando que no hay comas dentro de los operandos soportados.
std::vector<std::string> splitOperands(const std::string& s) {
    std::vector<std::string> out;
    if (trim(s).empty()) return out;
    std::string cur;
    for (char c : s) {
        if (c == ',') {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(trim(cur));
    return out;
}

Operand parseOperand(const std::string& text, int line) {
    Operand o;
    o.text = text;
    if (text.empty()) throw AsmError(line, "falta un operando");
    std::string low = lower(text);
    if (text.find('[') != std::string::npos || low.find("ptr") != std::string::npos) {
        throw AsmError(line, "operando de memoria '" + text +
                                 "' no soportado en el intérprete educativo "
                                 "(use el motor real para probarlo)");
    }
    if (auto r = lookupRegister(text)) {
        o.kind = OperandKind::Register;
        o.reg = *r;
        o.text = registerName(*r);
        return o;
    }
    uint64_t mag;
    bool neg;
    if (parseNumber(text, mag, neg)) {
        o.kind = OperandKind::Immediate;
        o.imm = neg ? (~mag + 1) : mag;
        return o;
    }
    if (isIdentifier(text)) {
        o.kind = OperandKind::Label;
        o.label = text;
        return o;
    }
    throw AsmError(line, "operando no válido: '" + text + "'");
}

// Comprueba la forma de los operandos de cada instrucción soportada.
void validate(Instruction& ins, const std::vector<std::string>& rawOps) {
    const int line = ins.line;
    const auto& ops = ins.ops;
    const std::string name = ins.mnemonic;
    auto expectCount = [&](size_t n) {
        if (ops.size() != n)
            throw AsmError(line, "'" + name + "' espera " + std::to_string(n) +
                                     " operando(s) y recibió " + std::to_string(ops.size()));
    };
    auto isReg = [&](size_t i) { return ops[i].kind == OperandKind::Register; };
    auto isImm = [&](size_t i) { return ops[i].kind == OperandKind::Immediate; };
    auto requireReg = [&](size_t i) {
        if (!isReg(i)) {
            throw AsmError(line, "el operando '" + ops[i].text + "' de '" + name +
                                     "' debe ser un registro");
        }
    };
    auto checkImm = [&](size_t i, int width, bool allow64) {
        uint64_t mag;
        bool neg;
        parseNumber(rawOps[i], mag, neg);
        if (!immediateFits(mag, neg, width, allow64)) {
            throw AsmError(line, "el inmediato " + rawOps[i] + " no cabe en un operando de " +
                                     std::to_string(width) + " bits" +
                                     (width == 64 && !allow64
                                          ? " (en 64 bits sólo se admiten inmediatos de 32 "
                                            "bits con signo, salvo en MOV)"
                                          : ""));
        }
    };
    auto checkSameWidth = [&](size_t a, size_t b) {
        if (ops[a].reg.width != ops[b].reg.width) {
            throw AsmError(line, "los operandos tienen tamaños distintos (" +
                                     widthText(ops[a].reg) + ", " + widthText(ops[b].reg) + ")");
        }
    };
    auto checkRexMix = [&]() {
        bool high = false, rex = false;
        for (const auto& o : ops) {
            if (o.kind != OperandKind::Register) continue;
            high |= o.reg.high8;
            rex |= o.reg.needsRex;
        }
        if (high && rex) {
            throw AsmError(line, "AH, BH, CH y DH no pueden combinarse en la misma instrucción "
                                 "con registros que requieren prefijo REX (R8..R15, SPL, BPL, "
                                 "SIL, DIL)");
        }
    };
    auto requireLabel = [&](size_t i) {
        if (ops[i].kind != OperandKind::Label)
            throw AsmError(line, "'" + name + "' espera una etiqueta como destino");
    };

    switch (ins.op) {
        case Op::MOV:
        case Op::ADD:
        case Op::SUB:
        case Op::CMP:
        case Op::XOR:
        case Op::AND:
        case Op::OR:
        case Op::TEST:
            expectCount(2);
            requireReg(0);
            ins.width = ops[0].reg.width;
            if (isReg(1)) {
                checkSameWidth(0, 1);
            } else if (isImm(1)) {
                checkImm(1, ins.width, ins.op == Op::MOV);
            } else {
                throw AsmError(line, "el segundo operando de '" + name +
                                         "' debe ser un registro o un inmediato");
            }
            checkRexMix();
            break;
        case Op::IMUL:
            if (ops.size() == 1)
                throw AsmError(line, "la forma de un operando de IMUL (RDX:RAX) no está "
                                     "soportada; use 'imul reg, reg' o 'imul reg, reg, imm'");
            if (ops.size() != 2 && ops.size() != 3)
                throw AsmError(line, "'imul' espera 2 o 3 operandos");
            requireReg(0);
            ins.width = ops[0].reg.width;
            if (ins.width == 8)
                throw AsmError(line, "IMUL de 2 o 3 operandos no existe con registros de 8 bits");
            if (ops.size() == 2) {
                if (isReg(1)) {
                    checkSameWidth(0, 1);
                } else if (isImm(1)) {
                    checkImm(1, ins.width, false);
                } else {
                    throw AsmError(line, "operando no válido para IMUL");
                }
            } else {
                requireReg(1);
                checkSameWidth(0, 1);
                if (!isImm(2)) throw AsmError(line, "el tercer operando de IMUL debe ser inmediato");
                checkImm(2, ins.width, false);
            }
            break;
        case Op::INC:
        case Op::DEC:
        case Op::NEG:
        case Op::NOT:
            expectCount(1);
            requireReg(0);
            ins.width = ops[0].reg.width;
            break;
        case Op::PUSH:
            expectCount(1);
            ins.width = 64;
            if (isReg(0)) {
                if (ops[0].reg.width != 64)
                    throw AsmError(line, "PUSH sólo admite registros de 64 bits en este emulador");
            } else if (isImm(0)) {
                checkImm(0, 64, false);
            } else {
                throw AsmError(line, "operando no válido para PUSH");
            }
            break;
        case Op::POP:
            expectCount(1);
            requireReg(0);
            ins.width = 64;
            if (ops[0].reg.width != 64)
                throw AsmError(line, "POP sólo admite registros de 64 bits en este emulador");
            break;
        case Op::JMP:
        case Op::JCC:
        case Op::CALL:
            expectCount(1);
            requireLabel(0);
            ins.width = 64;
            break;
        case Op::RET:
            if (!ops.empty())
                throw AsmError(line, "la forma 'ret imm16' no está soportada; use 'ret'");
            ins.width = 64;
            break;
        case Op::NOP:
            expectCount(0);
            break;
    }
}

}  // namespace

const char* condName(Cond c) {
    switch (c) {
        case Cond::E: return "E";
        case Cond::NE: return "NE";
        case Cond::G: return "G";
        case Cond::GE: return "GE";
        case Cond::L: return "L";
        case Cond::LE: return "LE";
        case Cond::A: return "A";
        case Cond::AE: return "AE";
        case Cond::B: return "B";
        case Cond::BE: return "BE";
        case Cond::S: return "S";
        case Cond::NS: return "NS";
        case Cond::O: return "O";
        case Cond::NO: return "NO";
    }
    return "?";
}

bool conditionForMnemonic(const std::string& mnemonicLower, Cond& c) {
    auto it = mnemonics().find(mnemonicLower);
    if (it == mnemonics().end() || it->second.op != Op::JCC) return false;
    c = it->second.cond;
    return true;
}

std::string Instruction::text() const {
    std::vector<std::string> parts;
    for (const auto& o : ops) parts.push_back(o.kind == OperandKind::Label ? o.label : o.text);
    return parts.empty() ? mnemonic : mnemonic + " " + joinOps(parts);
}

bool parseNumber(const std::string& textIn, uint64_t& magnitude, bool& negative) {
    std::string t = trim(textIn);
    negative = false;
    if (!t.empty() && (t[0] == '-' || t[0] == '+')) {
        negative = t[0] == '-';
        t = trim(t.substr(1));
    }
    if (t.empty()) return false;
    std::string low = lower(t);
    int base = 10;
    std::string digits = low;
    // El sufijo 'h' se revisa antes que el prefijo 0b: "0Bh" es hexadecimal (11).
    if (low.size() > 2 && low[0] == '0' && low[1] == 'x') {
        base = 16;
        digits = low.substr(2);
    } else if (low.size() > 1 && low.back() == 'h' && std::isdigit(static_cast<unsigned char>(low[0]))) {
        base = 16;
        digits = low.substr(0, low.size() - 1);
    } else if (low.size() > 2 && low[0] == '0' && low[1] == 'b') {
        base = 2;
        digits = low.substr(2);
    }
    if (digits.empty()) return false;
    uint64_t value = 0;
    for (char c : digits) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return false;
        if (d >= base) return false;
        if (value > (~0ULL - static_cast<uint64_t>(d)) / static_cast<uint64_t>(base)) return false;  // desborda 64 bits
        value = value * base + d;
    }
    magnitude = value;
    if (negative && magnitude > 0x8000000000000000ULL) return false;
    return true;
}

std::vector<InitAssign> parseInitList(const std::string& text) {
    // Normaliza "edi = 7, esi=4" a tokens "edi=7" "esi=4".
    std::string norm;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == ',') c = ' ';
        if (c == '=') {
            while (!norm.empty() && norm.back() == ' ') norm.pop_back();
            norm += '=';
            while (i + 1 < text.size() && std::isspace(static_cast<unsigned char>(text[i + 1]))) ++i;
            continue;
        }
        norm += c;
    }
    std::vector<InitAssign> out;
    std::istringstream in(norm);
    std::string tok;
    while (in >> tok) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 == tok.size())
            throw AsmError(0, "valor inicial mal formado: '" + tok + "' (use registro=valor, p. ej. edi=7)");
        std::string regName = tok.substr(0, eq), val = tok.substr(eq + 1);
        auto reg = lookupRegister(regName);
        if (!reg) throw AsmError(0, "registro desconocido en valores iniciales: '" + regName + "'");
        uint64_t mag;
        bool neg;
        if (!parseNumber(val, mag, neg))
            throw AsmError(0, "valor no numérico para " + regName + ": '" + val + "'");
        if (!immediateFits(mag, neg, reg->width, true))
            throw AsmError(0, "el valor " + val + " no cabe en " + registerName(*reg) + " (" +
                                  std::to_string(reg->width) + " bits)");
        InitAssign a;
        a.reg = *reg;
        a.value = (neg ? (~mag + 1) : mag) & widthMask(reg->width);
        a.text = registerName(*reg) + "=" + val;
        out.push_back(a);
    }
    return out;
}

std::vector<InitAssign> parseInitComments(const std::string& source) {
    std::vector<InitAssign> init;
    std::istringstream in(source);
    std::string raw;
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        std::string code, comment;
        splitComment(raw, code, comment);
        comment = trim(comment);
        std::string c = lower(comment);
        for (const std::string key : {"init:", "inicial:"}) {
            if (c.compare(0, key.size(), key) != 0) continue;
            try {
                auto list = parseInitList(comment.substr(key.size()));
                init.insert(init.end(), list.begin(), list.end());
            } catch (const AsmError& e) {
                throw AsmError(lineNo, e.what());
            }
        }
    }
    return init;
}

Program parseProgram(const std::string& source) {
    Program prog;
    prog.init = parseInitComments(source);
    std::istringstream in(source);
    std::string raw;
    int lineNo = 0;
    std::vector<std::pair<std::string, int>> pendingLabels;

    while (std::getline(in, raw)) {
        ++lineNo;
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::string code, comment;
        splitComment(raw, code, comment);

        std::string rest = trim(code);
        // Etiquetas al principio de la línea (puede haber varias o una instrucción después).
        while (true) {
            size_t colon = rest.find(':');
            if (colon == std::string::npos) break;
            std::string name = trim(rest.substr(0, colon));
            if (!isIdentifier(name)) {
                throw AsmError(lineNo, "etiqueta no válida: '" + name + "'");
            }
            if (lookupRegister(name)) {
                throw AsmError(lineNo, "'" + name + "' es un registro y no puede usarse como etiqueta");
            }
            if (prog.labels.count(name)) {
                throw AsmError(lineNo, "etiqueta duplicada: '" + name + "'");
            }
            prog.labels[name] = prog.code.size();
            rest = trim(rest.substr(colon + 1));
        }
        if (rest.empty()) continue;

        size_t sp = 0;
        while (sp < rest.size() && !std::isspace(static_cast<unsigned char>(rest[sp]))) ++sp;
        std::string mnem = lower(rest.substr(0, sp));
        std::string operandText = trim(rest.substr(sp));

        if (isDirective(mnem)) {
            prog.warnings.push_back("línea " + std::to_string(lineNo) + ": directiva ignorada '" +
                                    rest + "'");
            continue;
        }

        auto it = mnemonics().find(mnem);
        if (it == mnemonics().end()) {
            throw AsmError(lineNo, "instrucción no soportada en el intérprete educativo: '" +
                                       mnem + "'");
        }

        Instruction ins;
        ins.op = it->second.op;
        ins.cond = it->second.cond;
        ins.mnemonic = mnem;
        ins.line = lineNo;
        auto rawOps = splitOperands(operandText);
        for (const auto& r : rawOps) ins.ops.push_back(parseOperand(r, lineNo));
        validate(ins, rawOps);
        prog.code.push_back(ins);
    }

    if (prog.code.empty()) throw AsmError(0, "el programa está vacío: no contiene instrucciones");

    // Resolver destinos de saltos y llamadas.
    for (auto& ins : prog.code) {
        if (ins.op == Op::JMP || ins.op == Op::JCC || ins.op == Op::CALL) {
            auto it = prog.labels.find(ins.ops[0].label);
            if (it == prog.labels.end())
                throw AsmError(ins.line, "etiqueta no definida: '" + ins.ops[0].label + "'");
            ins.target = it->second;
        }
    }
    return prog;
}

std::string prepareSourceForAssembler(const std::string& source) {
    std::istringstream in(source);
    std::ostringstream out;
    std::string raw;
    while (std::getline(in, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        std::string code, comment;
        splitComment(raw, code, comment);
        std::string t = trim(code);
        if (t.empty()) continue;
        size_t sp = 0;
        while (sp < t.size() && !std::isspace(static_cast<unsigned char>(t[sp]))) ++sp;
        if (isDirective(lower(t.substr(0, sp)))) continue;
        out << t << "\n";
    }
    return out.str();
}

}  // namespace emu
