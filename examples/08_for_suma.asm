; FOR (sesión 2): int total = 0; for (int i=1; i<=4; i++) total += i; return total;
; Resultado esperado: EAX = 10
    xor eax, eax   ; total=0
    mov ecx, 1     ; i=1
inicio:
    cmp ecx, 4
    jg fin
    add eax, ecx
    add ecx, 1
    jmp inicio
fin:
    ret
