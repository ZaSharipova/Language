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

static void GenerateMainCode(Context *context, PltGot *plt_got, LangNode_t *root, VariableArr *arr);
static int LoadLib(LibBlob *blob, const char *path);
static void ContextFree(Context *context);
static void MergeAndPatchLibrary(Context *context, LibBlob *blob, size_t *blob_base);
static void FinalizeAndWrite(Context *context, PltGot *plt_got, const char *elf_path);
static void FreeLib(LibBlob *blob);

void CompileTreeToELF(LangNode_t *root, VariableArr *arr, const char *elf_path) {
    assert(root);
    assert(arr);
    assert(elf_path);

    Context context = {};
    PltGot plt_got = {};
    LibBlob blob = {};

    GenerateMainCode(&context, &plt_got, root, arr);

    if (!LoadLib(&blob, "my_lib.bin")) {
        ContextFree(&context);
        return;
    }

    size_t blob_base = 0;
    MergeAndPatchLibrary(&context, &blob, &blob_base);
    FinalizeAndWrite(&context, &plt_got, elf_path);

    FreeLib(&blob);
    ContextFree(&context);
}

static void ContextInit(Context *context);
static void BuildData(Context *context);
static void BuildGOT(Context *context, PltGot *plt_got);
static void EmitPLT(Context *context, PltGot *plt_got);
static void EmitStart(Context *context);
static void CodeGenerateProgram(Context *context, LangNode_t *root, VariableArr *arr, AsmInfo *info);

static void GenerateMainCode(Context *context, PltGot *plt_got, LangNode_t *root, VariableArr *arr) {
    assert(context);
    assert(plt_got);
    assert(root);
    assert(arr);

    ContextInit(context);
    BuildData(context);
    BuildGOT(context, plt_got);
    EmitPLT(context, plt_got);
    EmitStart(context);

    AsmInfo info = {};
    CodeGenerateProgram(context, root, arr, &info);
}

static void BufGrow(Buf *buf, size_t need);
static void Patch64(Buf *buf, size_t offset, uint64_t value);

static void MergeAndPatchLibrary(Context *context, LibBlob *blob, size_t *blob_base) {
    assert(context);
    assert(blob);
    assert(blob_base);

    *blob_base = context->code.size;

    BufGrow(&context->code, blob->size);
    memcpy(context->code.data + context->code.size, blob->data, blob->size);
    context->code.size += blob->size;

    context->b_printf = *blob_base + blob->printf_off;
    context->b_scanf = *blob_base + blob->scanf_off;
    context->b_exit = *blob_base + blob->exit_off;
    context->b_draw = *blob_base + blob->draw_off;

    uint64_t blob_vaddr = ELF_BASE + HDRS_TOTAL + *blob_base;
    for (uint32_t i = 0; i < blob->reloc_count; i++) {
        size_t patch_offset = *blob_base + blob->relocs[i];
        uint64_t current_value;
        memcpy(&current_value, context->code.data + patch_offset, 8);
        Patch64(&context->code, patch_offset, current_value + blob_vaddr);
    }
}

static void LinkRelocs(Context *context, PltGot *plt_got, uint64_t data_vaddr);
static void WriteElf(Context *context, const char *path);

