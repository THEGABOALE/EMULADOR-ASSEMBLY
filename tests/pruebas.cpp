// Pruebas automatizadas del emulador (sin dependencias externas).
// Ejecutar: ctest --test-dir build --output-on-failure   o   build/pruebas
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <climits>
#include <cstdint>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "cpu.hpp"
#include "examples.hpp"
#include "hexutil.hpp"
#include "interpreter.hpp"
#include "machine.hpp"
#include "parser.hpp"
#include "trace.hpp"

using namespace emu;

namespace {

int g_checks = 0, g_failures = 0;
std::string g_current;

void report(bool ok, const std::string& what, const char* file, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "  FALLO [" << g_current << "] " << file << ":" << line << ": " << what << "\n";
    }
}

#define CHECK(c) report((c), #c, __FILE__, __LINE__)
#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        auto va_ = (a);                                                                  \
        auto vb_ = (b);                                                                  \
        std::ostringstream m_;                                                           \
        m_ << #a << " == " << #b << "  (obtenido " << va_ << ", esperado " << vb_ << ")"; \
        report(va_ == vb_, m_.str(), __FILE__, __LINE__);                                \
    } while (0)

std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Register {
    Register(const char* n, std::function<void()> f) { registry().emplace_back(n, f); }
};
#define TEST(name)                          \
    void name();                            \
    Register reg_##name(#name, name);       \
    void name()

// ---------------------------------------------------------------- utilidades

RunResult edu(const std::string& src, const std::string& init = "", uint64_t max = 1000) {
    Program p = parseProgram(src);
    auto i = p.init;
    auto extra = parseInitList(init);
    i.insert(i.end(), extra.begin(), extra.end());
    RunOptions o;
    o.maxInstructions = max;
    return runEducational(p, i, o);
}

RunResult real(const std::string& src, const std::string& init = "", uint64_t max = 1000) {
    auto a = assembleIntel(prepareSourceForAssembler(src), kCodeBase);
    if (!a.ok) {
        RunResult r;
        r.message = "ensamblado: " + a.error;
        return r;
    }
    auto i = parseInitComments(src);
    auto extra = parseInitList(init);
    i.insert(i.end(), extra.begin(), extra.end());
    RunOptions o;
    o.maxInstructions = max;
    return runMachineCode(a.bytes, i, o);
}

RunResult bytes(const std::vector<uint8_t>& b, const std::string& init = "") {
    return runMachineCode(b, parseInitList(init), RunOptions{});
}

uint32_t e32(const RunResult& r, int reg) { return static_cast<uint32_t>(r.final.gpr[reg]); }

bool parseFails(const std::string& src, const std::string& expectedFragment) {
    try {
        parseProgram(src);
    } catch (const AsmError& e) {
        std::string msg = e.what();
        if (msg.find(expectedFragment) != std::string::npos) return true;
        std::cout << "    (mensaje recibido: " << msg << ")\n";
        return false;
    }
    return false;
}

// Ejecuta el mismo programa en ambos motores y exige el mismo estado final.
void checkEquivalent(const std::string& src, const std::string& init = "") {
    RunResult a = edu(src, init), b = real(src, init);
    CHECK(a.ok());
    CHECK(b.ok());
    auto diffs = compareResults(a, b);
    for (const auto& d : diffs) std::cout << "    diferencia: " << d << "\n";
    CHECK(diffs.empty());
}

std::string readExample(const std::string& name) {
    std::string s, err;
    if (!readTextFile(std::string(EXAMPLES_DIR) + "/" + name, s, err)) {
        std::cout << "    no se pudo leer " << name << ": " << err << "\n";
    }
    return s;
}

// ---------------------------------------------------------------- registros

TEST(registros_vistas_y_extension_a_cero) {
    CpuState s;
    s.gpr[RAX] = 0xFFFFFFFFFFFFFFFFULL;
    writeReg(s, *lookupRegister("ax"), 0x1234);
    CHECK_EQ(s.gpr[RAX], 0xFFFFFFFFFFFF1234ULL);  // AX conserva los bits altos
    writeReg(s, *lookupRegister("AL"), 0x56);
    CHECK_EQ(s.gpr[RAX], 0xFFFFFFFFFFFF1256ULL);
    writeReg(s, *lookupRegister("ah"), 0x99);
    CHECK_EQ(s.gpr[RAX], 0xFFFFFFFFFFFF9956ULL);
    writeReg(s, *lookupRegister("eax"), 7);
    CHECK_EQ(s.gpr[RAX], 7ULL);                   // EAX pone a cero los 32 bits altos
    s.gpr[R9] = ~0ULL;
    writeReg(s, *lookupRegister("r9d"), 1);
    CHECK_EQ(s.gpr[R9], 1ULL);
    CHECK_EQ(readReg(s, *lookupRegister("eax")), 7ULL);
    CHECK(!lookupRegister("exx"));
    CHECK_EQ(lookupRegister("R15B")->index, static_cast<int>(R15));
}

TEST(vistas_registros_ambos_motores) {
    std::string src = readExample("11_vistas_registros.asm");
    RunResult a = edu(src);
    CHECK_EQ(a.final.gpr[RAX], 0xFFFFFFFFFFFF1256ULL);
    CHECK_EQ(a.final.gpr[RBX], 5ULL);
    CHECK_EQ(a.final.gpr[RCX], 0xAB00ULL);
    checkEquivalent(src);
}

// ---------------------------------------------------------------- parser

TEST(parser_etiquetas_comentarios_y_numeros) {
    Program p = parseProgram(
        "; comentario\n"
        "section .text\n"
        "inicio: mov eax, 0x10  # comentario\n"
        "  add eax, 0Fh // otro\n"
        "  sub eax, -2\n"
        "fin:\n"
        "  ret\n");
    CHECK_EQ(p.code.size(), size_t(4));
    CHECK_EQ(p.labels.at("inicio"), size_t(0));
    CHECK_EQ(p.labels.at("fin"), size_t(3));
    CHECK_EQ(p.warnings.size(), size_t(1));  // directiva ignorada
    RunResult r = edu("mov eax, 0x10\nadd eax, 0Fh\nsub eax, -2\nret\n");
    CHECK_EQ(e32(r, RAX), 0x10u + 0xFu + 2u);
    uint64_t m;
    bool n;
    CHECK(parseNumber("0b101", m, n) && m == 5 && !n);
    CHECK(parseNumber("-128", m, n) && m == 128 && n);
    CHECK(!parseNumber("12a", m, n));
    CHECK(!parseNumber("0x1FFFFFFFFFFFFFFFF", m, n));
}

