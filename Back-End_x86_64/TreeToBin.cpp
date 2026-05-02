#include "Back-End/TreeToBin.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/CommonFunctions.h"
#include "Common/CommonBackFunctions.h"

#define ELF_BASE 0x400000u
#define PAGE_SIZE 0x1000u // TODO: переделать 
#define ELF_HEADER_SIZE 64
#define PHDR_SIZE 56
#define NUM_PHDRS 2
#define RAM_SIZE 65536
#define HDRS_TOTAL (ELF_HEADER_SIZE + NUM_PHDRS * PHDR_SIZE)

#define MAX_LABELS 4096
#define MAX_RELOCS 8192
#define CALLEE_SIZE 24
#define DEFAULT_SIZE 128

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
} PltGot;

typedef struct {
    uint32_t printf_off;
    uint32_t scanf_off;
    uint32_t exit_off;
    uint32_t code_size;
    uint32_t reloc_count;
} LibHeader;

typedef struct {
    uint8_t  *data;
    size_t size;
    uint32_t printf_off;
    uint32_t scanf_off;
    uint32_t exit_off;
    uint32_t reloc_count;
    uint32_t *relocs;
} LibBlob;

typedef struct {
    char name[DEFAULT_SIZE];
    size_t offset;
} Label;

typedef struct {
    size_t offset;
    char name[DEFAULT_SIZE];
    int type;
} Relocation;

typedef struct {
    Buf code;
    Buf data;

    Label labels[MAX_LABELS];
    int number_labels;
    Relocation relocs[MAX_RELOCS];
    int number_relocs;

    size_t fmt_int_off, fmt_char_off, ram_off;

    size_t b_printf, b_printf_char, b_scanf, b_exit, b_start;
} Context;

typedef struct {
    int ram_base;
    int param_count;
} Sub;

struct RegInfo {
    const char *name;
    int code;
    int bits;
};

#define REGS_NUMBER 35

static const struct RegInfo regs[REGS_NUMBER] = {
    {"rax",   0, 64}, {"rcx",   1, 64}, {"rdx",   2, 64}, {"rbx",   3, 64},
    {"rsp",   4, 64}, {"rbp",   5, 64}, {"rsi",   6, 64}, {"rdi",   7, 64},
    {"r8",    8, 64}, {"r9",    9, 64}, {"r10",  10, 64}, {"r11",  11, 64},
    {"r12",  12, 64}, {"r13",  13, 64}, {"r14",  14, 64}, {"r15",  15, 64},
    {"eax",   0, 32}, {"ecx",   1, 32}, {"edx",   2, 32}, {"ebx",   3, 32},
    {"esp",   4, 32}, {"ebp",   5, 32}, {"esi",   6, 32}, {"edi",   7, 32},
    {"r8d",   8, 32}, {"r9d",   9, 32}, {"r10d", 10, 32}, {"r11d", 11, 32},
    {"r12d", 12, 32}, {"r13d", 13, 32}, {"r14d", 14, 32}, {"r15d", 15, 32},
    {"xmm0",  0, 128}, {"xmm1",  1, 128},
    {NULL,    0, 0}
};

static void BufInit(Buf *buf, size_t capacity) {
    assert(buf);

    buf->data = (uint8_t *) calloc (1, capacity);
    if (!buf->data) {
        perror("Error calloc.\n");
        return;
    }

    buf->size = 0;
    buf->capacity = capacity;
}

static void BufFree(Buf *buf) {
    assert(buf);

    free(buf->data);
}

static void BufGrow(Buf *buf, size_t need) {
    assert(buf);
    if (buf->size + need <= buf->capacity) return;

    while (buf->size + need > buf->capacity) {
        buf->capacity *= 2;
    }

    uint8_t *ptr = (uint8_t *) realloc (buf->data, buf->capacity);
    if (!ptr) {
        perror("Error realloc.\n");
        return;
    }

    buf->data = ptr;
}

static void Emit8(Buf *buf, uint8_t value) {
    assert(buf);

    BufGrow(buf, 1);
    buf->data[buf->size++] = value;
}

static void Emit32(Buf *buf, uint32_t value) {
    assert(buf);

    BufGrow(buf, 4);
    memcpy(buf->data + buf->size, &value, 4);
    buf->size += 4;
}

static void Emit64(Buf *buf, uint64_t value) {
    assert(buf);

    BufGrow(buf, 8);
    memcpy(buf->data + buf->size, &value, 8);
    buf->size += 8;
}

static void Patch32(Buf *buf, size_t offset, uint32_t value) {
    assert(buf);

    memcpy(buf->data + offset, &value, 4);
}

static void Patch64(Buf *buf, size_t offset, uint64_t value) {
    assert(buf);

    memcpy(buf->data + offset, &value, 8);
}

static void ContextInit(Context *context) {
    assert(context);

    memset(context, 0, sizeof(*context));
    BufInit(&context->code, 131072);
    BufInit(&context->data, RAM_SIZE * 8 + 512);
}

static void ContextFree(Context *context) {
    assert(context);

    BufFree(&context->code);
    BufFree(&context->data);
}

static void LabelAdd(Context *context, const char *name, size_t offset) {
    assert(context);
    assert(name);
    assert(context->number_labels < MAX_LABELS);

    strncpy(context->labels[context->number_labels].name, name, DEFAULT_SIZE - 1);
    context->labels[context->number_labels].offset = offset;
    context->number_labels++;
}