static void FinalizeAndWrite(Context *context, PltGot *plt_got, const char *elf_path) {
    assert(context);
    assert(plt_got);
    assert(elf_path);

    size_t seg1 = HDRS_TOTAL + context->code.size;
    size_t data_offset = (seg1 + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    uint64_t data_vaddr = ELF_BASE + data_offset;
    uint64_t base = ELF_BASE + HDRS_TOTAL;

    uint64_t addrs[4] = {
        base + context->b_printf,
        base + context->b_scanf,
        base + context->b_exit,
        base + context->b_draw,
    };

    for (int i = 0; i < 4; i++) {
        memcpy(context->data.data + plt_got->got_off + i * 8, &addrs[i], 8);
    }

    LinkRelocs(context, plt_got, data_vaddr);
    WriteElf(context, elf_path);
}


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
    BufInit(&context->data, 512);
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

static void EmitMovR64Imm64(Context *context, int reg, int64_t value) {
    assert(context);

    uint8_t rex = 0x48;
    if (reg >= 8) rex |= 0x01;

    Emit8(CD, rex);
    Emit8(CD, (uint8_t)(0xB8 + (reg & 7)));
    Emit64(CD, (uint64_t)value);
}

static void EmitPush(Context *context, int reg) {
    assert(context);

    if (reg >= 8) Emit8(CD, 0x41);
    Emit8(CD, (uint8_t)(0x50 + (reg & 7)));
}

static void EmitPop(Context *context, int reg) {
    assert(context);

    if (reg >= 8) Emit8(CD, 0x41);
    Emit8(CD, (uint8_t)(0x58 + (reg & 7)));
}

static void EmitMovRR(Context *context, int dst, int src) {
    assert(context);

    Emit8(CD, RexW(src, dst));
    Emit8(CD, 0x89);
    Emit8(CD, ModRM(3, src, dst));
}

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

static void EmitCall(Context *context, const char *name) {
    assert(context);
    assert(name);

    Emit8(CD, 0xE8);
    RelocAdd(context, CD->size, name, 0);
    Emit32(CD, 0);
}

static void EmitJmp(Context *context, const char *name) {
    assert(context);
    assert(name);

    Emit8(CD, 0xE9);
    RelocAdd(context, CD->size, name, 0);
    Emit32(CD, 0);
}

static void EmitJCC(Context *context, uint8_t cc, const char *name) {
    assert(context);
    assert(name);

    Emit8(CD, 0x0F); Emit8(CD, cc);
    RelocAdd(context, CD->size, name, 0);
    Emit32(CD, 0);
}

static void EmitMovData(Context *context, int reg, const char *symbol) {
    assert(context);
    assert(symbol);

    EmitMovR64Imm64(context, reg, 0);
    RelocAdd(context, CD->size - 8, symbol, 1);
}

static void EmitAlignStack(Context *context) {
    assert(context);

    Emit8(CD, RexW(0, kRSP));
    Emit8(CD, 0x83);
    Emit8(CD, ModRM(3, 4, kRSP));
    Emit8(CD, 0xF0);
}

static void EmitCmpRaxRbx(Context *context) {
    assert(context);
    Emit8(CD, RexW(kRBX, kRAX));
    Emit8(CD, 0x39);
    Emit8(CD, ModRM(3, kRBX, kRAX));
}

static void EmitXorEax(Context *context) {
    assert(context);

    Emit8(CD, 0x31);
    Emit8(CD, ModRM(3, kRAX, kRAX));
}

static void EmitRet(Context *context) {
    assert(context);

    Emit8(CD, 0xC3);
}

/* -----------------------------------------------------------------------------------
 * Variable addressing on the stack.
 *
 * Function frame layout:
 *   [rbp + 16 + 8 * i] -- i-th parameter (i = 0 .. param_count - 1)
 *   [rbp + 8]          -- return address
 *   [rbp]              -- saved caller's rbp
 *   [rbp - 8 * 1]      -- 0th local cell
 *   [rbp - 8 * 2]      -- 1st local cell
 *   ...
 *   [rbp - frame_size] -- last local cell
 *
 * Variable slot identifier (pos_in_code):
 *   0 .. param_count - 1 -> parameters (read from [rbp + 16 + ...])
 *   >= param_count       -> locals (read from [rbp - 8 * ((slot - param_count) + 1)])
 * ------------------------------------------------------------------------------------
 */

// rcx = lea [rbp + disp]
static void EmitLeaRcxRbp(Context *context, int32_t disp) {
    assert(context);

    Emit8(CD, RexW(kRCX, kRBP));        // REX.W
    Emit8(CD, 0x8D);                    // lea
    if (disp >= -128 && disp <= 127) {
        Emit8(CD, ModRM(1, kRCX, kRBP));
        Emit8(CD, (uint8_t)(int8_t)disp);
    } else {
        Emit8(CD, ModRM(2, kRCX, kRBP));
        Emit32(CD, (uint32_t)disp);
    }
}

static void EmitVarAddrBySlot(Context *context, int slot, int param_count) {
    assert(context);

    if (slot < param_count) {
        int32_t disp = 16 + 8 * slot;
        EmitLeaRcxRbp(context, disp);
    } else {
        int local_idx = slot - param_count;
        int32_t disp = -8 * (local_idx + 1);
        EmitLeaRcxRbp(context, disp);
    }
}

static void EmitPrologue(Context *context, int frame_size) {
    assert(context);

    EmitPush(context, kRBP);                                 // push rbp
    EmitMovRR(context, kRBP, kRSP);                          // mov rbp, rsp
    if (frame_size > 0) {
        EmitAddRegImm(context, kRSP, -(int64_t)frame_size);  // sub rsp, frame_size
    }
}

static void EmitEpilogue(Context *context) {
    assert(context);

    EmitMovRR(context, kRSP, kRBP);          // mov rsp, rbp
    EmitPop(context, kRBP);                  // pop rbp
}

static void EmitStart(Context *context) {
    assert(context);

    context->b_start = CD->size;

    EmitAlignStack(context);
    EmitAddRegImm(context, kRSP, -8);

    Emit8(CD, 0xE8);
    RelocAdd(context, CD->size, "main", 0);
    Emit32(CD, 0);

    EmitMovRR(context, kRDI, kRAX);

    Emit8(CD, RexW(0, kRAX)); Emit8(CD, 0xC7);
    Emit8(CD, ModRM(3, 0, kRAX)); Emit32(CD, 60);

    Emit8(CD, 0x0F); Emit8(CD, 0x05);
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
    Emit64(data, 3);
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
}

static void EmitPLT(Context *context, PltGot *plt_got) {
    assert(context);
    assert(plt_got);

    plt_got->plt_printf = CD->size;
    LabelAdd(context, "my_printf", plt_got->plt_printf);
    Emit8(CD, 0xFF);
    Emit8(CD, ModRM(0, 4, 5));
    RelocAdd(context, CD->size, "__got_printf", 2);
    Emit32(CD, 0);

    plt_got->plt_scanf = CD->size;
    LabelAdd(context, "my_scanf", plt_got->plt_scanf);
    Emit8(CD, 0xFF);
    Emit8(CD, ModRM(0, 4, 5));
    RelocAdd(context, CD->size, "__got_scanf", 2);
    Emit32(CD, 0);

    plt_got->plt_exit = CD->size;
    LabelAdd(context, "my_exit", plt_got->plt_exit);
    Emit8(CD, 0xFF);
    Emit8(CD, 0x25);
    RelocAdd(context, CD->size, "__got_exit", 2);
    Emit32(CD, 0);

    plt_got->plt_draw = CD->size;
    LabelAdd(context, "my_draw", plt_got->plt_draw);
    Emit8(CD, 0xFF);
    Emit8(CD, 0x25);
    RelocAdd(context, CD->size, "__got_draw", 2);
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

    return 0;
}

static void LinkRelocs(Context *context, PltGot *plt_got, uint64_t data_vaddr) { // TODO
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
            } else if (strcmp(reloc->name, "__got_draw") == 0) {
                got_slot_vaddr = data_vaddr + plt_got->got_off + 3 * 8;
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

static void WriteElfHeader(FILE *file, uint64_t entry);
static void WriteCodeSegmentPhdr(FILE *file, size_t seg1);
static void WriteDataSegmentPhdr(FILE *file, size_t data_off, uint64_t data_vaddr, size_t data_size);
static void WritePadding(FILE *file, size_t pad);

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

    WriteElfHeader(file, entry);
    WriteCodeSegmentPhdr(file, seg1);
    WriteDataSegmentPhdr(file, data_off, data_vaddr, context->data.size);

    fwrite(context->code.data, 1, context->code.size, file);

    WritePadding(file, data_off - seg1);
    fwrite(context->data.data, 1, context->data.size, file);

    fclose(file);
}

static void WriteElfHeader(FILE *file, uint64_t entry) {
    assert(file);
    uint8_t header[ELF_HEADER_SIZE] = {};

    header[0] = 0x7F; header[1] = 'E'; header[2] = 'L'; header[3] = 'F';
    header[4] = 2;
    header[5] = 1;
    header[6] = 1;

    uint16_t value16 = 2;  memcpy(header + 16, &value16, 2);
             value16 = 62; memcpy(header + 18, &value16, 2);

    uint32_t value32 = 1;  memcpy(header + 20, &value32, 4);

    memcpy(header + 24, &entry, 8);

    uint64_t value64 = 64; memcpy(header + 32, &value64, 8);
             value64 = 64; memcpy(header + 52, &value64, 2);

             value16 = 56; memcpy(header + 54, &value16, 2);
             value16 = 2; memcpy(header + 56, &value16, 2);

    fwrite(header, 1, ELF_HEADER_SIZE, file);
}

static void WriteCodeSegmentPhdr(FILE *file, size_t seg1) {
    assert(file);
    uint8_t ph[PHDR_SIZE] = {};

    uint32_t value32 = 1; memcpy(ph, &value32, 4);
             value32 = 7; memcpy(ph + 4, &value32, 4);

    uint64_t value64 = 0; memcpy(ph + 8, &value64, 8);
             value64 = ELF_BASE; memcpy(ph + 16, &value64, 8);
    memcpy(ph + 24, &value64, 8);
             value64 = seg1; memcpy(ph + 32, &value64, 8);
    memcpy(ph + 40, &value64, 8);
             value64 = PAGE_SIZE; memcpy(ph + 48, &value64, 8);

    fwrite(ph, 1, PHDR_SIZE, file);
}

static void WriteDataSegmentPhdr(FILE *file, size_t data_off, uint64_t data_vaddr, size_t data_size) {
    assert(file);
    uint8_t ph[PHDR_SIZE] = {};

    uint32_t value32 = 1; memcpy(ph, &value32, 4);
    value32 = 6; memcpy(ph + 4, &value32, 4);

    memcpy(ph + 8, &data_off, 8);
    memcpy(ph + 16, &data_vaddr, 8);
    memcpy(ph + 24, &data_vaddr, 8);

    uint64_t value64 = data_size;
    memcpy(ph + 32, &value64, 8);
    memcpy(ph + 40, &value64, 8);

    value64 = PAGE_SIZE; memcpy(ph + 48, &value64, 8);

    fwrite(ph, 1, PHDR_SIZE, file);
}

static void WritePadding(FILE *file, size_t pad) {
    assert(file);
    if (pad == 0) return;

    uint8_t *zeroes = (uint8_t *) calloc (1, pad);
    if (!zeroes) {
        perror("Error calloc.\n");
        return;
    }

    fwrite(zeroes, 1, pad, file);
    free(zeroes);
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
        case kOperationA:  return 0x8E;
        case kOperationAE: return 0x8C;
        case kOperationB:  return 0x8D;
        case kOperationBE: return 0x8F;
        case kOperationE:  return 0x85;
        case kOperationNE: return 0x84;
        default:           return 0x84;
    }
    #pragma GCC diagnostic pop
}