TEST(valores_iniciales) {
    auto v = parseInitList("edi=7, esi = -2 rax=0x10");
    CHECK_EQ(v.size(), size_t(3));
    CHECK_EQ(v[1].value, 0xFFFFFFFEULL);  // -2 en 32 bits
    bool threw = false;
    try {
        parseInitList("al=300");
    } catch (const AsmError&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        parseInitList("edi:7");
    } catch (const AsmError&) {
        threw = true;
    }
    CHECK(threw);
    Program p = parseProgram("; init: edi=3\nmov eax, edi\nret\n");
    CHECK_EQ(p.init.size(), size_t(1));
}

TEST(errores_de_sintaxis_y_no_soportadas) {
    CHECK(parseFails("lea eax, [rbx+4]\n", "no soportada"));
    CHECK(parseFails("mov eax, [rbx]\n", "memoria"));
    CHECK(parseFails("movv eax, 1\n", "no soportada"));
    CHECK(parseFails("mov eax, rbx\n", "tamaños distintos"));
    CHECK(parseFails("mov eax\n", "espera 2"));
    CHECK(parseFails("add rax, 0x100000000\n", "no cabe"));
    CHECK(parseFails("mov al, 256\n", "no cabe"));
    CHECK(parseFails("jmp noexiste\n", "etiqueta no definida"));
    CHECK(parseFails("a:\na:\nret\n", "duplicada"));
    CHECK(parseFails("mov ah, r8b\n", "REX"));
    CHECK(parseFails("imul ecx\n", "un operando"));
    CHECK(parseFails("mov 5, eax\n", "registro"));
    CHECK(parseFails("; sólo comentarios\n", "vacío"));
    CHECK(parseFails("mov eax, 1\nmov ebx, %%\n", "línea 2"));
    // mov r64, imm64 sí es válido.
    RunResult r = edu("mov rax, 0x123456789ABCDEF0\nret\n");
    CHECK_EQ(r.final.gpr[RAX], 0x123456789ABCDEF0ULL);
}

// ---------------------------------------------------------------- ALU y banderas

TEST(mov_add_sub_imul) {
    RunResult r = edu("mov eax, 20\nmov ebx, eax\nadd eax, 5\nsub ebx, 30\nmov ecx, 6\n"
                      "imul ecx, ebx\nimul edx, eax, 3\nimul eax, 2\nret\n");
    CHECK(r.ok());
    CHECK_EQ(e32(r, RAX), 50u);
    CHECK_EQ(static_cast<int32_t>(e32(r, RBX)), -10);
    CHECK_EQ(static_cast<int32_t>(e32(r, RCX)), -60);
    CHECK_EQ(e32(r, RDX), 75u);
    CHECK_EQ(r.final.gpr[RBX] >> 32, 0ULL);  // escritura de 32 bits extiende con ceros
    checkEquivalent("mov eax, 20\nmov ebx, eax\nadd eax, 5\nsub ebx, 30\nmov ecx, 6\n"
                    "imul ecx, ebx\nimul edx, eax, 3\nimul eax, 2\nret\n");
}

TEST(banderas_add) {
    auto r = aluAdd(0xFF, 1, 8);  // 255 + 1 en 8 bits
    CHECK_EQ(r.value, 0ULL);
    CHECK(r.flags.zf && r.flags.cf && !r.flags.of && !r.flags.sf);
    r = aluAdd(0x7FFFFFFF, 1, 32);  // INT_MAX + 1: desbordamiento con signo
    CHECK(r.flags.of && r.flags.sf && !r.flags.cf && !r.flags.zf);
    r = aluAdd(0xFFFFFFFF, 1, 32);
    CHECK(r.flags.cf && r.flags.zf && !r.flags.of);
    r = aluAdd(0xFFFFFFFFFFFFFFFFULL, 1, 64);
    CHECK(r.flags.cf && r.flags.zf);
    r = aluAdd(3, 4, 32);
    CHECK(!r.flags.cf && !r.flags.zf && !r.flags.sf && !r.flags.of);
}

TEST(banderas_sub_y_cmp_no_modifica) {
    auto r = aluSub(5, 5, 32);
    CHECK(r.flags.zf && !r.flags.cf && !r.flags.sf && !r.flags.of);
    r = aluSub(2, 9, 32);  // 2 - 9: negativo, préstamo
    CHECK(!r.flags.zf && r.flags.sf && r.flags.cf && !r.flags.of);
    r = aluSub(0x80000000, 1, 32);  // INT_MIN - 1: desbordamiento
    CHECK(r.flags.of && !r.flags.sf);
    RunResult x = edu("mov eax, 10\nmov ebx, 15\ncmp eax, ebx\nret\n");
    CHECK_EQ(e32(x, RAX), 10u);  // CMP no guarda la resta
    CHECK_EQ(e32(x, RBX), 15u);
    CHECK(x.final.flags.sf && x.final.flags.cf && !x.final.flags.zf);
    checkEquivalent("mov eax, 10\nmov ebx, 15\ncmp eax, ebx\nret\n");
    checkEquivalent("mov eax, 0x80000000\ncmp eax, 1\nret\n");
    checkEquivalent("mov al, 0x80\ncmp al, 1\nret\n");
}

