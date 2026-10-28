// Emulador x86-64: punto de entrada.
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "trace.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

// argv llega en la página de códigos ANSI; se toma la línea de comandos en
// UTF-16 y se convierte a UTF-8 para que las rutas con cualquier carácter funcionen.
static std::vector<std::string> utf8Arguments() {
    std::vector<std::string> args;
    int n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w) return args;
    for (int i = 1; i < n; ++i) {
        int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(len > 0 ? len : 1, ' ');
        if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
        s.resize(len > 0 ? len - 1 : 0);  // sin el terminador
        args.push_back(s);
    }
    LocalFree(w);
    return args;
}

// Color sólo si la salida es una consola que acepta códigos ANSI.
static bool consoleSupportsColor() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return false;  // archivo o tubería
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}
#else
#include <unistd.h>
static bool consoleSupportsColor() { return isatty(STDOUT_FILENO) != 0; }
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    // Mostrar correctamente tildes y eñes en la consola de Windows.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    (void)argc;
    (void)argv;
    std::vector<std::string> args = utf8Arguments();
#else
    std::vector<std::string> args(argv + 1, argv + argc);
#endif
    emu::setColorEnabled(!std::getenv("NO_COLOR") && consoleSupportsColor());
    return emu::runCommandLine(args, std::cin, std::cout);
}