static void CountLocalSlots(LangNode_t *node, VariableArr *arr, AsmInfo *info) {
    if (!node) return;

    if (IsThatOperation(node, kOperationArrDecl)) {
        LangNode_t *arr_pos = node->left;
        if (arr_pos && arr_pos->left && arr_pos->right) {
            int var_pos = arr_pos->left->value.pos;
            int size = (int)arr_pos->right->value.number;

            if (arr->var_array[var_pos].pos_in_code == -1) {
                arr->var_array[var_pos].pos_in_code = info->counter;
                info->counter += size;
            }
        }

        return;
    }

    if (node->type == kVariable) {
        int pos = node->value.pos;
        for (size_t i = 0; i < arr->size; i++) {
            if (arr->var_array[pos].variable_name && arr->var_array[i].variable_name &&
                    strcmp(arr->var_array[i].variable_name, arr->var_array[pos].variable_name) == 0) {
                if (arr->var_array[i].pos_in_code == -1) {
                    arr->var_array[i].pos_in_code = info->counter++;
                }

                break;
            }
        }
    }

    CountLocalSlots(node->left, arr, info);
    CountLocalSlots(node->right, arr, info);
}

static int GetVarSlot(VariableArr *arr, LangNode_t *node) {
    assert(arr);
    assert(node);

    LangNode_t *check = node;
    if (IsThatOperation(node, kOperationGetAddr) || IsThatOperation(node, kOperationCallAddr)) {
        check = node->left;
    }

    int var_pos = check->value.pos;
    const char *name = arr->var_array[var_pos].variable_name;

    for (size_t i = 0; i < arr->size; i++) {
        if (name && arr->var_array[i].variable_name &&
                strcmp(arr->var_array[i].variable_name, name) == 0) {
            if (arr->var_array[i].pos_in_code != -1) {
                return arr->var_array[i].pos_in_code;
            }

            break;
        }
    }

    fprintf(stderr, "Unknown variable.\n");
    return 0;
}

