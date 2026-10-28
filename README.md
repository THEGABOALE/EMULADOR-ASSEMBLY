# Emulador x86-64

Emulador de assembly x86-64 para consola. Le das un programa (`MOV`, `ADD`, `CMP`, `JC`,
`JGE`, `JMP`, etc.) y después de **cada instrucción** te muestra cómo quedaron los
registros y las banderas `ZF`, `SF`, `CF` y `OF`.

## Qué tiene

- **Dos formas de ejecutar el mismo programa:**
  - **Intérprete propio:** lee el assembly y ejecuta cada instrucción con código nuestro.
  - **CPU emulada con Unicorn:** Keystone convierte el assembly en código máquina real,
    Unicorn lo ejecuta y Capstone lo desensambla para mostrarlo.
  - Se pueden correr **los dos y comparar** que den lo mismo.
- **Varias formas de cargar el programa:** escribirlo en la consola, abrir un `.asm`,
  elegir un ejemplo de la clase o pasar código máquina (bytes en hex o un archivo `.bin`).
- **Traza paso a paso:** instrucción, registros después de ejecutarla (marca con `*` lo que
  cambió), banderas, siguiente `RIP` y si el salto se tomó o no. Al final, el estado final.
- **Colores en la consola:** registros que cambian en amarillo, salto tomado en verde, no
  tomado y errores en rojo. Si la salida se guarda en un archivo, sale sin colores.
- **Valores iniciales** de registros, por ejemplo `edi=7 esi=4`.
- **Límite de instrucciones** (1000 por defecto) para que un ciclo infinito no cuelgue el programa.

### Instrucciones del intérprete propio

| Instrucción | Notas |
|---|---|
| `MOV` | no cambia banderas |
| `ADD`, `SUB`, `CMP` | `CMP` resta sin guardar el resultado |
| `IMUL` | `imul eax, ebx` · `imul eax, 5` · `imul eax, ebx, 5` |
| `XOR`, `AND`, `OR`, `TEST` | ponen `CF` y `OF` en 0 |
| `INC`, `DEC` | no tocan `CF` |
| `NEG`, `NOT` | |
| `PUSH`, `POP`, `CALL`, `RET`, `NOP` | |
| `JMP` y saltos condicionales | `JE/JZ`, `JNE/JNZ`, `JG`, `JGE`, `JL`, `JLE`, `JA`, `JAE/JNC`, `JB/JC`, `JBE`, `JS`, `JNS`, `JO`, `JNO` |

Registros: `RAX` a `R15` con sus partes de 32, 16 y 8 bits (`EAX`, `AX`, `AL`, `AH`, `R8D`…).
Escribir en `EAX` limpia la mitad alta de `RAX`; escribir en `AX` o `AL` no.

El intérprete propio no usa memoria (`[rbx+8]`), `LEA`, `SHL`, `DIV`, etc.: si aparecen
lo dice con un error claro. El motor con Unicorn sí acepta cualquier instrucción que
Keystone pueda ensamblar.

## Cómo funciona

- **Intérprete propio:** primero revisa todo el programa (etiquetas, operandos, tamaños) y
  avisa los errores con su número de línea. Después ejecuta instrucción por instrucción
  calculando las banderas según el tamaño del operando. Aquí `RIP` es el número de
  instrucción (`#0`, `#1`…), no una dirección de memoria.
- **CPU emulada con Unicorn:** el código se carga en `0x400000` y la pila en `0x7FF00000`.
  En la pila se deja una dirección de retorno especial (`0xDEAD0000`): cuando el `RET` llega
  ahí, el programa terminó. Unicorn ejecuta **una instrucción a la vez** y después de cada
  una se leen los registros. El código nunca corre en tu computadora real, solo dentro de
  la CPU emulada.
- **Comparar:** corre los dos motores y revisa que coincidan los 16 registros, las
  banderas, el número de instrucciones y la forma de terminar.

## Cómo correrlo (Windows)

**1. Instalar MSYS2**, que trae el compilador y las bibliotecas:

