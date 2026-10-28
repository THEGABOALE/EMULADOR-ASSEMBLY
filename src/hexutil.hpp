// Lectura de bytes de código máquina: texto hexadecimal y archivos binarios crudos.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace emu {

// Acepta "B8 07 00 00 00", "B8,07", "0xB8 0x07", "\xB8\x07" o "B807000000".
bool parseHexBytes(const std::string& text, std::vector<uint8_t>& out, std::string& error);

// Lee un archivo .bin sin cabeceras (no es un ELF ni un PE: sólo bytes).
bool readBinaryFile(const std::string& path, std::vector<uint8_t>& out, std::string& error);
bool writeBinaryFile(const std::string& path, const std::vector<uint8_t>& data, std::string& error);

bool readTextFile(const std::string& path, std::string& out, std::string& error);

}  // namespace emu
