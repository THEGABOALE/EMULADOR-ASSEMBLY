; Ejemplo 6b: ADD sin acarreo (CF = 0): JC NO se toma.
; Resultado esperado: EAX = 6, EBX = 0
    mov eax, 5
    add eax, 1
    jc hubo_acarreo
    mov ebx, 0
    ret
hubo_acarreo:
    mov ebx, 1
    ret