TEST(banderas_inc_dec_neg_logicas_imul) {
    // INC/DEC no modifican CF.
    RunResult r = edu("mov eax, 0xFFFFFFFF\nadd eax, 1\nmov ebx, 0xFFFFFFFF\ninc ebx\nret\n");
    CHECK(r.final.flags.cf && r.final.flags.zf);
    r = edu("mov eax, 1\nsub eax, 2\ndec eax\nret\n");  // CF=1 de SUB se conserva
    CHECK(r.final.flags.cf);
    CHECK_EQ(static_cast<int32_t>(e32(r, RAX)), -2);
    r = edu("mov eax, 0\nneg eax\nret\n");
    CHECK(!r.final.flags.cf && r.final.flags.zf);
    r = edu("mov eax, 0x80000000\nneg eax\nret\n");  // NEG INT_MIN
    CHECK(r.final.flags.of && r.final.flags.cf);
    CHECK_EQ(e32(r, RAX), 0x80000000u);
    r = edu("mov eax, 2\ncmp eax, 3\nxor eax, eax\nret\n");  // XOR pone CF=OF=0
    CHECK(r.final.flags.zf && !r.final.flags.cf && !r.final.flags.of);
    auto m = aluImul(0x40000000, 4, 32);  // desborda 32 bits con signo
    CHECK(m.flags.cf && m.flags.of);
    m = aluImul(static_cast<uint64_t>(-3) & 0xFFFFFFFF, 4, 32);
    CHECK(!m.flags.cf && !m.flags.of && m.value == 0xFFFFFFF4ULL);
    checkEquivalent("mov eax, 0xFFFFFFFF\nadd eax, 1\nmov ebx, 0xFFFFFFFF\ninc ebx\nret\n");
    checkEquivalent("mov eax, 1\nsub eax, 2\ndec eax\nret\n");
    checkEquivalent("mov eax, 0x80000000\nneg eax\nret\n");
    checkEquivalent("mov eax, 0x40000000\nimul eax, eax, 4\nret\n");
    checkEquivalent("mov rax, 0x7FFFFFFFFFFFFFFF\nimul rax, rax, 2\nret\n");
    checkEquivalent("mov eax, 0xF0\nand eax, 0x3C\nor eax, 1\nnot eax\ntest eax, eax\nret\n");
    checkEquivalent("mov ax, 0x7FFF\nadd ax, 1\nret\n");
}

// ---------------------------------------------------------------- saltos

TEST(condiciones_de_salto) {
    Flags f;  // todo a cero: p. ej. CMP 5, 3
    CHECK(evalCondition(Cond::G, f) && evalCondition(Cond::GE, f) && evalCondition(Cond::A, f));
    CHECK(!evalCondition(Cond::E, f) && evalCondition(Cond::NE, f) && !evalCondition(Cond::B, f));
    f.zf = true;  // iguales
    CHECK(evalCondition(Cond::E, f) && !evalCondition(Cond::G, f) && evalCondition(Cond::GE, f));
    CHECK(evalCondition(Cond::LE, f) && !evalCondition(Cond::L, f) && evalCondition(Cond::BE, f));
    f = Flags{};
    f.sf = true;  // negativo sin desbordamiento: menor con signo
    CHECK(evalCondition(Cond::L, f) && !evalCondition(Cond::GE, f));
    f.of = true;  // SF=OF -> mayor o igual con signo
    CHECK(evalCondition(Cond::GE, f) && !evalCondition(Cond::L, f));
    f = Flags{};
    f.cf = true;  // JC/JB
    CHECK(evalCondition(Cond::B, f) && !evalCondition(Cond::A, f) && !evalCondition(Cond::AE, f));
    // JC y JGE no son equivalentes: con CF=1, SF=OF=0 ambos se cumplen; con CF=0, SF=1 ninguno.
    f = Flags{};
    f.sf = true;
    CHECK(!evalCondition(Cond::B, f) && !evalCondition(Cond::GE, f));
    f = Flags{};
    CHECK(!evalCondition(Cond::B, f) && evalCondition(Cond::GE, f));
}

// Programa que devuelve en EAX 1 si el salto se toma y 0 si no.
std::string jumpProgram(const std::string& jcc) {
    return "    xor eax, eax\n    cmp edi, esi\n    " + jcc +
           " salta\n    ret\nsalta:\n    mov eax, 1\n    ret\n";
}

TEST(saltos_je_jne_jg_jl_jge_jle_jc_ja_jb) {
    struct Case {
        const char* jcc;
        int a, b;
        uint32_t taken;
    } cases[] = {
        {"je", 5, 5, 1},  {"je", 5, 6, 0},   {"jz", 0, 0, 1},   {"jne", 5, 6, 1},
        {"jne", 5, 5, 0}, {"jnz", 1, 2, 1},  {"jg", 8, 5, 1},   {"jg", 5, 5, 0},
        {"jg", 2, 9, 0},  {"jg", -1, 1, 0},  {"jl", -1, 1, 1},  {"jl", 3, 3, 0},
        {"jge", 3, 3, 1}, {"jge", 2, 3, 0},  {"jge", 0, -5, 1}, {"jle", 3, 3, 1},
        {"jle", 4, 3, 0}, {"jc", 2, 9, 1},   {"jc", 9, 2, 0},   {"jc", -1, 1, 0},
        {"jb", 1, -1, 1}, {"ja", -1, 1, 1},  {"ja", 1, -1, 0},  {"ja", 5, 5, 0},
        {"jae", 5, 5, 1}, {"jbe", 5, 5, 1},  {"js", 1, 2, 1},   {"jns", 2, 1, 1},
        {"jo", static_cast<int>(0x80000000u), 1, 1},            {"jno", 1, 2, 1},
    };
    for (const auto& c : cases) {
        std::string init = "edi=" + std::to_string(c.a) + " esi=" + std::to_string(c.b);
        std::string src = jumpProgram(c.jcc);
        RunResult r = edu(src, init);
        g_current = std::string("saltos ") + c.jcc + " " + init;
        CHECK_EQ(e32(r, RAX), c.taken);
        RunResult m = real(src, init);
        CHECK_EQ(e32(m, RAX), c.taken);
        checkEquivalent(src, init);
    }
    g_current = "saltos_je_jne_jg_jl_jge_jle_jc_ja_jb";
}

// ---------------------------------------------------------------- ejemplos de clase

TEST(ejemplo1_suma) {
    std::string src = readExample("01_suma.asm");
    CHECK_EQ(e32(edu(src), RAX), 11u);
    CHECK_EQ(e32(real(src), RAX), 11u);
    checkEquivalent(src);
    // También con valores de la diapositiva "Ejercicio 1": a=7, b=4 en mov/sub.
    CHECK_EQ(e32(edu("mov eax, edi\nsub eax, esi\nret\n", "edi=7 esi=4"), RAX), 3u);
}