static int LabelFind(Context *context, const char *name) {
    assert(context);
    assert(name);

    for (int i = 0; i < context->number_labels; i++) {
        if (strcmp(context->labels[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

static void RelocAdd(Context *context, size_t offset, const char *name, int type) {
    assert(context);
    assert(name);
    assert(context->number_relocs < MAX_RELOCS);

    context->relocs[context->number_relocs].offset = offset;
    context->relocs[context->number_relocs].type = type;
    const char *new_name = (name[0] == ':') ? name + 1 : name;
    strncpy(context->relocs[context->number_relocs].name, new_name, DEFAULT_SIZE - 1);
    context->number_relocs++;
}

static int FindRegCode(const char *reg_name) {
    assert(reg_name);

    for (int i = 0; i < REGS_NUMBER; i++) {
        if (strncmp(reg_name, regs[i].name, strlen(reg_name)) == 0) {
            return regs[i].code;
        }
    }

    return -1;
}

static uint8_t ModRM(int mod, int reg, int rm) {
    return (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

static uint8_t Sib(int scale, int index, int base) {
    return (uint8_t)((scale << 6) | ((index & 7) << 3) | (base & 7));
}

static uint8_t RexW(int reg, int rm) {
    uint8_t result = 0x48;

    if (reg >= 8) result |= 0x04;
    if (rm  >= 8) result |= 0x01;

    return result;
}

#define CD (&context->code)

//- mov rREG, imm64 -
static void EmitMovR64Imm64(Context *context, int reg, int64_t value) {
    assert(context);

    uint8_t rex = 0x48;
    if (reg >= 8) rex |= 0x01;

    Emit8(CD, rex);
    Emit8(CD, (uint8_t)(0xB8 + (reg & 7)));
    Emit64(CD, (uint64_t)value);
}

//- push rREG -
static void EmitPush(Context *context, int reg) {
    assert(context);

    if (reg >= 8) Emit8(CD, 0x41);
    Emit8(CD, (uint8_t)(0x50 + (reg & 7)));
}

//- pop rREG -
static void EmitPop(Context *context, int reg) {
    assert(context);

    if (reg >= 8) Emit8(CD, 0x41);
    Emit8(CD, (uint8_t)(0x58 + (reg & 7)));
}

//- mov rDST, rSRC -
static void EmitMovRR(Context *context, int dst, int src) {
    assert(context);

    Emit8(CD, RexW(src, dst));
    Emit8(CD, 0x89);
    Emit8(CD, ModRM(3, src, dst));
}

//- add rREG, imm  or  sub rREG, |imm| if imm < 0 -
static void EmitAddRegImm(Context *context, int reg, int64_t imm) {
    assert(context);
    if (imm == 0) return;

    uint8_t slash = (imm > 0) ? 0 : 5;
    int64_t abs_value = (imm > 0) ? imm : -imm;

    if (abs_value <= 127) {
        Emit8(CD, RexW(0, reg));
        Emit8(CD, 0x83);
        Emit8(CD, ModRM(3, slash, reg));
        Emit8(CD, (uint8_t)(int8_t)abs_value);
    } else {
        Emit8(CD, RexW(0, reg));
        Emit8(CD, 0x81);
        Emit8(CD, ModRM(3, slash, reg));
        Emit32(CD, (uint32_t)(int32_t)abs_value);
    }
}

//- call <name> -
static void EmitCall(Context *context, const char *name) {
    assert(context);
    assert(name);

    Emit8(CD, 0xE8);
    RelocAdd(context, CD->size, name, 0);
    Emit32(CD, 0);
}

//- jmp rel32 -
static void EmitJmp(Context *context, const char *name) {
    assert(context);
    assert(name);

    Emit8(CD, 0xE9);
    RelocAdd(context, CD->size, name, 0);
    Emit32(CD, 0);
}

//- jcc rel32  (cc — full byte of opcode: 0x84 = je 0x85 = jne ...) -
static void EmitJCC(Context *context, uint8_t cc, const char *name) {
    assert(context);
    assert(name);

    Emit8(CD, 0x0F); Emit8(CD, cc);
    RelocAdd(context, CD->size, name, 0);
    Emit32(CD, 0);
}

//- mov rREG, <abs addr данных>  (type=1 reloc) -
static void EmitMovData(Context *context, int reg, const char *symbol) {
    assert(context);
    assert(symbol);

    EmitMovR64Imm64(context, reg, 0);
    RelocAdd(context, CD->size - 8, symbol, 1);
}

//- and rsp, -16 -
static void EmitAlignStack(Context *context) {
    assert(context);

    Emit8(CD, 0x48);
    Emit8(CD, 0x83);
    Emit8(CD, 0xE4);
    Emit8(CD, 0xF0);
}

//- cmp rax, rbx -
static void EmitCmpRaxRbx(Context *context) {
    assert(context);

    Emit8(CD, RexW(3, 0));
    Emit8(CD, 0x39);
    Emit8(CD, ModRM(3, 3, 0));
}

//- xor eax, eax -
static void EmitXorEax(Context *context) {
    assert(context);

    Emit8(CD, 0x31);
    Emit8(CD, 0xC0);
}

//- ret -
static void EmitRet(Context *context) {
    assert(context);

    Emit8(CD, 0xC3);
}

/* ------------------------------------
 * Count rcx = &ram[r12 + shift]:
 *   mov rcx, <abs ram>
 *   mov rdi, r12
 *   add/sub rdi, shift
 *   lea rcx, [rcx + rdi * 8]
 *  -----------------------------------
 */
static void EmitVarAddr(Context *context, int shift) {
    assert(context);

    EmitMovData(context, FindRegCode("rcx"), "ram");

    //- mov rdi, r12 -
    Emit8(CD, RexW(12, 7));
    Emit8(CD, 0x89);
    Emit8(CD, ModRM(3, 12, 7));

    EmitAddRegImm(context, FindRegCode("rdi"), (int64_t)shift);

    //- lea rcx, [rcx + rdi * 8]   48 8D 0C + SIB(scale = 3, var_idx = rdi, base = rcx) -
    Emit8(CD, 0x48);
    Emit8(CD, 0x8D);
    Emit8(CD, 0x0C);
    Emit8(CD, Sib(3, FindRegCode("rdi"), FindRegCode("rcx")));
}

static void EmitPrologue(Context *context) {
    assert(context);

    Emit8(CD, 0x55);                                    // push rbp
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xE5);  // mov rbp,rsp
    Emit8(CD, 0x41); Emit8(CD, 0x54);                   // push r12
    Emit8(CD, 0x41); Emit8(CD, 0x55);                   // push r13
    Emit8(CD, 0x53);                                    // push rbx
}

static void EmitEpilogue(Context *context) {
    assert(context);

    //- lea rsp, [rbp - CALLEE_SIZE] -
    Emit8(CD, 0x48); Emit8(CD, 0x8D); Emit8(CD, 0x65);
    Emit8(CD, (uint8_t)(int8_t)(-CALLEE_SIZE));
    Emit8(CD, 0x5B);                   // pop rbx
    Emit8(CD, 0x41); Emit8(CD, 0x5D);  // pop r13
    Emit8(CD, 0x41); Emit8(CD, 0x5C);  // pop r12
    Emit8(CD, 0x5D);                   // pop rbp
}

static void EmitStart(Context *context) { // TODO
    assert(context);

    context->b_start = CD->size;

    Emit8(CD, 0x48);
    Emit8(CD, 0x83);
    Emit8(CD, 0xE4);
    Emit8(CD, 0xF0);
    Emit8(CD, 0x48);
    Emit8(CD, 0x83);
    Emit8(CD, 0xEC);
    Emit8(CD, 0x08);
    Emit8(CD, 0xE8);
    RelocAdd(context, CD->size, "main", 0);
    Emit32(CD, 0);
    Emit8(CD, 0x48);
    Emit8(CD, 0x89);
    Emit8(CD, 0xC7);
    Emit8(CD, 0x48);
    Emit8(CD, 0xC7);
    Emit8(CD, 0xC0);
    Emit32(CD, 60);
    Emit8(CD, 0x0F);
    Emit8(CD, 0x05);
}

static void BuildGOT(Context *context, PltGot *plt_got) {
    assert(context);
    assert(plt_got);

    Buf *data = &context->data;

    while (data->size % 8) {
        Emit8(data, 0);
    }

    plt_got->got_off = data->size;

    Emit64(data, 0);
    Emit64(data, 1);
    Emit64(data, 2);
}

static void BuildData(Context *context) {
    assert(context);

    Buf *data = &context->data;

    context->fmt_int_off = data->size;
    Emit8(data, '%'); Emit8(data, 'd');
    Emit8(data, '\n'); Emit8(data, 0);

    context->fmt_char_off = data->size;
    Emit8(data, '%'); Emit8(data, 'c');
    Emit8(data, 0);

    while (data->size % 8) {
        Emit8(data, 0);
    }

    context->ram_off = data->size;
    for (int i = 0; i < RAM_SIZE; i++) {
        Emit64(data, 0);
    }
}

static void EmitPLT(Context *context, PltGot *plt_got) {
    assert(context);
    assert(plt_got);

    plt_got->plt_printf = CD->size;
    LabelAdd(context, "my_printf", plt_got->plt_printf);
    Emit8(CD, 0xFF);
    Emit8(CD, 0x25);
    RelocAdd(context, CD->size, "__got_printf", 2);
    Emit32(CD, 0);

    plt_got->plt_scanf = CD->size;
    LabelAdd(context, "my_scanf", plt_got->plt_scanf);
    Emit8(CD, 0xFF);
    Emit8(CD, 0x25);
    RelocAdd(context, CD->size, "__got_scanf", 2);
    Emit32(CD, 0);

    plt_got->plt_exit = CD->size;
    LabelAdd(context, "my_exit", plt_got->plt_exit);
    Emit8(CD, 0xFF);
    Emit8(CD, 0x25);
    RelocAdd(context, CD->size, "__got_exit", 2);
    Emit32(CD, 0);
}

static int ResolveDataSym(Context *context, const char *name, size_t *out) {
    assert(context);
    assert(name);
    assert(out);

    if (strcmp(name, "fmt_int") == 0) {
        *out = context->fmt_int_off;
        return 1;
    }

    if (strcmp(name, "fmt_char") == 0) {
        *out = context->fmt_char_off;
        return 1;
    }

    if (strcmp(name, "ram") == 0) {
        *out = context->ram_off;
        return 1;
    }

    return 0;
}

static void LinkRelocs(Context *context, PltGot *plt_got, uint64_t data_vaddr) {
    assert(context);
    assert(plt_got);

    for (int i = 0; i < context->number_relocs; i++) {
        Relocation *reloc = &context->relocs[i];

        if (reloc->type == 0) {
            int label_index = LabelFind(context, reloc->name);
            if (label_index < 0) {
                fprintf(stderr, "Undefined label: %s\n", reloc->name);
                continue;
            }

            int32_t rel = (int32_t)((int64_t)context->labels[label_index].offset - (int64_t)(reloc->offset + 4));
            Patch32(&context->code, reloc->offset, (uint32_t)rel);

        } else if (reloc->type == 1) {
            size_t data_offset = 0;
            if (ResolveDataSym(context, reloc->name, &data_offset)) {
                Patch64(&context->code, reloc->offset, data_vaddr + data_offset);
            } else {
                int label_index = LabelFind(context, reloc->name);
                if (label_index >= 0) {
                    Patch64(&context->code, reloc->offset, ELF_BASE + HDRS_TOTAL + context->labels[label_index].offset);
                } else {
                    fprintf(stderr, "Unknown symbol: %s\n", reloc->name);
                }
            }

        } else if (reloc->type == 2) {
            uint64_t got_slot_vaddr = 0;

            if (strcmp(reloc->name, "__got_printf") == 0) {
                got_slot_vaddr = data_vaddr + plt_got->got_off + 0 * 8;
            } else if (strcmp(reloc->name, "__got_scanf") == 0) {
                got_slot_vaddr = data_vaddr + plt_got->got_off + 1 * 8;
            } else if (strcmp(reloc->name, "__got_exit") == 0) {
                got_slot_vaddr = data_vaddr + plt_got->got_off + 2 * 8;
            } else {
                fprintf(stderr, "Unknown GOT symbol: %s\n", reloc->name);
                continue;
            }

            uint64_t rip = ELF_BASE + HDRS_TOTAL + reloc->offset + 4;
            int32_t rel = (int32_t)(got_slot_vaddr - rip);
            Patch32(&context->code, reloc->offset, (uint32_t)rel);
        }
    }
}

static void WriteElf(Context *context, const char *path) {
    assert(context);
    assert(path);

    size_t seg1 = HDRS_TOTAL + context->code.size;
    size_t data_off = (seg1 + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    uint64_t data_vaddr = ELF_BASE + data_off;
    uint64_t entry = ELF_BASE + HDRS_TOTAL + context->b_start;

    FILE *file = fopen(path, "wb");
    if (!file) {
        perror(path);
        return;
    }

    uint8_t header[ELF_HEADER_SIZE] = {};
    header[0] = 0x7F; header[1] = 'E'; header[2] = 'L'; header[3] = 'F';
    header[4] = 2; header[5] = 1; header[6] = 1;

    { uint16_t value = 2;  memcpy(header + 16, &value, 2); }
    { uint16_t value = 62; memcpy(header + 18, &value, 2); }
    { uint32_t value = 1;  memcpy(header + 20, &value, 4); }
    memcpy(header + 24, &entry, 8);

    { uint64_t value = 64; memcpy(header + 32, &value, 8); }
    { uint16_t value = 64; memcpy(header + 52, &value, 2); }
    { uint16_t value = 56; memcpy(header + 54, &value, 2); }
    { uint16_t value = 2;  memcpy(header + 56, &value, 2); }
    fwrite(header, 1, ELF_HEADER_SIZE, file);

    uint8_t ph[PHDR_SIZE] = {};
    { uint32_t value = 1; memcpy(ph, &value, 4); }
    { uint32_t value = 7; memcpy(ph + 4, &value, 4); } // TODO
    { uint64_t value = 0; memcpy(ph + 8, &value, 8); } // TODO
    { uint64_t value = ELF_BASE; memcpy(ph + 16, &value, 8); memcpy(ph + 24, &value, 8); }
    { uint64_t value = seg1; memcpy(ph + 32, &value, 8); memcpy(ph + 40, &value, 8); }
    { uint64_t value = PAGE_SIZE; memcpy(ph + 48, &value, 8); }
    fwrite(ph, 1, PHDR_SIZE, file);

    memset(ph, 0, PHDR_SIZE);
    { uint32_t value = 1; memcpy(ph, &value, 4); }
    { uint32_t value = 6; memcpy(ph + 4, &value, 4); }
    memcpy(ph + 8, &data_off, 8);
    memcpy(ph + 16, &data_vaddr, 8);
    memcpy(ph + 24, &data_vaddr, 8);
    { uint64_t value = context->data.size; memcpy(ph + 32, &value, 8); memcpy(ph + 40, &value, 8); }
    { uint64_t value = PAGE_SIZE; memcpy(ph + 48, &value, 8); }
    fwrite(ph, 1, PHDR_SIZE, file);

    fwrite(context->code.data, 1, context->code.size, file);

    size_t pad = data_off - seg1;
    if (pad > 0) {
        uint8_t *zeroes = (uint8_t *) calloc (1, pad);
        if (!zeroes) {
            perror("Error calloc.\n");
            fclose(file);
            return;
        }

        fwrite(zeroes, 1, pad, file);
        free(zeroes);
    }

    fwrite(context->data.data, 1, context->data.size, file);
    fclose(file);
}

static void MakeLabel(char *buf, size_t size, const char *prefix, int number) {
    assert(buf);
    assert(prefix);

    snprintf(buf, size, "__%s_%d", prefix, number);
}

static uint8_t ChooseJCC(LangNode_t *cond) {
    if (!cond || cond->type != kOperation) return 0x84;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (cond->value.operation) {
        case kOperationA:  return 0x8E; // jle 
        case kOperationAE: return 0x8C; // jl  
        case kOperationB:  return 0x8D; // jge 
        case kOperationBE: return 0x8F; // jg  
        case kOperationE:  return 0x85; // jne 
        case kOperationNE: return 0x84; // je  
        default:           return 0x84;
    }
    #pragma GCC diagnostic pop
}

static int ResolveShift(VariableArr *arr, LangNode_t *node, AsmInfo *info, Sub *sub) {
    assert(arr);
    assert(node);
    assert(info);
    assert(sub);

    LangNode_t *check = node;
    if (IsThatOperation(node, kOperationGetAddr) || IsThatOperation(node, kOperationCallAddr)) {
        check = node->left;
    }

    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (arr->var_array[check->value.pos].variable_name &&
            arr->var_array[i].variable_name &&
            strcmp(arr->var_array[i].variable_name, arr->var_array[check->value.pos].variable_name) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = info->counter++;
            } else {
                var_idx = arr->var_array[i].pos_in_code;
            }

            break;
        }
    }

    if (var_idx == -1) {
        fprintf(stderr, "Unknown variable.\n");
        return 0;
    }

    return var_idx - sub->param_count;
}

static void CodeGenerateExpr(Context *context, LangNode_t *expr, VariableArr *arr, AsmInfo *info, Sub *sub);
static void CodeGenerateStatement(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub);

// pop rax; [&ram[r12 + shift]] = rax 
static void CodeGeneratePopToVar(Context *context, VariableArr *arr, LangNode_t *node, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(arr);
    assert(node);
    assert(info);
    assert(sub);

    int shift = ResolveShift(arr, node, info, sub);
    EmitPop(context, FindRegCode("rax"));
    EmitVarAddr(context, shift);

    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0x01); // mov [rcx], rax 
}

// rax=[rbp + frame_offset]; [&ram[r12 + shift]] = rax 
static void CodeGenerateStoreParam(Context *context, VariableArr *arr, LangNode_t *node, AsmInfo *info, Sub *sub, int frame_offset) {
    assert(context);
    assert(arr);
    assert(node);
    assert(info);
    assert(sub);

    int shift = ResolveShift(arr, node, info, sub);
    if (frame_offset >= -128 && frame_offset <= 127) {
        Emit8(CD, 0x48);
        Emit8(CD, 0x8B);
        Emit8(CD, 0x45);
        Emit8(CD, (uint8_t)(int8_t)frame_offset);
    } else {
        Emit8(CD, 0x48);
        Emit8(CD, 0x8B);
        Emit8(CD, 0x85);
        Emit32(CD, (uint32_t)(int32_t)frame_offset);
    }

    EmitVarAddr(context, shift);
    Emit8(CD, 0x48);
    Emit8(CD, 0x89);
    Emit8(CD, 0x01);
}

static void CodeGenerateParamsToRam(Context *context, LangNode_t *args, VariableArr *arr, AsmInfo *info, Sub *sub, int *frame_offset) {
    assert(context);
    assert(arr);
    assert(info);
    assert(sub);
    assert(frame_offset);
    if (!args) return;

    if (!IsThatOperation(args, kOperationComma)) {
        CodeGenerateStoreParam(context, arr, args, info, sub, *frame_offset);
        *frame_offset += 8;
        return;
    }

    if (args->left) {
        CodeGenerateParamsToRam(context, args->left, arr, info, sub, frame_offset);
    }

    if (args->right) {
        CodeGenerateParamsToRam(context, args->right, arr, info, sub, frame_offset);
    }
}

static void CodeGenerateParamsToStack(Context *context, LangNode_t *args, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(arr);
    assert(info);
    assert(sub);
    if (!args) return;

    if (!IsThatOperation(args, kOperationComma)) {
        CodeGenerateExpr(context, args, arr, info, sub);
        return;
    }

    if (args->left) {
        CodeGenerateParamsToStack(context, args->right, arr, info, sub);
    }

    if (args->right) {
        CodeGenerateParamsToStack(context, args->left, arr, info, sub);
    }
}

static void CodeGenerateAddrOf(Context *context, LangNode_t *var, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(var);
    assert(arr);
    assert(info);
    assert(sub);

    int shift = FindVarPos(arr, var, info) - sub->param_count;
    EmitVarAddr(context, shift);
    EmitPush(context, FindRegCode("rcx"));
}

// push [rcx]
static void CodeGenerateDeref(Context *context, LangNode_t *ptr, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(ptr);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateAddrOf(context, ptr, arr, info, sub);
    EmitPop(context, FindRegCode("rcx"));
    Emit8(CD, 0xFF);
    Emit8(CD, 0x31);
}

static void CodeGenerateAddrAssign(Context *context, LangNode_t *deref_node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(deref_node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, deref_node->left, arr, info, sub);
    EmitPop(context, 1);
    EmitPop(context, 0);
    Emit8(CD, 0x48);
    Emit8(CD, 0x89);
    Emit8(CD, 0x01);
}

static void CodeGenerateBinOp(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub, OperationTypes op) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    CodeGenerateExpr(context, node->right, arr, info, sub);
    EmitPop(context, FindRegCode("rbx"));
    EmitPop(context, FindRegCode("rax"));

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (op) {
        case kOperationAdd:
            Emit8(CD, 0x48);
            Emit8(CD, 0x01);
            Emit8(CD, ModRM(3, 3, 0));
            break;

        case kOperationSub:
            Emit8(CD, 0x48);
            Emit8(CD, 0x29);
            Emit8(CD, ModRM(3, 3, 0));
            break;

        case kOperationMul:
            Emit8(CD, 0x48);
            Emit8(CD, 0x0F);
            Emit8(CD, 0xAF);
            Emit8(CD, ModRM(3, 0, 3));
            break;

        case kOperationDiv:
            Emit8(CD, 0x48); Emit8(CD, 0x99); // cqo
            Emit8(CD, 0x48);
            Emit8(CD, 0xF7);
            Emit8(CD, ModRM(3, 7, 3));
            break;

        default:
            break;
    }
    #pragma GCC diagnostic pop

    EmitPush(context, FindRegCode("rax"));
}

static void CodeGeneratePrintInt(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    EmitPop(context, FindRegCode("rsi"));
    EmitMovData(context, FindRegCode("rdi"), "fmt_int");
    Emit8(CD, 0x49); Emit8(CD, 0x89); Emit8(CD, 0xE5); // mov r13, rsp

    EmitAlignStack(context);
    EmitXorEax(context);
    EmitCall(context, "my_printf");
    Emit8(CD, 0x4C); Emit8(CD, 0x89); Emit8(CD, 0xEC); // mov rsp, r13 
}

static void CodeGeneratePrintChar(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    EmitPop(context, FindRegCode("rsi"));
    EmitMovData(context, FindRegCode("rdi"), "fmt_char");
    Emit8(CD, 0x49);
    Emit8(CD, 0x89);
    Emit8(CD, 0xE5);

    EmitAlignStack(context);
    EmitXorEax(context);
    EmitCall(context, "my_printf");
    Emit8(CD, 0x4C);
    Emit8(CD, 0x89);
    Emit8(CD, 0xEC);
}

static void CodeGenerateReadInt(Context *context) {
    assert(context);

    EmitCall(context, "my_scanf");
    EmitPush(context, FindRegCode("rax"));
}

static void CodeGenerateArrAssign(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, stmt->right, arr, info, sub);

    int array_base = FindVarPos(arr, stmt->left->left, info) - sub->param_count;
    EmitMovData(context, FindRegCode("rcx"), "ram");
    EmitMovRR(context, FindRegCode("rdi"), FindRegCode("r12"));
    EmitAddRegImm(context, 7, (int64_t)array_base);

    CodeGenerateExpr(context, stmt->left->right, arr, info, sub);
    EmitPop(context, FindRegCode("rax"));
    Emit8(CD, 0x48); Emit8(CD, 0x01); Emit8(CD, ModRM(3, 0, 7)); // add rdi, rax 
    EmitPop(context, FindRegCode("rax"));
    Emit8(CD, 0x48);
    Emit8(CD, 0x89);
    Emit8(CD, 0x04);
    Emit8(CD, Sib(3, 7, 1));
}

static void CodeGenerateArrDecl(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    arr->var_array[stmt->left->left->left->value.pos].pos_in_code = info->counter;
    int size = (int)stmt->left->left->right->value.number;

    for (int i = 0; i < size; i++) {
        int shift = info->counter + i - sub->param_count;
        EmitVarAddr(context, shift);
        Emit8(CD, 0x48);
        Emit8(CD, 0xC7);
        Emit8(CD, 0x01);
        Emit32(CD, 0);
    }

    info->counter += size;
}

static void CodeGenerateIf(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    LangNode_t *cond = stmt->left;
    int if_number = info->label_if++;
    int else_number = info->label_else++;
    int has_else = IsThatOperation(stmt->right, kOperationElse);
    char else_label[64] = {};
    char end_label[64] = {};

    MakeLabel(else_label, sizeof(else_label), "else", else_number);
    MakeLabel(end_label, sizeof(end_label), "end_if", if_number);

    CodeGenerateExpr(context, cond->left, arr, info, sub);
    CodeGenerateExpr(context, cond->right, arr, info, sub);
    EmitPop(context, 3);
    EmitPop(context, 0);
    EmitCmpRaxRbx(context);
    EmitJCC(context, ChooseJCC(cond), else_label);

    if (has_else) {
        CodeGenerateStatement(context, stmt->right->left, arr, info, sub);
    } else {
        CodeGenerateStatement(context, stmt->right, arr, info, sub);
    }

    EmitJmp(context, end_label);
    LabelAdd(context, else_label, CD->size);

    if (has_else) {
        CodeGenerateStatement(context, stmt->right->right, arr, info, sub);
    }

    LabelAdd(context, end_label, CD->size);
}

static void CodeGenerateWhile(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    int start_number = info->label_counter++;
    int end_number = info->label_counter++;
    char start_label[64] = {};
    char end_label[64] = {};

    MakeLabel(start_label, sizeof(start_label), "wstart", start_number);
    MakeLabel(end_label, sizeof(end_label), "wend", end_number);

    LabelAdd(context, start_label, CD->size);
    CodeGenerateExpr(context, stmt->left->left, arr, info, sub);
    CodeGenerateExpr(context, stmt->left->right, arr, info, sub);
    EmitPop(context, 3);
    EmitPop(context, 0);

    EmitCmpRaxRbx(context);
    EmitJCC(context, ChooseJCC(stmt->left), end_label);
    CodeGenerateStatement(context, stmt->right, arr, info, sub);
    EmitJmp(context, start_label);
    LabelAdd(context, end_label, CD->size);
}

static void CodeGenerateReturn(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, stmt->left, arr, info, sub);
    EmitPop(context, 0);
    EmitEpilogue(context);
    EmitRet(context);
}

static void CodeGenerateExpr(Context *context, LangNode_t *expr, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(arr);
    assert(info);
    assert(sub);
    if (!expr) return;

    switch (expr->type) {
        case kNumber: {
            int64_t number = (int64_t)expr->value.number;
            if (number >= 0 && number <= 0x7FFFFFFF) {
                Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, ModRM(3, 0, 0));
                Emit32(CD, (uint32_t)(int32_t)number);
            } else {
                EmitMovR64Imm64(context, 0, number);
            }

            EmitPush(context, 0);
            break;
        }

        case kVariable: {
            int shift = FindVarPos(arr, expr, info) - sub->param_count;
            EmitVarAddr(context, shift);
            Emit8(CD, 0xFF); Emit8(CD, 0x31); // push [rcx] 
            break;
        }

        case kOperation:
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wswitch-enum"
            switch (expr->value.operation) {
                case kOperationAdd:
                    CodeGenerateBinOp(context, expr, arr, info, sub, kOperationAdd);
                    break;

                case kOperationSub:
                    CodeGenerateBinOp(context, expr, arr, info, sub, kOperationSub);
                    break;

                case kOperationMul:
                    CodeGenerateBinOp(context, expr, arr, info, sub, kOperationMul);
                    break;

                case kOperationDiv:
                    CodeGenerateBinOp(context, expr, arr, info, sub, kOperationDiv);
                    break;

                case kOperationSQRT:
                    CodeGenerateExpr(context, expr->left, arr, info, sub);
                    EmitPop(context, 0);
                    // cvtsi2sd xmm0, rax; sqrtsd xmm0, xmm0; cvttsd2si rax, xmm0 
                    Emit8(CD, 0xF2); Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0x2A); Emit8(CD, 0xC0);
                    Emit8(CD, 0xF2); Emit8(CD, 0x0F); Emit8(CD, 0x51); Emit8(CD, 0xC0);
                    Emit8(CD, 0xF2); Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0x2C); Emit8(CD, 0xC0);
                    EmitPush(context, 0);
                    break;

                case kOperationCallAddr:
                    CodeGenerateAddrOf(context, expr->left, arr, info, sub);
                    break;

                case kOperationGetAddr:
                    CodeGenerateDeref(context, expr->left, arr, info, sub);
                    break;

                case kOperationCall: {
                    const char *callee = arr->var_array[expr->left->value.pos].variable_name;
                    int num_args = CountArgs(expr->right);
                    CodeGenerateParamsToStack(context, expr->right, arr, info, sub);
                    EmitCall(context, callee);
                    if (num_args > 0) {
                        EmitAddRegImm(context, FindRegCode("rsp"), (int64_t)(num_args * 8));
                    }

                    EmitPush(context, 0);
                    break;
                }

                case kOperationArrPos: {
                    int array_base = FindVarPos(arr, expr->left, info) - sub->param_count;
                    EmitMovData(context, FindRegCode("rcx"), "ram");
                    EmitMovRR(context, FindRegCode("rdi"), FindRegCode("r12"));
                    EmitAddRegImm(context, 7, (int64_t)array_base);

                    CodeGenerateExpr(context, expr->right, arr, info, sub);
                    EmitPop(context, FindRegCode("rax"));
                    Emit8(CD, 0x48); Emit8(CD, 0x01); Emit8(CD, ModRM(3, 0, 7));

                    // push [rcx + rdi * 8] 
                    Emit8(CD, 0xFF); Emit8(CD, 0x04); Emit8(CD, Sib(3, 7, 1));
                    break;
                }

                default:
                    break;
            }
            #pragma GCC diagnostic pop
            break;

        default:
            break;
    }
}

