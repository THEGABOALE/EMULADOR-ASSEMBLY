#include "cli.hpp"

#include <algorithm>
#include <cctype>

#include "examples.hpp"
#include "hexutil.hpp"
#include "interpreter.hpp"
#include "machine.hpp"
#include "parser.hpp"

namespace emu {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Quita comillas que Windows añade al arrastrar un archivo a la consola.
std::string unquote(std::string s) {
    s = trim(s);
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front())
        s = s.substr(1, s.size() - 2);
    return s;
}

// Entero positivo sólo con dígitos (std::stoull aceptaría "-5" dando un valor enorme).
bool parsePositive(const std::string& s, uint64_t& v) {
    if (s.empty() || s.size() > 18 ||
        !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); }))
        return false;
    v = std::stoull(s);
    return v > 0;
}

bool ask(std::istream& in, std::ostream& out, const std::string& prompt, std::string& line) {
    out << prompt << std::flush;
    if (!std::getline(in, line)) return false;
    line = trim(line);
    return true;
}

bool parseInit(const std::string& source, const std::string& initText,
               std::vector<InitAssign>& init, std::ostream& out) {
    try {
        init = parseInitComments(source);
        auto extra = parseInitList(initText);
        init.insert(init.end(), extra.begin(), extra.end());
    } catch (const AsmError& e) {
        out << paint::red << "Error en los valores iniciales: " << e.what() << "\n" << paint::reset;
        return false;
    }
    if (!init.empty()) {
        out << "Valores iniciales:";
        for (const auto& a : init) out << " " << a.text;
        out << "\n";
    }
    return true;
}

void printListing(const std::vector<uint8_t>& code, std::ostream& out) {
    std::string err;
    auto lines = disassemble(code, kCodeBase, err);
    out << "Desensamblado (Capstone), cargado en " << hex64(kCodeBase) << ":\n";
    for (const auto& l : lines) {
        std::string b = formatBytes(l.bytes);
        out << "  " << hex64(l.address) << ":  " << b << std::string(b.size() < 24 ? 24 - b.size() : 1, ' ')
            << l.text << "\n";
    }
    if (!err.empty()) out << "  (" << err << ")\n";
}

Engine askEngine(std::istream& in, std::ostream& out) {
    out << "Motor: 1) Intérprete propio   2) CPU emulada con Unicorn   3) Comparar los dos\n";
    std::string line;
    if (!ask(in, out, "Elija [1]: ", line)) return Engine::Educational;
    if (line == "2") return Engine::Real;
    if (line == "3") return Engine::Both;
    return Engine::Educational;
}

std::string askInit(std::istream& in, std::ostream& out) {
    std::string line;
    ask(in, out, "Valores iniciales, ej. edi=7 esi=4 [Enter = ninguno]: ", line);
    return line;
}

void printHelp(std::ostream& out) {
    out << "Uso:\n"
           "  emulador                         Abre el menú interactivo\n"
           "  emulador --asm ARCHIVO.asm       Ejecuta un archivo .asm ('-' = entrada estándar)\n"
           "  emulador --hex \"B8 07 00 00 00 83 C0 03 C3\"   Ejecuta bytes de código máquina\n"
           "  emulador --bin ARCHIVO.bin       Ejecuta un archivo binario crudo\n"
           "  emulador --ejemplo N             Ejecuta el ejemplo de clase N (ver --listar-ejemplos)\n"
           "  emulador --ensamblar A.asm --salida A.bin   Genera un .bin con Keystone\n"
           "Opciones:\n"
           "  --motor edu|real|ambos   Motor para --asm/--ejemplo (por defecto: edu)\n"
           "  --init \"edi=7 esi=4\"     Valores iniciales de registros\n"
           "  --max N                  Límite de instrucciones (por defecto 1000)\n"
           "  --compacto               Traza compacta: sólo registros que cambian\n"
           "  --sin-color              Salida sin colores\n"
           "  --version                Versiones de las bibliotecas\n";
}

}  // namespace