```powershell
winget install --id MSYS2.MSYS2 -e
```

**2. Instalar el compilador, CMake, Unicorn, Capstone y Keystone:**

```powershell
C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-unicorn mingw-w64-ucrt-x86_64-capstone mingw-w64-ucrt-x86_64-keystone"
```

**3. Compilar** desde la carpeta del proyecto:

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S . -B build -G Ninja
cmake --build build
```

Las DLL necesarias se copian junto a `build\emulador.exe`, así que después se puede abrir
sin configurar nada más.

**4. Ejecutar:**

```powershell
.\build\emulador.exe
```

```
========== EMULADOR x86-64 ==========
  1) Escribir un programa
  2) Abrir un archivo .asm
  3) Ejemplos de la clase
  4) Código máquina: bytes o archivo .bin
  5) Ajustes
  0) Salir
```

Al escribir un programa se termina con una línea `FIN`. Luego se elige el motor
(1 intérprete propio, 2 Unicorn, 3 comparar los dos) y, si se quiere, valores iniciales.

También se puede usar directo desde la línea de comandos:

```powershell
.\build\emulador.exe --asm examples\01_suma.asm
.\build\emulador.exe --asm examples\02_mayor.asm --init "edi=2 esi=9" --motor ambos
.\build\emulador.exe --ejemplo 4
.\build\emulador.exe --hex "B8 07 00 00 00 83 C0 03 C3"
.\build\emulador.exe --bin examples\suma.bin
.\build\emulador.exe --ayuda
```

Otras opciones: `--motor edu|real|ambos`, `--max N` (límite de instrucciones),
`--compacto` (solo muestra lo que cambia), `--sin-color` y `--ensamblar A.asm --salida A.bin`.

### Formato de un `.asm`

```asm
; init: edi=8 esi=5     <- valores iniciales (opcional)
mayor:
    mov eax, edi
    cmp edi, esi
    jg fin              ; salta si edi > esi (con signo)
    mov eax, esi
fin:
    ret
```

Una instrucción por línea, destino primero, comentarios con `;`. Números en decimal
(`10`, `-3`), hex (`0x1F` o `1Fh`) o binario (`0b101`).

En `examples/` hay ejemplos de la clase (suma, mayor, if/else, while, for, acarreo con
`JC`, `JG` frente a `JA`, valor absoluto, etc.) con su resultado esperado en el comentario.

## Pruebas

```powershell
ctest --test-dir build --output-on-failure
.\build\pruebas.exe          # con el detalle de cada prueba
```

Son 34 pruebas con 607 comprobaciones. Revisan:

- banderas de cada instrucción en 8, 16, 32 y 64 bits, y los saltos condicionales;
- todos los ejemplos de la clase con distintos valores iniciales, en los dos motores;
- que los dos motores den exactamente el mismo resultado;
- la traza: estado después de cada paso, salto tomado o no, `RET` final;
- errores: sintaxis, etiquetas, bytes incompletos, memoria no válida, ciclos infinitos;
- el menú, la línea de comandos, los colores, rutas con tildes o ñ, y el `.exe` real de
  principio a fin.

Además, una **prueba aleatoria** genera cientos de programas al azar y exige que el
intérprete propio y Unicorn lleguen al mismo resultado. Para una pasada más larga:

```powershell
$env:EMU_FUZZ_N = 10000; .\build\pruebas.exe; Remove-Item Env:EMU_FUZZ_N
```

Resultado actual:

```
607 comprobaciones, 0 fallos, 34 pruebas.
100% tests passed out of 1
```

Con 10 000 programas aleatorios: 8153 comparados y 0 diferencias.

Cosas que encontraron las pruebas y ya están resueltas: Keystone 0.9.2 a veces lee
`mov eax, 255` como `0x255`, se cuelga con líneas que empiezan con un signo (`[rax]`,
`, 3`) y cierra el programa con `mov ah, sil`. El emulador evita las tres situaciones y
muestra un error claro.