static void CodeGenerateStatement(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(arr);
    assert(info);
    assert(sub);
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wswitch-enum"
            switch (stmt->value.operation) {
                case kOperationHLT:
                    EmitCall(context, "my_exit");
                    break;

                case kOperationCallAddr:
                    CodeGenerateAddrOf(context, stmt->left, arr, info, sub);
                    break;

                case kOperationGetAddr:
                    CodeGenerateDeref(context, stmt->left, arr, info, sub);
                    break;

                case kOperationCall: {
                    const char *callee = arr->var_array[stmt->left->value.pos].variable_name;
                    int num_args = CountArgs(stmt->right);
                    CodeGenerateParamsToStack(context, stmt->right, arr, info, sub);
                    EmitCall(context, callee);
                    if (num_args > 0) {
                        EmitAddRegImm(context, 4, (int64_t)(num_args * 8));
                    }

                    break;
                }

                case kOperationIs:
                    if (IsThatOperation(stmt->left, kOperationArrPos)) {
                        CodeGenerateArrAssign(context, stmt, arr, info, sub);
                        break;
                    }

                    CodeGenerateExpr(context, stmt->right, arr, info, sub);
                    if (IsThatOperation(stmt->left, kOperationGetAddr)) {
                        CodeGenerateAddrAssign(context, stmt, arr, info, sub);
                        break;
                    }

                    CodeGenerateStatement(context, stmt->left, arr, info, sub);
                    break;

                case kOperationReturn:
                    CodeGenerateReturn(context, stmt, arr, info, sub);
                    break;

                case kOperationWrite:
                    CodeGeneratePrintInt(context, stmt, arr, info, sub);
                    break;

                case kOperationWriteChar:
                    CodeGeneratePrintChar(context, stmt, arr, info, sub);
                    break;

                case kOperationRead:
                    CodeGenerateReadInt(context);
                    CodeGeneratePopToVar(context, arr, stmt->left, info, sub);
                    break;

                case kOperationThen:
                    CodeGenerateStatement(context, stmt->left, arr, info, sub);
                    CodeGenerateStatement(context, stmt->right, arr, info, sub);
                    break;

                case kOperationIf:
                    CodeGenerateIf(context, stmt, arr, info, sub);
                    break;

                case kOperationWhile:
                    CodeGenerateWhile(context, stmt, arr, info, sub);
                    break;

                case kOperationTernary:
                    CodeGenerateStatement(context, stmt->left->right, arr, info, sub);
                    CodeGenerateStatement(context, stmt->left->left, arr, info, sub);
                    break;

                case kOperationArrDecl:
                    CodeGenerateArrDecl(context, stmt, arr, info, sub);
                    break;

                case kOperationDraw: // TODO
                    break;

                default:
                    CodeGenerateExpr(context, stmt, arr, info, sub);
                    break;
            }
            #pragma GCC diagnostic pop
            break;

        case kVariable:
            CodeGeneratePopToVar(context, arr, stmt, info, sub);
            break;

        case kNumber: {
            int64_t number = (int64_t)stmt->value.number;
            Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, ModRM(3, 0, 0));
            Emit32(CD, (uint32_t)(int32_t)number);
            EmitPush(context, 0);
            break;
        }

        default:
            break;
    }
}