TEST(ejemplo2_mayor_tres_caminos) {
    std::string src = readExample("02_mayor.asm");
    struct { const char* init; uint32_t eax; bool jgTaken; } c[] = {
        {"edi=8 esi=5", 8, true}, {"edi=2 esi=9", 9, false}, {"edi=5 esi=5", 5, false}};
    for (auto& k : c) {
        RunResult r = edu(src, k.init);
        CHECK_EQ(e32(r, RAX), k.eax);
        CHECK_EQ(r.steps.size(), size_t(k.jgTaken ? 4 : 5));  // camino distinto en la traza
        CHECK(r.steps[2].flow.find(k.jgTaken ? ": TOMADO" : "NO TOMADO") != std::string::npos);
        CHECK_EQ(e32(r, RDI), static_cast<uint32_t>(std::stoi(std::string(k.init).substr(4))));
        RunResult m = real(src, k.init);
        CHECK_EQ(e32(m, RAX), k.eax);
        CHECK(m.steps[2].flow.find(k.jgTaken ? ": TOMADO" : "NO TOMADO") != std::string::npos);
        checkEquivalent(src, k.init);
    }
    // Caso (5,5): ZF=1 tras CMP.
    RunResult r = edu(src, "edi=5 esi=5");
    CHECK(r.steps[1].after.flags.zf);
}

TEST(ejemplo3_if_else_edad) {
    std::string src = readExample("03_edad_if_else.asm");
    struct { int edad; uint32_t eax; } c[] = {{17, 0}, {18, 1}, {25, 1}};
    for (auto& k : c) {
        std::string init = "edi=" + std::to_string(k.edad);
        CHECK_EQ(e32(edu(src, init), RAX), k.eax);
        CHECK_EQ(e32(real(src, init), RAX), k.eax);
        checkEquivalent(src, init);
    }
}

TEST(ejemplo4_while_termina) {
    std::string src = readExample("04_while.asm");
    RunResult r = edu(src);
    CHECK(r.ok());
    CHECK_EQ(e32(r, RAX), 3u);
    int cmps = 0, adds = 0;
    for (const auto& s : r.steps) {
        if (s.text.rfind("cmp", 0) == 0) ++cmps;
        if (s.text.rfind("add", 0) == 0) ++adds;
    }
    CHECK_EQ(cmps, 4);
    CHECK_EQ(adds, 3);
    RunResult m = real(src);
    CHECK_EQ(e32(m, RAX), 3u);
    CHECK_EQ(m.executed, r.executed);
    checkEquivalent(src);
}

TEST(ejemplo5_cmp_no_modifica_destino) {
    std::string src = readExample("05_cmp_no_modifica.asm");
    for (const RunResult& r : {edu(src), real(src)}) {
        CHECK_EQ(e32(r, RAX), 10u);
        CHECK_EQ(e32(r, RBX), 15u);
        CHECK_EQ(e32(r, RCX), 0u);
    }
    checkEquivalent(src);
}

TEST(ejemplo6_acarreo_jc) {
    std::string con = readExample("06_acarreo_jc.asm");
    std::string sin = readExample("06_sin_acarreo_jc.asm");
    RunResult a = edu(con);
    CHECK(a.steps[1].after.flags.cf);  // ADD produjo CF=1
    CHECK(a.steps[2].flow.find(": TOMADO") != std::string::npos);
    CHECK_EQ(e32(a, RBX), 1u);
    RunResult b = edu(sin);
    CHECK(!b.steps[1].after.flags.cf);
    CHECK(b.steps[2].flow.find("NO TOMADO") != std::string::npos);
    CHECK_EQ(e32(b, RBX), 0u);
    CHECK_EQ(e32(b, RAX), 6u);
    CHECK_EQ(e32(real(con), RBX), 1u);
    CHECK_EQ(e32(real(sin), RBX), 0u);
    checkEquivalent(con);
    checkEquivalent(sin);
}

TEST(ejemplo_jg_frente_a_ja) {
    std::string src = readExample("07_jg_vs_ja.asm");
    for (const RunResult& r : {edu(src), real(src)}) {
        CHECK_EQ(e32(r, RCX), 0u);  // JG no tomado
        CHECK_EQ(e32(r, RDX), 1u);  // JA tomado
    }
    checkEquivalent(src);
}

TEST(ejemplos_for_absoluto_imul_ciclo) {
    CHECK_EQ(e32(edu(readExample("08_for_suma.asm")), RAX), 10u);
    checkEquivalent(readExample("08_for_suma.asm"));
    std::string absS = readExample("09_absoluto_neg.asm");
    for (int x : {-3, 0, 5}) {
        std::string init = "edi=" + std::to_string(x);
        CHECK_EQ(static_cast<int32_t>(e32(edu(absS, init), RAX)), x < 0 ? -x : x);
        checkEquivalent(absS, init);
    }
    CHECK_EQ(e32(edu(readExample("10_imul_sub.asm")), RAX), 11u);
    checkEquivalent(readExample("10_imul_sub.asm"));
    RunResult c = edu(readExample("13_ciclo_jl.asm"));
    CHECK_EQ(e32(c, RAX), 10u);
    CHECK_EQ(e32(c, RCX), 5u);
    checkEquivalent(readExample("13_ciclo_jl.asm"));
}

TEST(ejemplos_embebidos_del_menu) {
    for (const auto& ex : classExamples()) {
        g_current = "ejemplo embebido: " + ex.title;
        checkEquivalent(ex.source);
    }
    g_current = "ejemplos_embebidos_del_menu";
}

TEST(ejemplo7_codigo_maquina_real) {
    std::vector<uint8_t> code;
    std::string err;
    CHECK(parseHexBytes("B8 07 00 00 00 83 C0 03 C3", code, err));
    RunResult r = bytes(code);
    CHECK(r.ok());
    CHECK_EQ(r.reason == StopReason::Returned, true);
    CHECK_EQ(e32(r, RAX), 10u);
    CHECK_EQ(r.executed, 3ULL);
    CHECK_EQ(r.steps[0].text, std::string("mov eax, 7"));
    CHECK_EQ(r.steps[1].text, std::string("add eax, 3"));
    CHECK_EQ(r.steps[2].text, std::string("ret"));
    CHECK_EQ(formatBytes(r.steps[1].bytes), std::string("83 C0 03"));
    // Los bytes que genera Keystone coinciden con la diapositiva 11 (sesión 1).
    auto a = assembleIntel("mov eax, edi\nadd eax, esi\nret\n", kCodeBase);
    CHECK(a.ok);
    CHECK_EQ(formatBytes(a.bytes), std::string("89 F8 01 F0 C3"));
    // Regresión: Keystone 0.9.2 puede leer decimales como hexadecimales; los
    // literales deben llegar con su valor decimal correcto.
    a = assembleIntel("mov eax, 255\ncmp edi, 18\nadd eax, -2\nmov ecx, 0FFh\n", kCodeBase);
    CHECK(a.ok);
    CHECK_EQ(formatBytes(a.bytes), std::string("B8 FF 00 00 00 83 FF 12 83 C0 FE B9 FF 00 00 00"));
}

