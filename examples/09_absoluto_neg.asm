; Ejercicio 2 (sesión 2): int absoluto(int x){ if(x<0) return -x; return x; }
; Pruebe x = -3, 0 y 5 -> EAX = 3, 0, 5
; init: edi=-3
absoluto:
    mov eax, edi
    cmp eax, 0
    jge listo
    neg eax
listo:
    ret
