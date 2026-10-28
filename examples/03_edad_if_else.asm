; Ejemplo 3 (sesión 2): IF/ELSE  int acceso(int edad) { return edad >= 18 ? 1 : 0; }
; Pruebe edades 17, 18 y 25 -> EAX = 0, 1, 1
; init: edi=17
acceso:
    cmp edi, 18
    jl menor        ; JL: salto si es menor con signo (SF<>OF)
    mov eax, 1
    ret
menor:
    xor eax, eax    ; EAX = 0 (también pone CF=0 y OF=0)
    ret