bool runAssembly(const std::string& source, const std::string& initText, Engine engine,
                 const Settings& settings, std::ostream& out) {
    std::vector<InitAssign> init;
    if (!parseInit(source, initText, init, out)) return false;

    bool ok = true;
    RunResult edu, real;
    bool haveEdu = false, haveReal = false;

    if (engine != Engine::Real) {
        try {
            Program prog = parseProgram(source);
            for (const auto& w : prog.warnings) out << "Aviso: " << w << "\n";
            edu = runEducational(prog, init, settings.run);
            printRun(out, edu, settings.trace);
            haveEdu = true;
            ok = ok && edu.ok();
        } catch (const AsmError& e) {
            out << paint::red << "Error en el intérprete propio: " << e.what() << "\n" << paint::reset;
            ok = false;
        }
    }

    if (engine != Engine::Educational) {
        if (!keystoneAvailable() || !unicornAvailable() || !capstoneAvailable()) {
            out << "El motor real no está disponible en esta compilación (" << libraryVersions()
                << ")\n";
            return false;
        }
        AssembleResult a = assembleIntel(prepareSourceForAssembler(source), kCodeBase);
        if (!a.ok) {
            out << paint::red << "Error al ensamblar: " << a.error << "\n" << paint::reset;
            return false;
        }
        out << "\nKeystone ensambló " << a.statements << " sentencias en " << a.bytes.size()
            << " bytes: " << formatBytes(a.bytes) << "\n";
        printListing(a.bytes, out);
        real = runMachineCode(a.bytes, init, settings.run);
        printRun(out, real, settings.trace);
        haveReal = true;
        ok = ok && real.ok();
    }

    if (engine == Engine::Both && haveEdu && haveReal) {
        auto diffs = compareResults(edu, real);
        out << "\nComparación intérprete propio vs Unicorn: ";
        if (diffs.empty()) {
            out << paint::green << "COINCIDEN: mismos registros, banderas, número de instrucciones y final.\n" << paint::reset;
        } else {
            out << paint::red << "DIFIEREN:\n" << paint::reset;
            for (const auto& d : diffs) out << "  - " << d << "\n";
            ok = false;
        }
    }
    return ok;
}

bool runBytes(const std::vector<uint8_t>& code, const std::string& initText,
              const Settings& settings, std::ostream& out) {
    std::vector<InitAssign> init;
    if (!parseInit("", initText, init, out)) return false;
    if (!unicornAvailable() || !capstoneAvailable()) {
        out << "El backend real no está disponible en esta compilación (" << libraryVersions()
            << ")\n";
        return false;
    }
    out << "Bytes cargados (" << code.size() << "): " << formatBytes(code) << "\n";
    printListing(code, out);
    RunResult r = runMachineCode(code, init, settings.run);
    printRun(out, r, settings.trace);
    return r.ok();
}

std::string readAsmFromStream(std::istream& in, std::ostream& out, bool prompt) {
    if (prompt) {
        out << "Escriba el programa, una instrucción por línea. FIN para ejecutar.\n";
    }
    std::string text, line;
    int n = 1;
    while (true) {
        if (prompt) out << n << "> " << std::flush;
        if (!std::getline(in, line)) break;
        if (lower(trim(line)) == "fin") break;
        text += line + "\n";
        ++n;
    }
    return text;
}