// ---------------------------------------------------------------- traza

TEST(traza_estado_posterior_correcto) {
    // El estado registrado en cada paso debe ser el POSTERIOR a la instrucción.
    std::vector<uint8_t> code = {0xB8, 0x07, 0, 0, 0, 0x83, 0xC0, 0x03, 0xC3};
    RunResult r = bytes(code);
    CHECK_EQ(r.initial.gpr[RAX], 0ULL);
    CHECK_EQ(r.steps[0].after.gpr[RAX], 7ULL);   // tras MOV, no antes
    CHECK_EQ(r.steps[1].after.gpr[RAX], 10ULL);  // tras ADD
    CHECK_EQ(r.steps[0].ripBefore, kCodeBase);
    CHECK_EQ(r.steps[0].after.rip, kCodeBase + 5);
    CHECK_EQ(r.steps[1].ripBefore, kCodeBase + 5);
    CHECK_EQ(r.steps[2].ripBefore, kCodeBase + 8);
    CHECK_EQ(r.steps[2].after.rip, kReturnSentinel);  // RET al centinela
    CHECK_EQ(r.steps[2].after.gpr[RSP], kInitialRsp + 8);
    // Encadenamiento: RIP siguiente de un paso = RIP antes del siguiente (ambos motores).
    for (const RunResult& x : {edu(readExample("04_while.asm")), real(readExample("04_while.asm"))}) {
        for (size_t i = 0; i + 1 < x.steps.size(); ++i)
            CHECK_EQ(x.steps[i].after.rip, x.steps[i + 1].ripBefore);
    }
    // Salto tomado en el motor real: RIP siguiente es el destino, no la siguiente instrucción.
    RunResult m = real(readExample("02_mayor.asm"), "edi=8 esi=5");
    CHECK_EQ(m.steps[2].text, std::string("jg 0x400008"));
    CHECK_EQ(m.steps[2].after.rip, kCodeBase + 8);
    // Banderas por paso: tras CMP 2,9 SF=1 y CF=1 en el paso 2.
    RunResult n = real(readExample("02_mayor.asm"), "edi=2 esi=9");
    CHECK(n.steps[1].after.flags.sf && n.steps[1].after.flags.cf);
    // La salida impresa contiene el estado completo de los 16 registros en cada paso.
    std::ostringstream out;
    printRun(out, r, TraceOptions{});
    std::string t = out.str();
    size_t count = 0;
    for (size_t p = t.find("R15="); p != std::string::npos; p = t.find("R15=", p + 1)) ++count;
    CHECK_EQ(count, size_t(1 + 3));  // estado inicial + 3 pasos
    CHECK(t.find("Bytes: 83 C0 03") != std::string::npos);
    CHECK(t.find("Instrucciones ejecutadas: 3") != std::string::npos);
}

TEST(ret_y_fin_de_codigo) {
    RunResult a = edu("mov eax, 1\n");
    CHECK(a.reason == StopReason::EndOfCode);
    RunResult b = real("mov eax, 1\n");
    CHECK(b.reason == StopReason::EndOfCode);
    CHECK_EQ(e32(b, RAX), 1u);
    // CALL/PUSH/POP con la pila simulada, equivalentes en ambos motores.
    std::string src =
        "    mov edi, 5\n    call doble\n    push rax\n    pop rbx\n    ret\n"
        "doble:\n    mov eax, edi\n    add eax, eax\n    ret\n";
    RunResult c = edu(src);
    CHECK_EQ(e32(c, RBX), 10u);
    CHECK(c.reason == StopReason::Returned);
    checkEquivalent(src);
    // POP con la pila vacía lee la dirección centinela; un segundo POP sale de la pila.
    RunResult d = edu("pop rax\npop rbx\nret\n");
    CHECK(d.reason == StopReason::Error);
    CHECK(d.message.find("no mapeada") != std::string::npos);
    RunResult e = real("pop rax\npop rbx\nret\n");
    CHECK(e.reason == StopReason::Error);
    CHECK(e.message.find("no mapeada") != std::string::npos);
}

TEST(limite_de_instrucciones) {
    std::string src = readExample("12_ciclo_infinito.asm");
    RunResult a = edu(src, "", 50);
    CHECK(a.reason == StopReason::InstructionLimit);
    CHECK_EQ(a.executed, 50ULL);
    RunResult b = real(src, "", 50);
    CHECK(b.reason == StopReason::InstructionLimit);
    CHECK_EQ(b.executed, 50ULL);
    CHECK(compareResults(a, b).empty());
    RunResult c = bytes({0xEB, 0xFE});  // jmp $ (salto a sí mismo)
    CHECK(c.reason == StopReason::InstructionLimit);
}

// ---------------------------------------------------------------- código máquina: errores

TEST(hex_formatos_y_errores) {
    std::vector<uint8_t> v;
    std::string err;
    CHECK(parseHexBytes("B8,07 0x00 \\x00 0000", v, err) && v.size() == 6);
    CHECK(parseHexBytes("b807000000", v, err) && v.size() == 5 && v[0] == 0xB8);
    CHECK(!parseHexBytes("", v, err));
    CHECK(!parseHexBytes("B8 0", v, err));
    CHECK(err.find("impar") != std::string::npos);
    CHECK(!parseHexBytes("B8 ZZ", v, err));
    CHECK(err.find("no hexadecimal") != std::string::npos);
}

