; Ciclo sin salida: el emulador se detiene al alcanzar el límite de instrucciones
; (por defecto 1000; puede cambiarse con --max N).
    mov eax, 0
inicio:
    add eax, 1
    jmp inicio