void interactiveMenu(std::istream& in, std::ostream& out, Settings& s) {
    while (true) {
        out << "\n" << paint::bold << paint::cyan << "========== EMULADOR x86-64 ==========" << paint::reset << "\n"
            << "  1) Escribir un programa\n"
            << "  2) Abrir un archivo .asm\n"
            << "  3) Ejemplos de la clase\n"
            << "  4) Código máquina: bytes o archivo .bin\n"
            << "  5) Ajustes\n"
            << "  0) Salir\n";
        std::string opt;
        if (!ask(in, out, "Opción: ", opt)) return;

        if (opt == "0") {
            out << "Hasta luego.\n";
            return;
        } else if (opt == "1") {
            Engine e = askEngine(in, out);
            std::string init = askInit(in, out);
            std::string src = readAsmFromStream(in, out, true);
            if (trim(src).empty()) {
                out << "No se introdujo ninguna instrucción.\n";
                continue;
            }
            runAssembly(src, init, e, s, out);
        } else if (opt == "2") {
            std::string path;
            if (!ask(in, out, "Ruta del archivo: ", path)) return;
            std::string src, err;
            if (!readTextFile(unquote(path), src, err)) {
                out << paint::red << "Error: " << err << "\n" << paint::reset;
                continue;
            }
            Engine e = askEngine(in, out);
            std::string init = askInit(in, out);
            runAssembly(src, init, e, s, out);
        } else if (opt == "3") {
            const auto& ex = classExamples();
            for (size_t i = 0; i < ex.size(); ++i)
                out << "  " << (i + 1) << ") " << ex[i].title << "\n";
            std::string sel;
            if (!ask(in, out, "Ejemplo: ", sel)) return;
            uint64_t k = 0;
            if (!parsePositive(sel, k) || k > ex.size()) {
                out << "Ejemplo no válido.\n";
                continue;
            }
            out << "\n" << ex[k - 1].source << "\n";
            Engine e = askEngine(in, out);
            std::string init = askInit(in, out);
            runAssembly(ex[k - 1].source, init, e, s, out);
        } else if (opt == "4") {
            std::string text, err;
            if (!ask(in, out, "Bytes en hex (ej. B8 07 00 00 00 C3) o ruta de un .bin: ", text))
                return;
            // Una ruta existente o terminada en .bin es un archivo; lo demás, bytes en hex.
            std::string path = unquote(text);
            std::vector<uint8_t> code;
            bool isFile = lower(path).size() > 4 && lower(path).substr(path.size() - 4) == ".bin";
            if (!isFile) {
                std::vector<uint8_t> probe;
                isFile = readBinaryFile(path, probe, err);
            }
            bool ok = isFile ? readBinaryFile(path, code, err) : parseHexBytes(text, code, err);
            if (!ok) {
                out << paint::red << "Error: " << err << "\n" << paint::reset;
                continue;
            }
            std::string init = askInit(in, out);
            runBytes(code, init, s, out);
        } else if (opt == "5") {
            std::string line;
            if (!ask(in, out, "Máximo de instrucciones [" + std::to_string(s.run.maxInstructions) + "]: ", line))
                return;
            uint64_t v;
            if (!line.empty()) {
                if (parsePositive(line, v))
                    s.run.maxInstructions = v;
                else
                    out << "Valor no válido; se mantiene " << s.run.maxInstructions << ".\n";
            }
            if (!ask(in, out, "¿Mostrar sólo los registros que cambian? s/n [n]: ", line))
                return;
            s.trace.compact = lower(line) == "s" || lower(line) == "si" || lower(line) == "sí";
        } else {
            out << "Opción no válida.\n";
        }
    }
}

