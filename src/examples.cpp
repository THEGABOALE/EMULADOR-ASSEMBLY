#include "examples.hpp"

namespace emu {

const std::vector<ClassExample>& classExamples() {
    static const std::vector<ClassExample> ex = {
        {"Suma de dos enteros", "int sumar(int a, int b) { return a + b; }  -> EAX = 11",
         "; init: edi=7 esi=4\n"
         "sumar:\n"
         "    mov eax, edi  ; eax = a\n"
         "    add eax, esi  ; eax = a + b\n"
         "    ret           ; devuelve eax\n"},
        {"Mayor de dos enteros con JG", "if (a > b) return a; return b;  -> con (8,5) EAX = 8",
         "; init: edi=8 esi=5\n"
         "mayor:\n"
         "    mov eax, edi\n"
         "    cmp edi, esi\n"
         "    jg fin\n"
         "    mov eax, esi\n"
         "fin:\n"
         "    ret\n"},
        {"IF/ELSE: clasificar edad con JL", "edad >= 18 ? 1 : 0  -> con 17 EAX = 0",
         "; init: edi=17\n"
         "acceso:\n"
         "    cmp edi, 18\n"
         "    jl menor\n"
         "    mov eax, 1\n"
         "    ret\n"
         "menor:\n"
         "    xor eax, eax\n"
         "    ret\n"},
        {"WHILE: contador hasta 3 con JGE", "while (x < 3) x++;  -> EAX = 3",
         "contar:\n"
         "    xor eax, eax\n"
         "inicio:\n"
         "    cmp eax, 3\n"
         "    jge fin\n"
         "    add eax, 1\n"
         "    jmp inicio\n"
         "fin:\n"
         "    ret\n"},
        {"CMP no modifica el destino", "EAX=10, EBX=15, ECX=0 tras comparar",
         "mov eax, 10\n"
         "mov ebx, 15\n"
         "cmp eax, ebx\n"
         "jge mayor\n"
         "mov ecx, 0\n"
         "jmp fin\n"
         "mayor:\n"
         "mov ecx, 1\n"
         "fin:\n"
         "ret\n"},
        {"Acarreo: ADD con CF=1 y JC", "0xFFFFFFFF + 1 produce CF=1; JC salta -> EBX = 1",
         "    mov eax, 0xFFFFFFFF\n"
         "    add eax, 1        ; EAX = 0, CF = 1, ZF = 1\n"
         "    jc hubo_acarreo\n"
         "    mov ebx, 0\n"
         "    ret\n"
         "hubo_acarreo:\n"
         "    mov ebx, 1\n"
         "    ret\n"},
        {"JG con signo frente a JA sin signo", "a=-1, b=1: JG no salta, JA sí -> ECX=0, EDX=1",
         "; init: edi=-1 esi=1\n"
         "    xor ecx, ecx\n"
         "    xor edx, edx\n"
         "    cmp edi, esi\n"
         "    jg con_signo     ; -1 > 1 es falso con signo\n"
         "    jmp prueba_ja\n"
         "con_signo:\n"
         "    mov ecx, 1\n"
         "prueba_ja:\n"
         "    cmp edi, esi\n"
         "    ja sin_signo     ; 0xFFFFFFFF > 1 es verdadero sin signo\n"
         "    ret\n"
         "sin_signo:\n"
         "    mov edx, 1\n"
         "    ret\n"},
        {"FOR: sumar de 1 a 4", "for (i=1; i<=4; i++) total += i;  -> EAX = 10",
         "    xor eax, eax   ; total=0\n"
         "    mov ecx, 1     ; i=1\n"
         "inicio:\n"
         "    cmp ecx, 4\n"
         "    jg fin\n"
         "    add eax, ecx\n"
         "    add ecx, 1\n"
         "    jmp inicio\n"
         "fin:\n"
         "    ret\n"},
        {"Valor absoluto con NEG", "if (x < 0) return -x; return x;  -> con -3 EAX = 3",
         "; init: edi=-3\n"
         "absoluto:\n"
         "    mov eax, edi\n"
         "    cmp eax, 0\n"
         "    jge listo\n"
         "    neg eax\n"
         "listo:\n"
         "    ret\n"},
        {"IMUL y SUB", "return a*b - 1;  -> con (4,3) EAX = 11",
         "; init: edi=4 esi=3\n"
         "    mov eax, edi\n"
         "    imul eax, esi\n"
         "    sub eax, 1\n"
         "    ret\n"},
        {"Vistas RAX/EAX/AX/AL", "Escribir EAX limpia los 32 bits altos; escribir AX no",
         "    mov rax, -1          ; RAX = 0xFFFFFFFFFFFFFFFF\n"
         "    mov ax, 0x1234       ; sólo cambian los 16 bits bajos\n"
         "    mov al, 0x56         ; sólo cambian los 8 bits bajos\n"
         "    mov rbx, -1\n"
         "    mov ebx, 5           ; RBX = 0x0000000000000005\n"
         "    ret\n"},
    };
    return ex;
}

}  // namespace emu
