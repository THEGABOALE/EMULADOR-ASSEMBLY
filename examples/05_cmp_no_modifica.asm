; Ejemplo 5: CMP calcula EAX - EBX sólo para las banderas; EAX no cambia.
; Resultado esperado: EAX = 10, EBX = 15, ECX = 0
mov eax, 10
mov ebx, 15
cmp eax, ebx
jge mayor
mov ecx, 0
jmp fin
mayor:
mov ecx, 1
fin:
ret
