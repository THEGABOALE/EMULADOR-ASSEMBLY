; Ejercicio 3 (sesión 2): ciclo con la comparación al final.
; mov eax, 0 / mov ecx, 1 / inicio: add eax, ecx / add ecx, 1 / cmp ecx, 5 / jl inicio / ret
; Resultado esperado: EAX = 1 + 2 + 3 + 4 = 10, ECX = 5
    mov eax, 0
    mov ecx, 1
inicio:
    add eax, ecx
    add ecx, 1
    cmp ecx, 5
    jl inicio
    ret
