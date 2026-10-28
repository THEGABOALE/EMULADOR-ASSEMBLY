; Ejercicio 3 (sesión 1): mov eax, edi / imul eax, esi / sub eax, 1 / ret
; Equivale a: int f(int a, int b) { return a * b - 1; }   Con a=4, b=3 -> EAX = 11
; init: edi=4 esi=3
    mov eax, edi
    imul eax, esi
    sub eax, 1
    ret