static void CodeGenerateFunction(Context *context, LangNode_t *func_node, VariableArr *arr, int *ram_base, AsmInfo *info) {
    assert(context);
    assert(arr);
    assert(ram_base);
    assert(info);
    if (!func_node) return;

    CleanPositions(arr);
    info->counter = 0;

    LangNode_t *args = func_node->right->left;
    const char *func_name = arr->var_array[func_node->left->value.pos].variable_name;
    int is_main = (strcmp(MAIN, func_name) == 0);

    LabelAdd(context, func_name, CD->size);
    if (is_main) {
        LabelAdd(context, "main", CD->size);
    }

    Sub sub = { *ram_base, 0 };
    EmitPrologue(context);

    if (is_main) {
        // xor r12d, r12d 
        Emit8(CD, 0x45); Emit8(CD, 0x31); Emit8(CD, 0xE4);
    }

    int param_count = arr->var_array[func_node->left->value.pos].variable_value;
    sub.param_count = param_count;
    if (param_count > 0) {
        EmitAddRegImm(context, 12, (int64_t)param_count);
    }

    int frame_offset = 16;
    if (args) {
        CodeGenerateParamsToRam(context, args, arr, info, &sub, &frame_offset);
    }

    *ram_base += param_count;
    sub.ram_base = *ram_base;

    CodeGenerateStatement(context, func_node->right->right, arr, info, &sub);

    *ram_base -= param_count;
    EmitEpilogue(context);

    if (is_main) {
        EmitCall(context, "my_exit");
    } else {
        EmitRet(context);
    }
}