static void CodeGenerateExpr(Context *context, LangNode_t *expr, VariableArr *arr, AsmInfo *info, Sub *sub);
static void CodeGenerateStatement(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub);

// pop rax; mov [rcx_addr_of(var)], rax
static void CodeGeneratePopToVar(Context *context, VariableArr *arr, LangNode_t *node, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(arr);
    assert(node);
    assert(info);
    assert(sub);
    (void)info;

    int slot = GetVarSlot(arr, node);
    EmitPop(context, kRAX);
    EmitVarAddrBySlot(context, slot, sub->param_count);

    Emit8(CD, RexW(kRAX, kRCX));
    Emit8(CD, 0x89);
    Emit8(CD, ModRM(0, kRAX, kRCX)); // mov [rcx], rax
}

static void CodeGenerateAddrOf(Context *context, LangNode_t *var, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(var);
    assert(arr);
    assert(info);
    assert(sub);
    (void)info;

    int slot = GetVarSlot(arr, var);
    EmitVarAddrBySlot(context, slot, sub->param_count);
    EmitPush(context, kRCX);
}

static void CodeGenerateDeref(Context *context, LangNode_t *ptr, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(ptr);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateAddrOf(context, ptr, arr, info, sub);
    EmitPop(context, kRCX);
    Emit8(CD, 0xFF);
    Emit8(CD, ModRM(0, 6, kRCX));
}