int runCommandLine(const std::vector<std::string>& argsIn, std::istream& in, std::ostream& out) {
    Settings s;
    std::vector<std::string> args;
    for (const auto& a : argsIn) {
        if (a == "--sin-color")
            setColorEnabled(false);  // válido también con el menú
        else
            args.push_back(a);
    }
    if (args.empty()) {
        interactiveMenu(in, out, s);
        return 0;
    }
    std::string asmPath, hex, binPath, initText, example, assemblePath, outputPath;
    bool listExamples = false;
    Engine engine = Engine::Educational;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& dst) {
            if (i + 1 >= args.size()) {
                out << paint::red << "Error: falta el valor de " << a << "\n" << paint::reset;
                return false;
            }
            dst = args[++i];
            return true;
        };
        std::string v;
        if (a == "--ayuda" || a == "--help" || a == "-h") {
            printHelp(out);
            return 0;
        } else if (a == "--version") {
            out << "Emulador x86-64 1.0 | " << libraryVersions() << "\n";
            return 0;
        } else if (a == "--asm") {
            if (!value(asmPath)) return 2;
        } else if (a == "--hex") {
            if (!value(hex)) return 2;
        } else if (a == "--bin") {
            if (!value(binPath)) return 2;
        } else if (a == "--init") {
            if (!value(initText)) return 2;
        } else if (a == "--ejemplo") {
            if (!value(example)) return 2;
        } else if (a == "--listar-ejemplos") {
            listExamples = true;
        } else if (a == "--ensamblar") {
            if (!value(assemblePath)) return 2;
        } else if (a == "--salida") {
            if (!value(outputPath)) return 2;
        } else if (a == "--compacto") {
            s.trace.compact = true;
        } else if (a == "--max") {
            if (!value(v)) return 2;
            if (!parsePositive(v, s.run.maxInstructions)) {
                out << paint::red << "Error: --max necesita un número entero mayor que 0\n" << paint::reset;
                return 2;
            }
        } else if (a == "--motor") {
            if (!value(v)) return 2;
            if (v == "edu" || v == "educativo") engine = Engine::Educational;
            else if (v == "real") engine = Engine::Real;
            else if (v == "ambos") engine = Engine::Both;
            else {
                out << paint::red << "Error: motor desconocido '" << v << "' (use edu, real o ambos)\n" << paint::reset;
                return 2;
            }
        } else {
            out << paint::red << "Error: opción desconocida '" << a << "'\n" << paint::reset;
            printHelp(out);
            return 2;
        }
    }

    int sources = !asmPath.empty() + !hex.empty() + !binPath.empty() + !example.empty() +
                  !assemblePath.empty();
    if (sources > 1) {
        out << paint::red << "Error: indique sólo una entrada (--asm, --hex, --bin, --ejemplo o --ensamblar)\n" << paint::reset;
        return 2;
    }

    std::string err;
    if (listExamples) {
        const auto& ex = classExamples();
        for (size_t i = 0; i < ex.size(); ++i)
            out << (i + 1) << ") " << ex[i].title << " - " << ex[i].description << "\n";
        return 0;
    }
    if (!assemblePath.empty()) {
        std::string src;
        if (!readTextFile(assemblePath, src, err)) {
            out << paint::red << "Error: " << err << "\n" << paint::reset;
            return 1;
        }
        AssembleResult r = assembleIntel(prepareSourceForAssembler(src), kCodeBase);
        if (!r.ok) {
            out << paint::red << "Error al ensamblar: " << r.error << "\n" << paint::reset;
            return 1;
        }
        out << "Bytes (" << r.bytes.size() << "): " << formatBytes(r.bytes) << "\n";
        if (!outputPath.empty()) {
            if (!writeBinaryFile(outputPath, r.bytes, err)) {
                out << paint::red << "Error: " << err << "\n" << paint::reset;
                return 1;
            }
            out << "Escrito en " << outputPath << "\n";
        }
        return 0;
    }
    if (!example.empty()) {
        const auto& ex = classExamples();
        uint64_t k = 0;
        if (!parsePositive(example, k) || k > ex.size()) {
            out << paint::red << "Error: ejemplo no válido (1.." << ex.size() << ")\n" << paint::reset;
            return 2;
        }
        out << ex[k - 1].title << "\n" << ex[k - 1].source;
        return runAssembly(ex[k - 1].source, initText, engine, s, out) ? 0 : 1;
    }
    if (!asmPath.empty()) {
        std::string src;
        if (asmPath == "-") {
            src = readAsmFromStream(in, out, false);
        } else if (!readTextFile(asmPath, src, err)) {
            out << paint::red << "Error: " << err << "\n" << paint::reset;
            return 1;
        }
        return runAssembly(src, initText, engine, s, out) ? 0 : 1;
    }
    if (!hex.empty() || !binPath.empty()) {
        std::vector<uint8_t> code;
        bool ok = !hex.empty() ? parseHexBytes(hex, code, err) : readBinaryFile(binPath, code, err);
        if (!ok) {
            out << paint::red << "Error: " << err << "\n" << paint::reset;
            return 1;
        }
        return runBytes(code, initText, s, out) ? 0 : 1;
    }
    out << paint::red << "Error: no se indicó qué ejecutar\n" << paint::reset;
    printHelp(out);
    return 2;
}

}  // namespace emu
