; Ejercicio 4 (sesión 2): JG (con signo) y JA (sin signo) NO son equivalentes.
; a = -1 (0xFFFFFFFF), b = 1
;   JG: -1 > 1 con signo -> falso -> ECX = 0
;   JA: 0xFFFFFFFF > 1 sin signo -> verdadero -> EDX = 1
; init: edi=-1 esi=1
    xor ecx, ecx
    xor edx, edx
    cmp edi, esi
    jg con_signo
    jmp prueba_ja
con_signo:
    mov ecx, 1
prueba_ja:
    cmp edi, esi
    ja sin_signo
    ret
sin_signo:
    mov edx, 1
    ret
