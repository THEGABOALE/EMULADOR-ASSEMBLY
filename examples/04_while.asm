; Ejemplo 4 (sesión 2): WHILE  int x = 0; while (x < 3) x++; return x;
; Resultado esperado: EAX = 3 (4 comparaciones y 3 sumas)
contar:
    xor eax, eax
inicio:
    cmp eax, 3
    jge fin         ; JGE: salto si es mayor o igual con signo (SF=OF)
    add eax, 1
    jmp inicio
fin:
    ret