static void CodeGenerateAddrAssign(Context *context, LangNode_t *deref_node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(deref_node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, deref_node->left, arr, info, sub);
    EmitPop(context, kRCX);
    EmitPop(context, kRAX);
    Emit8(CD, RexW(kRAX, kRCX));
    Emit8(CD, 0x89);
    Emit8(CD, ModRM(0, kRAX, kRCX));
}

static void CodeGenerateBinOp(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub, OperationTypes op) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    CodeGenerateExpr(context, node->right, arr, info, sub);
    EmitPop(context, kRBX);
    EmitPop(context, kRAX);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (op) {
        case kOperationAdd:
            Emit8(CD, RexW(kRBX, kRAX));
            Emit8(CD, 0x01);
            Emit8(CD, ModRM(3, kRBX, kRAX));
            break;

        case kOperationSub:
            Emit8(CD, RexW(kRBX, kRAX));
            Emit8(CD, 0x29);
            Emit8(CD, ModRM(3, kRBX, kRAX));
            break;

        case kOperationMul:
            Emit8(CD, RexW(kRAX, kRBX));
            Emit8(CD, 0x0F);
            Emit8(CD, 0xAF);
            Emit8(CD, ModRM(3, kRAX, kRBX));
            break;

        case kOperationDiv:
            Emit8(CD, RexW(0, 0));
            Emit8(CD, 0x99);
            Emit8(CD, RexW(0, kRBX));
            Emit8(CD, 0xF7);
            Emit8(CD, ModRM(3, 7, kRBX));
            break;

        default:
            break;
    }
    #pragma GCC diagnostic pop

    EmitPush(context, kRAX);
}

