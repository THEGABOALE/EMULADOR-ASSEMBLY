// Ejemplos de las presentaciones de clase, listos para ejecutar desde el menú.
#pragma once

#include <string>
#include <vector>

namespace emu {

struct ClassExample {
    std::string title;
    std::string description;
    std::string source;   // incluye un comentario "; init:" con valores sugeridos
};

const std::vector<ClassExample>& classExamples();

}  // namespace emu
