#include "cpu.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace emu {

const std::array<int, NUM_GPR> kDisplayOrder = {
    RAX, RBX, RCX, RDX, RSI, RDI, RBP, RSP,
    R8, R9, R10, R11, R12, R13, R14, R15};

namespace {

std::map<std::string, RegRef> buildTable() {
    std::map<std::string, RegRef> t;
    const char* n64[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"};
    const char* n32[] = {"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    const char* n16[] = {"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
    const char* n8[] = {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil"};
    const char* nh[] = {"ah", "ch", "dh", "bh"};
    for (int i = 0; i < 8; ++i) {
        t[n64[i]] = {i, 64, false, false};
        t[n32[i]] = {i, 32, false, false};
        t[n16[i]] = {i, 16, false, false};
        t[n8[i]] = {i, 8, false, i >= 4};  // spl, bpl, sil, dil necesitan REX
    }
    for (int i = 0; i < 4; ++i) t[nh[i]] = {i, 8, true, false};
    for (int i = 8; i < 16; ++i) {
        std::string base = "r" + std::to_string(i);
        t[base] = {i, 64, false, true};
        t[base + "d"] = {i, 32, false, true};
        t[base + "w"] = {i, 16, false, true};
        t[base + "b"] = {i, 8, false, true};
        t[base + "l"] = {i, 8, false, true};  // alias usado por algunos ensambladores
    }
    return t;
}

const std::map<std::string, RegRef>& table() {
    static const std::map<std::string, RegRef> t = buildTable();
    return t;
}

}  // namespace

std::optional<RegRef> lookupRegister(const std::string& name) {
    std::string low = name;
    std::transform(low.begin(), low.end(), low.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto it = table().find(low);
    if (it == table().end()) return std::nullopt;
    return it->second;
}

const char* gprName64(int index) {
    static const char* names[] = {"RAX", "RCX", "RDX", "RBX", "RSP", "RBP", "RSI", "RDI",
                                  "R8",  "R9",  "R10", "R11", "R12", "R13", "R14", "R15"};
    return names[index];
}

std::string registerName(const RegRef& r) {
    for (const auto& [name, ref] : table()) {
        bool aliasR8l = name[0] == 'r' && name.back() == 'l';  // preferir r8b sobre r8l
        if (ref.index == r.index && ref.width == r.width && ref.high8 == r.high8 && !aliasR8l)
            return name;
    }
    return "?";
}

uint64_t widthMask(int width) {
    return width >= 64 ? ~0ULL : ((1ULL << width) - 1);
}

uint64_t readReg(const CpuState& s, const RegRef& r) {
    uint64_t full = s.gpr[r.index];
    if (r.high8) return (full >> 8) & 0xFF;
    return full & widthMask(r.width);
}

void writeReg(CpuState& s, const RegRef& r, uint64_t value) {
    uint64_t& full = s.gpr[r.index];
    switch (r.width) {
        case 64:
            full = value;
            break;
        case 32:
            // Regla de x86-64: el resultado de 32 bits se extiende con ceros a 64.
            full = value & 0xFFFFFFFFULL;
            break;
        case 16:
            full = (full & ~0xFFFFULL) | (value & 0xFFFF);
            break;
        case 8:
            if (r.high8)
                full = (full & ~0xFF00ULL) | ((value & 0xFF) << 8);
            else
                full = (full & ~0xFFULL) | (value & 0xFF);
            break;
    }
}

CpuState makeInitialState(const std::vector<InitAssign>& init, uint64_t rip) {
    CpuState s;
    s.rip = rip;
    s.gpr[RSP] = kInitialRsp;
    for (const auto& a : init) writeReg(s, a.reg, a.value);
    return s;
}

}  // namespace emu