static void EmitSaveRspToR11(Context *context) {
    Emit8(CD, RexW(kRSP, kR11));
    Emit8(CD, 0x89);
    Emit8(CD, ModRM(3, kRSP, kR11));
}

static void EmitRestoreRspFromR11(Context *context) {
    Emit8(CD, RexW(kR11, kRSP));
    Emit8(CD, 0x89);
    Emit8(CD, ModRM(3, kR11, kRSP));
}

static void CodeGeneratePrintInt(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    EmitPop(context, kRSI);
    EmitMovData(context, kRDI, "fmt_int");

    EmitSaveRspToR11(context);
    EmitAlignStack(context);
    EmitXorEax(context);
    EmitCall(context, "my_printf");
    EmitRestoreRspFromR11(context);
}

static void CodeGeneratePrintChar(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    EmitPop(context, kRSI);
    EmitMovData(context, kRDI, "fmt_char");

    EmitSaveRspToR11(context);
    EmitAlignStack(context);
    EmitXorEax(context);
    EmitCall(context, "my_printf");
    EmitRestoreRspFromR11(context);
}

static void CodeGenerateReadInt(Context *context) {
    assert(context);

    EmitSaveRspToR11(context);
    EmitAlignStack(context);
    EmitCall(context, "my_scanf");
    EmitRestoreRspFromR11(context);
    EmitPush(context, kRAX);
}

static void CodeGenerateDraw(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateAddrOf(context, node->left, arr, info, sub);
    EmitPop(context, kRDI);

    EmitSaveRspToR11(context);
    EmitAlignStack(context);
    EmitCall(context, "my_draw");
    EmitRestoreRspFromR11(context);
}

static void CodeGenerateArrAssign(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, stmt->right, arr, info, sub);          // stack: [value]
    CodeGenerateExpr(context, stmt->left->right, arr, info, sub);    // stack: [value, index]
    EmitPop(context, kRDI);                                          // rdi = index
    EmitPop(context, kRAX);                                          // rax = value

    int slot = GetVarSlot(arr, stmt->left->left);
    int local_idx = slot - sub->param_count;

    // rcx = lea [rbp - 8*(local_idx + 1)]
    int32_t base_disp = -8 * (local_idx + 1);
    EmitLeaRcxRbp(context, base_disp);

    // rdi = rdi * 8  -> shl rdi, 3
    Emit8(CD, RexW(0, kRDI));
    Emit8(CD, 0xC1);
    Emit8(CD, ModRM(3, 4, kRDI));   // /4 = SHL
    Emit8(CD, 3);

    // rcx = rcx - rdi  -> sub rcx, rdi
    Emit8(CD, RexW(kRDI, kRCX));
    Emit8(CD, 0x29);
    Emit8(CD, ModRM(3, kRDI, kRCX));

    // mov [rcx], rax
    Emit8(CD, RexW(kRAX, kRCX));
    Emit8(CD, 0x89);
    Emit8(CD, ModRM(0, kRAX, kRCX));
}

