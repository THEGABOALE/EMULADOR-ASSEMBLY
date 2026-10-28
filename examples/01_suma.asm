; Ejemplo 1 (sesión 1): int sumar(int a, int b) { return a + b; }
; Convención System V AMD64: a en EDI, b en ESI, retorno en EAX.
; init: edi=7 esi=4
; Resultado esperado: EAX = 11
sumar:
    mov eax, edi  ; eax = a
    add eax, esi  ; eax = a + b
    ret           ; devuelve eax
