#include "hexutil.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace emu {

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// Las rutas llegan en UTF-8 (consola y argumentos); en Windows un std::string
// se interpretaría con la página de códigos ANSI y fallarían rutas con tildes o ñ.
std::filesystem::path toPath(const std::string& utf8) { return std::filesystem::u8path(utf8); }

}  // namespace

bool parseHexBytes(const std::string& text, std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    // Sustituir separadores y quitar prefijos 0x y \x.
    std::string clean;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if ((c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) ||
            (c == '\\' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X'))) {
            clean += ' ';
            ++i;
            continue;
        }
        if (c == ',' || c == ';' || c == '-' || std::isspace(static_cast<unsigned char>(c))) {
            clean += ' ';
            continue;
        }
        if (hexValue(c) < 0) {
            error = "carácter no hexadecimal '" + std::string(1, c) + "' en la posición " +
                    std::to_string(i + 1);
            return false;
        }
        clean += c;
    }
    std::istringstream in(clean);
    std::string tok;
    while (in >> tok) {
        if (tok.size() % 2 != 0) {
            error = "el grupo '" + tok + "' tiene un número impar de dígitos hexadecimales "
                    "(cada byte son 2 dígitos)";
            return false;
        }
        for (size_t i = 0; i < tok.size(); i += 2)
            out.push_back(static_cast<uint8_t>(hexValue(tok[i]) * 16 + hexValue(tok[i + 1])));
    }
    if (out.empty()) {
        error = "no se introdujo ningún byte";
        return false;
    }
    return true;
}

bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out, std::string& error) {
    std::ifstream f(toPath(path), std::ios::binary);
    if (!f) {
        error = "no se pudo abrir el archivo '" + path + "'";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (out.empty()) {
        error = "el archivo '" + path + "' está vacío";
        return false;
    }
    return true;
}

bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data, std::string& error) {
    std::ofstream f(toPath(path), std::ios::binary);
    if (!f) {
        error = "no se pudo crear el archivo '" + path + "'";
        return false;
    }
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

bool readTextFile(const std::string& path, std::string& out, std::string& error) {
    std::ifstream f(toPath(path), std::ios::binary);
    if (!f) {
        error = "no se pudo abrir el archivo '" + path + "'";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    // Quitar BOM UTF-8 si existe.
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF)
        out.erase(0, 3);
    return true;
}

}  // namespace emu