static void CodeGenerateProgram(Context *context, LangNode_t *root, VariableArr *arr, int *ram_base, AsmInfo *info) {
    assert(context);
    assert(arr);
    assert(ram_base);
    assert(info);
    if (!root) return;

    info->counter = 0;

    if (IsThatOperation(root, kOperationFunction)) {
        CodeGenerateFunction(context, root, arr, ram_base, info);
    }

    if (root->left) {
        CodeGenerateProgram(context, root->left, arr, ram_base, info);
    }

    if (root->right) {
        CodeGenerateProgram(context, root->right, arr, ram_base, info);
    }
}

static int LoadLib(LibBlob *blob, const char *path) {
    assert(blob);
    assert(path);

    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return 0;
    }

    LibHeader header;
    if (fread(&header, sizeof(header), 1, file) != 1) {
        fprintf(stderr, "%s: failed to read header.\n", path);
        fclose(file);
        return 0;
    }

    blob->printf_off = header.printf_off;
    blob->scanf_off = header.scanf_off;
    blob->exit_off = header.exit_off;
    blob->size = header.code_size;
    blob->reloc_count = header.reloc_count;

    if (header.reloc_count > 0) {
        blob->relocs = (uint32_t *) calloc (header.reloc_count,  sizeof(uint32_t));
        if (!blob->relocs) {
            perror("Error calloc relocs.\n");
            fclose(file);
            return 0;
        }

        if (fread(blob->relocs, sizeof(uint32_t), header.reloc_count, file) != header.reloc_count) {
            fprintf(stderr, "%s: failed to read reloc table.\n", path);
            free(blob->relocs);
            fclose(file);
            return 0;
        }
    } else {
        blob->relocs = NULL;
    }

    blob->data = (uint8_t *) calloc (1, blob->size);
    if (!blob->data) {
        perror("Error calloc blob.\n");
        free(blob->relocs);
        fclose(file);
        return 0;
    }

    if (fread(blob->data, 1, blob->size, file) != blob->size) {
        fprintf(stderr, "%s: failed to read code.\n", path);
        free(blob->data);
        free(blob->relocs);
        fclose(file);
        return 0;
    }

    fclose(file);
    return 1;
}