static void CodeGenerateArrDecl(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);
    (void)info;

    int base_slot = arr->var_array[stmt->left->left->left->value.pos].pos_in_code;
    int size = (int)stmt->left->left->right->value.number;

    for (int i = 0; i < size; i++) {
        int slot = base_slot + i;
        EmitVarAddrBySlot(context, slot, sub->param_count);
        // mov qword ptr [rcx], 0
        Emit8(CD, RexW(0, kRCX));
        Emit8(CD, 0xC7);
        Emit8(CD, ModRM(0, 0, kRCX));
        Emit32(CD, 0);
    }
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
    char else_label[DEFAULT_LABEL_SIZE] = {};
    char end_label[DEFAULT_LABEL_SIZE] = {};

    MakeLabel(else_label, sizeof(else_label), "else", else_number);
    MakeLabel(end_label, sizeof(end_label), "end_if", if_number);

    CodeGenerateExpr(context, cond->left, arr, info, sub);
    CodeGenerateExpr(context, cond->right, arr, info, sub);
    EmitPop(context, kRBX);
    EmitPop(context, kRAX);
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
    char start_label[DEFAULT_LABEL_SIZE] = {};
    char end_label[DEFAULT_LABEL_SIZE] = {};

    MakeLabel(start_label, sizeof(start_label), "wstart", start_number);
    MakeLabel(end_label, sizeof(end_label), "wend", end_number);

    LabelAdd(context, start_label, CD->size);
    CodeGenerateExpr(context, stmt->left->left, arr, info, sub);
    CodeGenerateExpr(context, stmt->left->right, arr, info, sub);
    EmitPop(context, kRBX);
    EmitPop(context, kRAX);

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
    EmitPop(context, kRAX);
    EmitEpilogue(context);
    EmitRet(context);
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
                Emit8(CD, RexW(0, kRAX));
                Emit8(CD, 0xC7);
                Emit8(CD, ModRM(3, 0, kRAX));
                Emit32(CD, (uint32_t)(int32_t)number);
            } else {
                EmitMovR64Imm64(context, kRAX, number);
            }

            EmitPush(context, kRAX);
            break;
        }

        case kVariable: {
            int slot = GetVarSlot(arr, expr);
            EmitVarAddrBySlot(context, slot, sub->param_count);
            Emit8(CD, 0xFF); Emit8(CD, ModRM(0, 6, kRCX)); // push qword [rcx]
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
                    EmitPop(context, kRAX);
                    Emit8(CD, 0xF2); Emit8(CD, RexW(0, kRAX));
                    Emit8(CD, 0x0F); Emit8(CD, 0x2A); Emit8(CD, ModRM(3, 0, kRAX));

                    Emit8(CD, 0xF2); Emit8(CD, 0x0F); Emit8(CD, 0x51); Emit8(CD, ModRM(3, 0, 0));

                    Emit8(CD, 0xF2); Emit8(CD, RexW(kRAX, 0)); Emit8(CD, 0x0F); Emit8(CD, 0x2C);
                    Emit8(CD, ModRM(3, kRAX, 0));

                    EmitPush(context, kRAX);
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
                        EmitAddRegImm(context, kRSP, (int64_t)(num_args * 8));
                    }

                    EmitPush(context, kRAX);
                    break;
                }

                case kOperationArrPos: {
                    int slot = GetVarSlot(arr, expr->left);
                    int local_idx = slot - sub->param_count;

                    CodeGenerateExpr(context, expr->right, arr, info, sub);
                    EmitPop(context, kRDI);   // rdi = index

                    // rcx = lea [rbp - 8*(local_idx + 1)]
                    int32_t base_disp = -8 * (local_idx + 1);
                    EmitLeaRcxRbp(context, base_disp);

                    // rdi <<= 3
                    Emit8(CD, RexW(0, kRDI));
                    Emit8(CD, 0xC1);
                    Emit8(CD, ModRM(3, 4, kRDI));
                    Emit8(CD, 3);

                    // rcx -= rdi
                    Emit8(CD, RexW(kRDI, kRCX));
                    Emit8(CD, 0x29);
                    Emit8(CD, ModRM(3, kRDI, kRCX));

                    // push qword [rcx]
                    Emit8(CD, 0xFF);
                    Emit8(CD, ModRM(0, 6, kRCX));
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
                        EmitAddRegImm(context, kRSP, (int64_t)(num_args * 8));
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

                case kOperationDraw:
                    CodeGenerateDraw(context, stmt, arr, info, sub);
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
            Emit8(CD, RexW(0, kRAX));
            Emit8(CD, 0xC7);
            Emit8(CD, ModRM(3, 0, kRAX));
            Emit32(CD, (uint32_t)(int32_t)number);
            EmitPush(context, kRAX);
            break;
        }

        default:
            break;
    }
}