TEST(codigo_maquina_errores) {
    RunResult r = bytes({0xB8, 0x07});  // MOV EAX, imm32 incompleto
    CHECK(r.reason == StopReason::Error);
    CHECK(r.message.find("incompleta") != std::string::npos);
    r = bytes({0x0F, 0x0B});  // UD2: instrucción inválida a propósito
    CHECK(r.reason == StopReason::Error);
    r = bytes({0xFF, 0xFF});  // codificación inexistente
    CHECK(r.reason == StopReason::Error);
    CHECK(r.message.find("inválida") != std::string::npos);
    r = bytes({0x8B, 0x04, 0x25, 0x10, 0x00, 0x00, 0x00, 0xC3});  // mov eax, [0x10]
    CHECK(r.reason == StopReason::Error);
    CHECK(r.message.find("no mapeada") != std::string::npos);
    r = bytes({0x0F, 0x05});  // syscall
    CHECK(r.reason == StopReason::Error);
    CHECK(r.message.find("sistema") != std::string::npos);
    r = bytes({});
    CHECK(r.reason == StopReason::Error);
    r = bytes({0xE9, 0x00, 0x10, 0x00, 0x00});  // jmp fuera del código cargado
    CHECK(r.reason == StopReason::Error);
    std::cout << "    (jmp fuera del código: " << r.message << ")\n";
    CHECK(r.message.find("no mapeada") != std::string::npos || r.message.find("salió") != std::string::npos);
    auto a = assembleIntel("mov eax,\n", kCodeBase);
    CHECK(!a.ok);
}

TEST(archivo_bin) {
    std::vector<uint8_t> code;
    std::string err;
    CHECK(readBinaryFile(std::string(EXAMPLES_DIR) + "/suma.bin", code, err));
    CHECK_EQ(code.size(), size_t(9));
    RunResult r = bytes(code);
    CHECK_EQ(e32(r, RAX), 10u);
    CHECK(!readBinaryFile("no_existe_12345.bin", code, err));
    // Un .bin generado por el propio programa con Keystone.
    auto a = assembleIntel(prepareSourceForAssembler(readExample("04_while.asm")), kCodeBase);
    CHECK(a.ok);
    CHECK(writeBinaryFile("prueba_tmp.bin", a.bytes, err));
    CHECK(readBinaryFile("prueba_tmp.bin", code, err));
    CHECK_EQ(e32(bytes(code), RAX), 3u);
    std::remove("prueba_tmp.bin");
    std::ofstream("vacio_tmp.bin", std::ios::binary).close();
    CHECK(!readBinaryFile("vacio_tmp.bin", code, err));
    std::remove("vacio_tmp.bin");
}

// ---------------------------------------------------------------- consola y CLI

TEST(lectura_por_consola) {
    std::istringstream in("mov eax, edi\nadd eax, esi\nret\nFIN\nlo que sigue no se lee\n");
    std::ostringstream out;
    std::string src = readAsmFromStream(in, out, true);
    CHECK_EQ(src, std::string("mov eax, edi\nadd eax, esi\nret\n"));
    // Menú completo: opción 1, motor 3 (ambos), valores iniciales, programa, FIN, salir.
    std::istringstream menu("1\n3\nedi=7 esi=4\nmov eax, edi\nadd eax, esi\nret\nFIN\n0\n");
    std::ostringstream mo;
    Settings s;
    interactiveMenu(menu, mo, s);
    std::string o = mo.str();
    CHECK(o.find("RAX = 0x000000000000000B") != std::string::npos);
    CHECK(o.find("COINCIDEN") != std::string::npos);
    CHECK(o.find("Hasta luego") != std::string::npos);
}

TEST(menu_archivos_hex_bin_y_errores) {
    std::string asmPath = std::string(EXAMPLES_DIR) + "/01_suma.asm";
    std::string binPath = std::string(EXAMPLES_DIR) + "/suma.bin";
    std::istringstream menu("2\n" + asmPath + "\n1\n\n"            // .asm, educativo
                            "4\nB8 07 00 00 00 83 C0 03 C3\n\n"   // hex
                            "4\n\"" + binPath + "\"\n\n"         // .bin (con comillas)
                            "4\nB8 0\n"                           // hex mal formado
                            "2\nno_existe.asm\n"                  // archivo inexistente
                            "5\n20\ns\n"                          // ajustes
                            "1\n1\n\ninicio:\njmp inicio\nFIN\n"  // ciclo infinito
                            "9\n0\n");
    std::ostringstream mo;
    Settings s;
    interactiveMenu(menu, mo, s);
    std::string o = mo.str();
    CHECK(o.find("RAX = 0x000000000000000B") != std::string::npos);  // 01_suma.asm
    size_t tens = 0;
    for (size_t p = o.find("RAX = 0x000000000000000A"); p != std::string::npos;
         p = o.find("RAX = 0x000000000000000A", p + 1))
        ++tens;
    CHECK_EQ(tens, size_t(2));  // hex y .bin
    CHECK(o.find("impar") != std::string::npos);
    CHECK(o.find("no se pudo abrir") != std::string::npos);
    CHECK_EQ(s.run.maxInstructions, 20ULL);
    CHECK(s.trace.compact);
    CHECK(o.find("límite de 20 instrucciones") != std::string::npos);
    CHECK(o.find("Opción no válida") != std::string::npos);
}

TEST(linea_de_comandos) {
    std::istringstream in;
    std::ostringstream out;
    std::string ex = std::string(EXAMPLES_DIR);
    CHECK_EQ(runCommandLine({"--asm", ex + "/01_suma.asm"}, in, out), 0);
    CHECK_EQ(runCommandLine({"--asm", ex + "/02_mayor.asm", "--init", "edi=2 esi=9", "--motor", "ambos"}, in, out), 0);
    CHECK_EQ(runCommandLine({"--hex", "B8 07 00 00 00 83 C0 03 C3"}, in, out), 0);
    CHECK_EQ(runCommandLine({"--bin", ex + "/suma.bin", "--compacto"}, in, out), 0);
    CHECK_EQ(runCommandLine({"--asm", ex + "/12_ciclo_infinito.asm", "--max", "30"}, in, out), 1);
    CHECK_EQ(runCommandLine({"--asm", "no_existe.asm"}, in, out), 1);
    CHECK_EQ(runCommandLine({"--opcion-rara"}, in, out), 2);
    CHECK_EQ(runCommandLine({"--motor", "x", "--asm", "a"}, in, out), 2);
    CHECK_EQ(runCommandLine({"--ejemplo", "4", "--motor", "ambos"}, in, out), 0);
    std::istringstream stdinProg("mov eax, 3\nimul eax, eax\nret\n");
    std::ostringstream o2;
    CHECK_EQ(runCommandLine({"--asm", "-", "--motor", "real"}, stdinProg, o2), 0);
    CHECK(o2.str().find("RAX = 0x0000000000000009") != std::string::npos);
    std::ostringstream o3;
    CHECK_EQ(runCommandLine({"--asm", ex + "/01_suma.asm", "--init", "edi=x"}, in, o3), 1);
    CHECK(o3.str().find("valores iniciales") != std::string::npos);
}

