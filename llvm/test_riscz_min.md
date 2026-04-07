# Minimal RISCZ llc object generation example

Input IR:

```llvm
define dso_local i32 @main() {
entry:
  ret i32 12
}
```

Generate the object:

```bash
llc test_riscz_min.ll -march=riscz --filetype=obj -o test_riscz_min.o
```

`llvm-readobj --file-headers --sections --symbols test_riscz_min.o`:

```text
File: test_riscz_min.o
Format: elf64-unknown
Arch: riscz
AddressSize: 64bit
...
Type: Relocatable (0x1)
Machine: EM_RISCZ (0x105)
...
Section {
  Index: 2
  Name: .text
  Type: SHT_PROGBITS (0x1)
  Size: 8
  AddressAlignment: 4
}
...
Symbol {
  Name: main
  Value: 0x0
  Size: 8
  Binding: Global (0x1)
  Type: Function (0x2)
  Section: .text (0x2)
}
```

`llvm-readelf -h -S -s test_riscz_min.o`:

```text
ELF Header:
  Type:                              REL (Relocatable file)
  Machine:                           RISC-Z
  Start of section headers:          208 (bytes into file)
  Number of section headers:         5

Section Headers:
  [ 1] .strtab           STRTAB
  [ 2] .text             PROGBITS        ... Size 000008 ... AX
  [ 3] .note.GNU-stack   PROGBITS
  [ 4] .symtab           SYMTAB

Symbol table '.symtab' contains 3 entries:
     1: 0000000000000000     0 FILE    LOCAL  DEFAULT   ABS test_riscz_min.ll
     2: 0000000000000000     8 FUNC    GLOBAL DEFAULT     2 main
```

Minimal hex check:

```text
00000040: 1305c000 67800000
```

Interpreted as:

```text
0x00c00513 -> addi x10, x0, 12
0x00008067 -> jalr x0, x1, 0
```
