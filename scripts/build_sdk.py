#!/usr/bin/env python3
"""Publish kernel sources' interfaces and native-language ABI wrapper objects."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GROUPS = {"io": list(range(16)) + [20, 21], "memory": list(range(16, 20)),
          "fs": list(range(22, 27)) + [44, 45, 46],
          "thread": [27, 28, 29, 30, 31, 32, 33, 37, 38, 39, 40],
          "env": [34, 35, 36, 41, 42, 43, 47, 48, 49]}
TYPES = {"SZ_INT": "int", "SZ_CHAR": "char", "SZ_VOID": "void", "SZ_INT_PTR": "int*",
         "SZ_CHAR_PTR": "char*", "SZ_VOID_PTR": "void*"}

def main():
    directory = ROOT / "build/sdk"
    headers = directory / "include/systemzero"; headers.mkdir(parents=True, exist_ok=True)
    libraries = directory / "lib"; libraries.mkdir(parents=True, exist_ok=True)
    services = {}
    for line in (ROOT / "shared/sys_api.def").read_text().splitlines():
        match = re.match(r"SZ_SERVICE\((.*)\)", line)
        if match:
            items = [item.strip() for item in match[1].split(',')]
            number, name, result, argc = int(items[0]), items[1], items[2], int(items[3])
            services[number] = name, TYPES[result], [TYPES[t] for t in items[4:4+argc]]
    objects = []
    for group, numbers in GROUPS.items():
        assembly = ["BITS 32", "section .text", "section .note.GNU-stack noalloc noexec nowrite progbits", "section .text"]
        declarations = [f"/* SystemZero {group} API: link {group}.o or system.a. */"]
        for number in numbers:
            if number not in services: continue
            name, result, arguments = services[number]
            symbol = "sz_" + name
            declarations.append(f"{result} {symbol}({', '.join(arguments)});")
            assembly += [f"global {symbol}", f"{symbol}:", "push ebp", "mov ebp,esp", "and esp,-16", "sub esp,32", "mov [esp],esi", "xor eax,eax"]
            for i in range(1, 6): assembly.append(f"mov [esp+{i*4}],eax")
            for i in range(len(arguments)):
                assembly += [f"mov eax,[ebp+{8+i*4}]", f"mov [esp+{4+i*4}],eax"]
            assembly += [f"call [edi+{12+number*4}]", "mov esp,ebp", "pop ebp", "ret"]
        (headers / f"{group}.h").write_text('\n'.join(declarations) + '\n')
        source = libraries / f"{group}.asm"; source.write_text('\n'.join(assembly) + '\n')
        obj = libraries / f"{group}.o"
        subprocess.run(["nasm", "-f", "elf32", str(source), "-o", str(obj)], check=True)
        objects.append(obj)
    (headers / "system.h").write_text(''.join(f'#include <systemzero/{group}.h>\n' for group in GROUPS))
    subprocess.run(["ar", "rcs", str(libraries / "system.a"), *map(str, objects)], check=True)
    (directory / "stamp").touch()

if __name__ == '__main__': main()