// ---------------------------------------------------------------- regresiones de la revisión

TEST(regresiones_revision) {
    // "0Bh" es hexadecimal (11), no un binario con prefijo 0b.
    CHECK_EQ(e32(edu("mov eax, 0Bh\nret\n"), RAX), 11u);
    CHECK_EQ(e32(real("mov eax, 0Bh\nret\n"), RAX), 11u);
    CHECK_EQ(e32(edu("mov eax, 0b11\nret\n"), RAX), 3u);
    // --max debe ser un entero positivo; y sólo una entrada a la vez.
    std::istringstream in;
    std::ostringstream out;
    std::string ex = std::string(EXAMPLES_DIR) + "/01_suma.asm";
    CHECK_EQ(runCommandLine({"--asm", ex, "--max", "-5"}, in, out), 2);
    CHECK_EQ(runCommandLine({"--asm", ex, "--max", "0"}, in, out), 2);
    CHECK_EQ(runCommandLine({"--asm", ex, "--hex", "C3"}, in, out), 2);
    // Código automodificable: escribir en la región de código es un error claro
    // (antes Unicorn repetía la instrucción hasta agotar el límite).
    RunResult r = real("mov byte ptr [rip+1], 0x90\nnop\nnop\nret\n");
    CHECK(r.reason == StopReason::Error);
    CHECK(r.message.find("protegida") != std::string::npos);
    CHECK_EQ(r.executed, 0ULL);
    CHECK_EQ(registerName(*lookupRegister("al")), std::string("al"));
    CHECK_EQ(registerName(*lookupRegister("r8l")), std::string("r8b"));
    // El número de ejemplo debe ser exacto ("2abc" no es el ejemplo 2).
    CHECK_EQ(runCommandLine({"--ejemplo", "2abc"}, in, out), 2);
    std::istringstream menu("3\n2abc\n0\n");
    std::ostringstream mo;
    Settings s;
    interactiveMenu(menu, mo, s);
    CHECK(mo.str().find("Ejemplo no válido") != std::string::npos);
    // Rutas con tildes y ñ (UTF-8, como llegan desde la consola o los argumentos).
    const std::string ruta = "prueba_Máquinas_ñ_tmp.bin";
    std::string err;
    CHECK(writeBinaryFile(ruta, {0xB8, 0x07, 0x00, 0x00, 0x00, 0x83, 0xC0, 0x03, 0xC3}, err));
    std::vector<uint8_t> code;
    CHECK(readBinaryFile(ruta, code, err));
    CHECK_EQ(e32(bytes(code), RAX), 10u);
    std::string texto;
    CHECK(readTextFile(ruta, texto, err));
    CHECK_EQ(texto.size(), size_t(9));
    std::filesystem::remove(std::filesystem::u8path(ruta));
    // Sentencias que dejaban a Keystone 0.9.2 en un ciclo infinito: ahora son un error claro.
    for (const char* src : {"[rax]\nret\n", "\\ nop\n", ", 3\n", "-1\n", "etiqueta::\nret\n",
                            "inicio: (eax)\n", "~\n", "|\n", "mov eax, 1\n  + 2\n"}) {
        AssembleResult a = assembleIntel(src, kCodeBase);
        CHECK(!a.ok);
        CHECK(a.error.find("sentencia no válida") != std::string::npos);
    }
    CHECK(assembleIntel(".zzz -1\nret\n", kCodeBase).error.find("directiva") != std::string::npos);
    CHECK(assembleIntel(prepareSourceForAssembler(".text\nglobal f\nf: mov eax, 1\nret\n"), kCodeBase).ok);
    // Las etiquetas en la misma línea que la instrucción siguen siendo válidas.
    CHECK(assembleIntel("a: b: mov eax, 1\n.c: ret\n", kCodeBase).ok);
    // AH..DH con REX: Keystone terminaba el proceso entero ("LLVM ERROR").
    for (const char* src : {"mov ah, sil\n", "add r8b, bh\n", "mov ah, byte ptr [r8]\n",
                            "mov byte ptr [r9], ah\n", "movzx rax, ah\n", "test dh, dil\n",
                            "xchg ah, sil\n"}) {
        AssembleResult a = assembleIntel(src, kCodeBase);
        CHECK(!a.ok);
        CHECK(a.error.find("no codificable") != std::string::npos);
    }
    CHECK(assembleIntel("mov ah, byte ptr [rax]\nmovzx eax, ah\nmov ah, al\nret\n", kCodeBase).ok);
    std::ostringstream o4;
    CHECK(!runAssembly("mov ah, sil\nret\n", "", Engine::Both, Settings{}, o4));
    CHECK(o4.str().find("no codificable") != std::string::npos);
}

// ---------------------------------------------------------------- diferencial aleatorio

