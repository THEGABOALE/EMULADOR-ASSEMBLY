; Ejemplo 2 (sesión 2): int mayor(int a,int b) { if (a > b) return a; return b; }
; Pruebe con --init "edi=8 esi=5", "edi=2 esi=9" y "edi=5 esi=5" -> EAX = 8, 9, 5
; init: edi=8 esi=5
mayor:
    mov eax, edi
    cmp edi, esi
    jg fin          ; JG: salto si es mayor CON signo (ZF=0 y SF=OF)
    mov eax, esi
fin:
    ret
