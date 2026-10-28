; Ejemplo 6a: ADD que produce acarreo sin signo (CF = 1) y JC lo detecta.
; 0xFFFFFFFF + 1 = 0x1_0000_0000 -> EAX = 0, CF = 1, ZF = 1
; Resultado esperado: EBX = 1 (JC tomado)
    mov eax, 0xFFFFFFFF
    add eax, 1
    jc hubo_acarreo
    mov ebx, 0
    ret
hubo_acarreo:
    mov ebx, 1
    ret