// Genera programas aleatorios del subconjunto educativo y exige que el
// intérprete propio y Unicorn (CPU x86-64 emulada) lleguen al mismo estado.
TEST(diferencial_aleatorio_educativo_vs_unicorn) {
    std::mt19937_64 rng(20261028);  // semilla fija: resultados reproducibles
    auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };
    const std::vector<std::string> regs[4] = {
        {"al", "bl", "cl", "dl", "ah", "bh", "ch", "dh", "sil", "dil", "bpl", "r8b", "r11b", "r15b"},
        {"ax", "bx", "cx", "dx", "si", "di", "bp", "r8w", "r12w", "r15w"},
        {"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "r8d", "r9d", "r13d", "r15d"},
        {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "r8", "r10", "r14", "r15"}};
    const char* binops[] = {"mov", "add", "sub", "cmp", "xor", "and", "or", "test", "imul"};
    const char* unops[] = {"inc", "dec", "neg", "not"};
    const char* jccs[] = {"je", "jne", "jg", "jge", "jl", "jle", "ja", "jae",
                          "jb", "jbe", "jc", "js", "jns", "jo", "jno"};

    // Inmediato válido para el ancho (64 bits: imm32 con signo, salvo MOV).
    auto immFor = [&](int w, bool imm64) -> std::string {
        int64_t lo, hi;
        if (w == 64 && !imm64) { lo = INT32_MIN; hi = INT32_MAX; }
        else if (w == 64) { lo = INT64_MIN; hi = INT64_MAX; }
        else { lo = -(int64_t(1) << (w - 1)); hi = (int64_t(1) << (w - 1)) - 1; }
        int64_t special[] = {0, 1, -1, lo, hi, 2, -2, 0x7F, 0x80};
        // El rango de 64 bits completo tiene 2^64 valores: (hi - lo) + 1 desbordaría a 0.
        uint64_t span = static_cast<uint64_t>(hi) - static_cast<uint64_t>(lo);
        uint64_t off = span == UINT64_MAX ? rng() : rng() % (span + 1);
        int64_t v = pick(3) == 0 ? special[pick(9)]
                                 : static_cast<int64_t>(static_cast<uint64_t>(lo) + off);
        if (v < lo || v > hi) v = 1;
        if (pick(2)) return std::to_string(v);  // decimal (prueba la reescritura para Keystone)
        std::ostringstream h;
        h << (v < 0 ? "-0x" : "0x") << std::hex << (v < 0 ? 0 - static_cast<uint64_t>(v) : static_cast<uint64_t>(v));
        return h.str();
    };

    // EMU_FUZZ_N permite una pasada más larga, p. ej. EMU_FUZZ_N=10000.
    int total = 600;
    if (const char* n = std::getenv("EMU_FUZZ_N")) total = std::max(600, std::atoi(n));
    int generated = 0, skipped = 0, failures = 0;
    for (int prog = 0; prog < total && failures < 5; ++prog) {
        std::string src;
        int pendingLabel = -1, labelId = 0;
        int len = 3 + static_cast<int>(pick(12));
        for (int k = 0; k < len; ++k) {
            int wi = static_cast<int>(pick(4));
            int w = 8 << wi;
            const auto& rs = regs[wi];
            std::string d = rs[pick(rs.size())];
            std::string line;
            if (pick(5) == 0) {
                line = std::string(unops[pick(4)]) + " " + d;
            } else {
                std::string op = binops[pick(9)];
                if (op == "imul" && w == 8) op = "add";
                bool useImm = pick(2) == 0;
                if (op == "imul" && pick(3) == 0) {
                    line = "imul " + d + ", " + rs[pick(rs.size())] + ", " + immFor(w, false);
                } else {
                    line = op + " " + d + ", " + (useImm ? immFor(w, op == "mov") : rs[pick(rs.size())]);
                }
            }
            src += "    " + line + "\n";
            if (pendingLabel >= 0) {
                src += "s" + std::to_string(pendingLabel) + ":\n";
                pendingLabel = -1;
            } else if (pick(3) == 0) {
                pendingLabel = labelId++;
                src += "    " + std::string(jccs[pick(15)]) + " s" + std::to_string(pendingLabel) + "\n";
            }
        }
        if (pendingLabel >= 0) src += "s" + std::to_string(pendingLabel) + ":\n";
        if (pick(4) == 0) src += "    push rax\n    pop rbx\n";
        src += "    ret\n";

        std::string init;
        for (const char* r : {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "r8", "r9", "r10",
                              "r11", "r12", "r13", "r14", "r15"}) {
            std::ostringstream h;
            h << r << "=0x" << std::hex << (pick(4) == 0 ? pick(16) : rng()) << " ";
            init += h.str();
        }
        try {
            parseProgram(src);  // descarta combinaciones inválidas (p. ej. AH con R8B)
        } catch (const AsmError&) {
            ++skipped;
            continue;
        }
        ++generated;
        RunResult a = edu(src, init), b = real(src, init);
        auto diffs = compareResults(a, b);
        if (!a.ok() || !b.ok() || !diffs.empty()) {
            ++failures;
            std::cout << "    Programa " << prog << " (init " << init << "):\n" << src;
            if (!b.ok()) std::cout << "    Unicorn: " << b.message << "\n";
            for (const auto& d : diffs) std::cout << "    diferencia: " << d << "\n";
        }
    }
    std::cout << "    " << generated << " programas aleatorios comparados (" << skipped
              << " descartados por combinaciones no codificables)\n";
    CHECK_EQ(failures, 0);
    CHECK(generated >= 400);
}

TEST(ejecutable_de_extremo_a_extremo) {
    // Lanza el .exe real con la entrada por tubería, como lo haría un usuario.
    std::string exe = EMULADOR_EXE;
    {
        std::ofstream f("entrada_tmp.txt");
        f << "4\nB8 07 00 00 00 83 C0 03 C3\n\n0\n";
    }
#ifdef _WIN32
    std::string cmd = "\"\"" + exe + "\" < entrada_tmp.txt > salida_tmp.txt\"";
#else
    std::string cmd = "\"" + exe + "\" < entrada_tmp.txt > salida_tmp.txt";
#endif
    int rc = std::system(cmd.c_str());
    CHECK_EQ(rc, 0);
    std::string out, err;
    readTextFile("salida_tmp.txt", out, err);
    CHECK(out.find("RAX = 0x000000000000000A") != std::string::npos);
    CHECK(out.find("Paso 3") != std::string::npos);
    std::remove("entrada_tmp.txt");
    std::remove("salida_tmp.txt");
}

}  // namespace

int main() {
    std::cout << "Bibliotecas: " << libraryVersions() << "\n";
    if (!unicornAvailable() || !capstoneAvailable() || !keystoneAvailable()) {
        std::cout << "AVISO: faltan bibliotecas del motor real; las pruebas que las usan fallarán.\n";
    }
    for (auto& [name, fn] : registry()) {
        g_current = name;
        int before = g_failures;
        try {
            fn();
        } catch (const std::exception& e) {
            report(false, std::string("excepción no esperada: ") + e.what(), __FILE__, __LINE__);
        }
        std::cout << (g_failures == before ? "[ OK ]   " : "[FALLO]  ") << name << "\n";
    }
    std::cout << "\n" << g_checks << " comprobaciones, " << g_failures << " fallos, "
              << registry().size() << " pruebas.\n";
    return g_failures == 0 ? 0 : 1;
}
