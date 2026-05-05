#ifndef TREE_TO_BIN_H_
#define TREE_TO_BIN_H_

#include <stdint.h>
#include "Common/Structs.h"

#define ELF_BASE 0x400000u
#define PAGE_SIZE 0x1000u
#define ELF_HEADER_SIZE 64
#define PHDR_SIZE 56
#define NUM_PHDRS 2
#define RAM_SIZE 65536
#define HDRS_TOTAL (ELF_HEADER_SIZE + NUM_PHDRS * PHDR_SIZE)

#define MAX_LABELS 4096
#define MAX_RELOCS 8192
#define CALLEE_SIZE 24
#define DEFAULT_SIZE 128
#define DEFAULT_LABEL_SIZE 64

typedef struct {
    uint8_t *data;
    size_t size, capacity;
} Buf;

typedef struct {
    size_t got_off;
    size_t plt_printf;
    size_t plt_printf_char;
    size_t plt_scanf;
    size_t plt_exit;
    size_t plt_draw;
} PltGot;

typedef struct {
    uint32_t printf_off;
    uint32_t scanf_off;
    uint32_t exit_off;
    uint32_t draw_off;
    uint32_t code_size;
    uint32_t reloc_count;
} LibHeader;

typedef struct {
    char name[DEFAULT_SIZE];
    size_t offset;
} Label;

enum RelocType {
    kRel32,
    kAbs64,
    kGOTRel32,
};

typedef struct {
    size_t offset;
    char name[DEFAULT_SIZE];
    RelocType type;
} Relocation;

typedef struct {
    Buf code;
    Buf data;

    Label labels[MAX_LABELS];
    int number_labels;
    Relocation relocs[MAX_RELOCS];
    int number_relocs;

    size_t fmt_int_off, fmt_char_off, ram_off;

    size_t b_printf, b_printf_char, b_scanf, b_exit, b_draw, b_start;
} Context;

typedef struct {
    int ram_base;
    int param_count;
    int frame_size;
} Sub;

struct RegInfo {
    const char *name;
    int code;
    int bits;
};

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t printf_off;
    uint32_t scanf_off;
    uint32_t exit_off;
    uint32_t draw_off;
    uint32_t reloc_count;
    uint32_t *relocs;
} LibBlob;

enum Regs {
    kRAX = 0,
    kRCX = 1,
    kRDX = 2,
    kRBX = 3,
    kRSP = 4,
    kRBP = 5,
    kRSI = 6,
    kRDI = 7,
    kR8  = 8,
    kR9  = 9,
    kR10 = 10,
    kR11 = 11,
    kR12 = 12,
    kR13 = 13,
    kR14 = 14,
    kR15 = 15,

    kEAX = 0,
    kECX = 1,
    kEDX = 2,
    kEBX = 3,
    kESP = 4,
    kEBP = 5,
    kESI = 6,
    kEDI = 7,
    kR8D = 8,
    kR9D = 9,
    kR10D = 10,
    kR11D = 11,
    kR12D = 12,
    kR13D = 13,
    kR14D = 14,
    kR15D = 15,

    kXMM0 = 0,
    kXMM1 = 1
};

void CompileTreeToELF(LangNode_t *root, VariableArr *arr, const char *elf_path);

#endif // TREE_TO_BIN_H_