static void AssignParamSlots(LangNode_t *args, VariableArr *arr, int *slot_counter) {
    if (!args) return;

    if (!IsThatOperation(args, kOperationComma)) {
        if (args->type == kVariable) {
            int pos = args->value.pos;
            const char *name = arr->var_array[pos].variable_name;

            for (size_t i = 0; i < arr->size; i++) {
                if (name && arr->var_array[i].variable_name &&
                        strcmp(arr->var_array[i].variable_name, name) == 0) {
                    if (arr->var_array[i].pos_in_code == -1) {
                        arr->var_array[i].pos_in_code = (*slot_counter)++;
                    }

                    break;
                }
            }
        }
        return;
    }

    AssignParamSlots(args->left, arr, slot_counter);
    AssignParamSlots(args->right, arr, slot_counter);
}

static void CodeGenerateFunction(Context *context, LangNode_t *func_node, VariableArr *arr, AsmInfo *info) {
    assert(context);
    assert(arr);
    assert(info);
    if (!func_node) return;

    CleanPositions(arr);
    info->counter = 0;

    LangNode_t *args = func_node->right->left;
    LangNode_t *body = func_node->right->right;
    const char *func_name = arr->var_array[func_node->left->value.pos].variable_name;
    int is_main = (strcmp(MAIN, func_name) == 0);

    int param_count = arr->var_array[func_node->left->value.pos].variable_value;

    int slot_counter = 0;
    if (args) {
        AssignParamSlots(args, arr, &slot_counter);
    }

    if (slot_counter < param_count) {
        slot_counter = param_count;
    }


    info->counter = slot_counter;
    CountLocalSlots(body, arr, info);
    int total_slots = info->counter;
    int local_slots = total_slots - param_count;
    if (local_slots < 0) local_slots = 0;

    int frame_size = local_slots * 8;
    if (frame_size % 16 != 0) frame_size += 8;

    Sub sub = { 0, param_count, frame_size };

    LabelAdd(context, func_name, CD->size);
    if (is_main) {
        LabelAdd(context, "main", CD->size);
    }

    EmitPrologue(context, frame_size);
    CodeGenerateStatement(context, body, arr, info, &sub);
    EmitEpilogue(context);

    if (is_main) {
        EmitCall(context, "my_exit");
    } else {
        EmitRet(context);
    }
}

static void CodeGenerateProgram(Context *context, LangNode_t *root, VariableArr *arr, AsmInfo *info) {
    assert(context);
    assert(arr);
    assert(info);
    if (!root) return;

    info->counter = 0;

    if (IsThatOperation(root, kOperationFunction)) {
        CodeGenerateFunction(context, root, arr, info);
    }

    if (root->left) {
        CodeGenerateProgram(context, root->left, arr, info);
    }

    if (root->right) {
        CodeGenerateProgram(context, root->right, arr, info);
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

    LibHeader header = {};
    if (fread(&header, sizeof(header), 1, file) != 1) {
        fprintf(stderr, "%s: failed to read header.\n", path);
        fclose(file);
        return 0;
    }

    blob->printf_off = header.printf_off;
    blob->scanf_off = header.scanf_off;
    blob->exit_off = header.exit_off;
    blob->draw_off = header.draw_off;
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