static void FreeLib(LibBlob *blob) {
    assert(blob);

    free(blob->data);
    free(blob->relocs);
    blob->data = NULL;
    blob->relocs = NULL;
}

void CompileTreeToELF(LangNode_t *root, VariableArr *arr, const char *elf_path) {
    Context context = {};
    PltGot plt_got = {};
    LibBlob blob = {};

    ContextInit(&context);
    BuildData(&context);
    BuildGOT(&context, &plt_got);

    EmitPLT(&context, &plt_got);
    EmitStart(&context);

    AsmInfo info = {};
    int ram_base = 0;
    CodeGenerateProgram(&context, root, arr, &ram_base, &info);

    if (!LoadLib(&blob, "my_lib.bin")) {
        ContextFree(&context);
        return;
    }

    size_t blob_base = context.code.size;

    BufGrow(&context.code, blob.size);
    memcpy(context.code.data + context.code.size, blob.data, blob.size);
    context.code.size += blob.size;

    context.b_printf = blob_base + blob.printf_off;
    context.b_scanf = blob_base + blob.scanf_off;
    context.b_exit = blob_base + blob.exit_off;

    uint64_t blob_vaddr = ELF_BASE + HDRS_TOTAL + blob_base;
    for (uint32_t i = 0; i < blob.reloc_count; i++) {
        size_t patch_offset = blob_base + blob.relocs[i];
        uint64_t current_value;
        memcpy(&current_value, context.code.data + patch_offset, 8);
        Patch64(&context.code, patch_offset, current_value + blob_vaddr);
    }

    size_t seg1 = HDRS_TOTAL + context.code.size;
    size_t data_offset = (seg1 + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    uint64_t data_vaddr = ELF_BASE + data_offset;
    uint64_t base = ELF_BASE + HDRS_TOTAL;

    uint64_t addrs[3] = {
        base + context.b_printf,
        base + context.b_scanf,
        base + context.b_exit,
    };

    for (int i = 0; i < 3; i++) {
        memcpy(context.data.data + plt_got.got_off + i * 8, &addrs[i], 8);
    }

    LinkRelocs(&context, &plt_got, data_vaddr);
    WriteElf(&context, elf_path);

    FreeLib(&blob);
    ContextFree(&context);
}