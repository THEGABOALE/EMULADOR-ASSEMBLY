; Vistas de un mismo registro físico: RAX / EAX / AX / AL / AH.
; Escribir EAX pone a cero los 32 bits superiores de RAX; escribir AX o AL no.
; Resultado esperado: RAX = 0xFFFFFFFFFFFF1256, RBX = 0x0000000000000005,
;                     RCX = 0x000000000000AB00
    mov rax, -1          ; RAX = 0xFFFFFFFFFFFFFFFF
    mov ax, 0x1234       ; RAX = 0xFFFFFFFFFFFF1234
    mov al, 0x56         ; RAX = 0xFFFFFFFFFFFF1256
    mov rbx, -1
    mov ebx, 5           ; RBX = 0x0000000000000005
    xor ecx, ecx
    mov ch, 0xAB         ; RCX = 0x000000000000AB00
    ret
