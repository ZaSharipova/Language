#include "Back-End/TreeToBin.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/CommonFunctions.h"

#define ELF_BASE 0x400000u
#define PAGE_SIZE 0x1000u
#define ELF_HEADER_SIZE 64
#define PHDR_SIZE 56
#define NUM_PHDRS 2
#define RAM_SIZE 65536
#define HDRS_TOTAL (ELF_HEADER_SIZE + NUM_PHDRS * PHDR_SIZE)

#define MAX_LABELS 4096
#define MAX_RELOCS 8192
#define CALLEE_SIZE 24 // push r12/r13/rbx = 3 * 8

#define DEFAULT_SIZE 128


typedef struct {
    uint8_t *data;
    size_t size, capacity;
} Buf;

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

    uint8_t *ptr = buf->data;
    ptr = (uint8_t *) realloc (buf->data, buf->capacity);
    if (!ptr) {
        perror("Error realloc.\n");
        return;
    }

    buf->data = ptr;
}

static void Emit8(Buf *buf, uint8_t  value) {
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

    size_t fmt_int_off, fmt_char_off, ram_off, scan_buf_off;

    size_t b_printf, b_printf_char, b_scanf, b_exit, b_start; // buildins
} Context;

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
    {"xmm0",  0, 128}, {"xmm1", 1, 128},
    {NULL,    0, 0}
};

static int FindRegCode(const char *reg_name) {
    assert(reg_name);
    
    for (int i = 0; i < REGS_NUMBER; i++) {
        if (strncmp(reg_name, regs[i].name, strlen(reg_name)) == 0) {
            return regs[i].code;
        }
    }

    return -1;
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

    strncpy(context->labels[context->number_labels].name, name, 127);
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

    context->relocs[context->number_relocs].offset  = offset;
    context->relocs[context->number_relocs].type = type;
    const char *name_new = (name[0] == ':') ? name + 1 : name;
    strncpy(context->relocs[context->number_relocs].name, name_new, 127);
    context->number_relocs++;
}

static uint8_t ModRM(int mod, int reg, int rm) {
    return (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}
static uint8_t Sib(int scale, int var_idx, int base) {
    return (uint8_t)((scale << 6) | ((var_idx & 7) <<3 ) | (base & 7));
}
static uint8_t RexW(int reg, int rm) {
    uint8_t r = 0x48;

    if (reg >= 8) r |= 0x04;
    if (rm >= 8) r |= 0x01;
    return r;
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
    int64_t abs = (imm > 0) ? imm : -imm;

    if (abs >= -128 && abs <= 127) {
        Emit8(CD, RexW(0, reg)); Emit8(CD, 0x83);
        Emit8(CD, ModRM(3, slash, reg));
        Emit8(CD, (uint8_t)(int8_t)abs);
    } else {
        Emit8(CD, RexW(0, reg)); Emit8(CD, 0x81);
        Emit8(CD, ModRM(3, slash, reg));
        Emit32(CD, (uint32_t)(int32_t)abs);
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
static void EmitMovData(Context *context, int reg, const char *sym) {
    assert(context);
    assert(sym);

    EmitMovR64Imm64(context, reg, 0);
    RelocAdd(context, CD->size - 8, sym, 1);
}

//- and rsp, -16 -
static void EmitAlignStack(Context *context) {
    assert(context);

    Emit8(CD, 0x48); Emit8(CD, 0x83); Emit8(CD, 0xE4); Emit8(CD, 0xF0);
}

//- cmp rax, rbx -
static void EmitCmpRaxRbx(Context *context) {
    assert(context);

    Emit8(CD, RexW(3, 0)); Emit8(CD, 0x39); Emit8(CD, ModRM(3, 3, 0));
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
    Emit8(CD, RexW(12,7)); Emit8(CD, 0x89); Emit8(CD, ModRM(3, 12, 7));

    EmitAddRegImm(context, FindRegCode("rdi"), (int64_t)shift);

    //- lea rcx, [rcx + rdi * 8]   48 8D 0C + SIB(scale = 3, var_idx = rdi, base = rcx) -
    Emit8(CD, 0x48); Emit8(CD, 0x8D); Emit8(CD, 0x0C);
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

static void EmitBuiltinPrintf(Context *context) {
    assert(context);
    context->b_printf = CD->size;

    Emit8(CD, 0x55);                                   // push rbp
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xE5); // mov rbp, rsp
    Emit8(CD, 0x41); Emit8(CD, 0x54);                  // push r12
    Emit8(CD, 0x53);                                   // push rbx

    // rax = rsi (number)
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xF0);
    // r12d = 0 (~sign flag)
    Emit8(CD, 0x45); Emit8(CD, 0x31); Emit8(CD, 0xE4);
    // test rax,rax; jns + 9
    Emit8(CD, 0x48); Emit8(CD, 0x85); Emit8(CD, 0xC0);
    Emit8(CD, 0x79); Emit8(CD, 0x09);
    // neg rax 
    Emit8(CD, 0x48); Emit8(CD, 0xF7); Emit8(CD, 0xD8);
    // mov r12d, 1 
    Emit8(CD, 0x41); Emit8(CD, 0xBC); Emit32(CD, 1);

    // rcx = 0, rbx = 10 
    Emit8(CD, 0x31); Emit8(CD, 0xC9);
    Emit8(CD, 0xBB); Emit32(CD, 10);

    // div_loop
    size_t div_loop = CD->size;
    Emit8(CD, 0x31); Emit8(CD, 0xD2);                   // xor rdx,rdx 
    Emit8(CD, 0x48); Emit8(CD, 0xF7); Emit8(CD, 0xF3);  // div rbx     
    Emit8(CD, 0x83); Emit8(CD, 0xC2); Emit8(CD, '0');   // add dl, '0'  
    Emit8(CD, 0x52);                                    // push rdx    
    Emit8(CD, 0x48); Emit8(CD, 0xFF); Emit8(CD, 0xC1);  // inc rcx     
    Emit8(CD, 0x48); Emit8(CD, 0x85); Emit8(CD, 0xC0);  // test rax    

    { int8_t rb = (int8_t)((int64_t)div_loop - (int64_t)(CD->size + 2));
    Emit8(CD, 0x75); Emit8(CD, (uint8_t)rb); }

    // if negative: push '-';
    // inc rcx 
    Emit8(CD, 0x45); Emit8(CD, 0x85); Emit8(CD, 0xE4);
    Emit8(CD, 0x74); Emit8(CD, 0x05);
    Emit8(CD, 0x6A); Emit8(CD, '-');
    Emit8(CD, 0x48); Emit8(CD, 0xFF); Emit8(CD, 0xC1);

    // r13 = rcx; sub rsp, 80; rdi = rsp (buffer); rcx = 0 
    Emit8(CD, 0x49); Emit8(CD, 0x89); Emit8(CD, 0xCC);
    Emit8(CD, 0x48); Emit8(CD, 0x83); Emit8(CD, 0xEC); Emit8(CD, 80);
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xE7);
    Emit8(CD, 0x31); Emit8(CD, 0xC9);

    // copy_loop: while rcx < r13 
    size_t copy_loop = CD->size;
    Emit8(CD, 0x4C); Emit8(CD, 0x39); Emit8(CD, 0xE1); // cmp rcx,r13 
    Emit8(CD, 0x7D);
    size_t jge_patch = CD->size;
    Emit8(CD, 0x00);                                   // patched below 

    // address of number in the stack: rbp - (r13 - rcx) * 8 - 16 
    Emit8(CD, 0x4C); Emit8(CD, 0x89); Emit8(CD, 0xE0);                // mov rax,r13 
    Emit8(CD, 0x48); Emit8(CD, 0x29); Emit8(CD, 0xC8);                // sub rax,rcx 
    Emit8(CD, 0x48); Emit8(CD, 0xC1); Emit8(CD, 0xE0); Emit8(CD, 3);  // shl rax,3 
    Emit8(CD, 0x48); Emit8(CD, 0x83); Emit8(CD, 0xC0); Emit8(CD, 16); // add rax,16 
    Emit8(CD, 0x48); Emit8(CD, 0xF7); Emit8(CD, 0xD8);                // neg rax     
    Emit8(CD, 0x48); Emit8(CD, 0x01); Emit8(CD, 0xE8);                // add rax,rbp 
    Emit8(CD, 0x0F); Emit8(CD, 0xB6); Emit8(CD, 0x00);                // movzx eax,[rax] 
    Emit8(CD, 0x88); Emit8(CD, 0x04); Emit8(CD, Sib(0, FindRegCode("rcx"), FindRegCode("rdi")));
    Emit8(CD, 0x48); Emit8(CD, 0xFF); Emit8(CD, 0xC1);                // inc rcx 
    { int8_t rb = (int8_t)((int64_t)copy_loop - (int64_t)(CD->size + 2));
        Emit8(CD, 0xEB); Emit8(CD, (uint8_t)rb); }
    CD->data[jge_patch] = (uint8_t)(CD->size - (jge_patch + 1));

    // add '\n'; write(1, rdi, rdx + 1)
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xFE);                // mov rsi,rdi 
    Emit8(CD, 0x4C); Emit8(CD, 0x89); Emit8(CD, 0xE2);                // mov rdx,r13 
    Emit8(CD, 0xC6); Emit8(CD, 0x04); Emit8(CD, Sib(0, 2, 6)); Emit8(CD, '\n');
    Emit8(CD, 0x48); Emit8(CD, 0xFF); Emit8(CD, 0xC2);                // inc rdx 
    Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, 0xC0); Emit32(CD, 1); // mov rax,1 
    Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, 0xC7); Emit32(CD, 1); // mov rdi,1 
    Emit8(CD, 0x0F); Emit8(CD, 0x05);                                 // syscall   

    // lea rsp, [rbp - 16]; pop rbx; pop r12; pop rbp; ret 
    Emit8(CD, 0x48); Emit8(CD, 0x8D); Emit8(CD, 0x65); Emit8(CD, (uint8_t)(int8_t) - 16);
    Emit8(CD, 0x5B);
    Emit8(CD, 0x41); Emit8(CD, 0x5C);
    Emit8(CD, 0x5D);
    Emit8(CD, 0xC3);
}

/* -------------------------------------------------
 * my_printf_char(rsi = symbol) — write(1, &rsi, 1).
 * -------------------------------------------------
*/
static void EmitBuiltinPrintfChar(Context *context) {
    assert(context);
    context->b_printf_char = CD->size;

    Emit8(CD, 0x55);
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xE5);
    Emit8(CD, 0x56);                                                    // push rsi — symbol on stack
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xE6);                  // mov rsi, rsp 
    Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, 0xC2); Emit32(CD, 1);
    Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, 0xC0); Emit32(CD, 1);
    Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, 0xC7); Emit32(CD, 1);
    Emit8(CD, 0x0F); Emit8(CD, 0x05);
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xEC);                  // mov rsp,rbp 
    Emit8(CD, 0x5D);
    Emit8(CD, 0xC3);
}

static void EmitBuiltinScanf(Context *context) {
    assert(context);
    context->b_scanf = CD->size;

    Emit8(CD, 0x57); Emit8(CD, 0x56); Emit8(CD, 0x52);
    Emit8(CD, 0x53); Emit8(CD, 0x51); Emit8(CD, 0x50);

    // read(0, scan_buf, 16) 
    Emit8(CD, 0x48); Emit8(CD, 0x31); Emit8(CD, 0xC0);
    Emit8(CD, 0x48); Emit8(CD, 0x31); Emit8(CD, 0xFF);
    EmitMovR64Imm64(context, 6, 0);
    RelocAdd(context, CD->size - 8, "scan_buf", 1);
    Emit8(CD, 0xBA); Emit32(CD, 16);
    Emit8(CD, 0x0F); Emit8(CD, 0x05);

    // rsi = scan_buf
    EmitMovR64Imm64(context, 6, 0);
    RelocAdd(context, CD->size - 8, "scan_buf", 1);

    // rax = 0, rcx = 0, rbx = 1 
    Emit8(CD, 0x48); Emit8(CD, 0x31); Emit8(CD, 0xC0);
    Emit8(CD, 0x48); Emit8(CD, 0x31); Emit8(CD, 0xC9);
    Emit8(CD, 0xBB); Emit32(CD, 1);

    // skip_loop 
    size_t skip_loop = CD->size;
    Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0xB6); Emit8(CD, 0x14); Emit8(CD, 0x0E);
    Emit8(CD, 0x80); Emit8(CD, 0xFA); Emit8(CD, 0x20);
    size_t je_sp = CD->size; Emit8(CD, 0x74); Emit8(CD, 0);
    Emit8(CD, 0x80); Emit8(CD, 0xFA); Emit8(CD, 0x09);
    size_t je_tb = CD->size; Emit8(CD, 0x74); Emit8(CD, 0);
    Emit8(CD, 0x80); Emit8(CD, 0xFA); Emit8(CD, 0x0A);
    size_t je_lf = CD->size; Emit8(CD, 0x74); Emit8(CD, 0);
    Emit8(CD, 0x80); Emit8(CD, 0xFA); Emit8(CD, 0x0D);
    size_t je_cr = CD->size; Emit8(CD, 0x74); Emit8(CD, 0);
    size_t jmp_sd = CD->size; Emit8(CD, 0xEB); Emit8(CD, 0);
    size_t skip_label = CD->size;

    CD->data[je_sp + 1] = (uint8_t)(skip_label - (je_sp + 2));
    CD->data[je_tb + 1] = (uint8_t)(skip_label - (je_tb + 2));
    CD->data[je_lf + 1] = (uint8_t)(skip_label - (je_lf + 2));
    CD->data[je_cr + 1] = (uint8_t)(skip_label - (je_cr + 2));

    Emit8(CD, 0x48); Emit8(CD, 0xFF); Emit8(CD, 0xC1);
    { int8_t rb = (int8_t)((int64_t)skip_loop - (int64_t)(CD->size + 2));
      Emit8(CD, 0xEB); Emit8(CD, (uint8_t)rb); }
    
    size_t skip_done = CD->size;
    CD->data[jmp_sd + 1] = (uint8_t)(skip_done - (jmp_sd + 2));

    // check '-' 
    Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0xB6); Emit8(CD, 0x14); Emit8(CD, 0x0E);
    Emit8(CD, 0x80); Emit8(CD, 0xFA); Emit8(CD, 0x2D);
    size_t jne_p = CD->size; Emit8(CD, 0x75); Emit8(CD, 0);
    Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, 0xC3); Emit32(CD, (uint32_t) - 1);
    Emit8(CD, 0x48); Emit8(CD, 0xFF); Emit8(CD, 0xC1);

    // parse_loop 
    size_t parse_loop = CD->size;
    CD->data[jne_p + 1] = (uint8_t)(parse_loop - (jne_p+2));
    Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0xB6); Emit8(CD, 0x14); Emit8(CD, 0x0E);
    Emit8(CD, 0x80); Emit8(CD, 0xEA); Emit8(CD, '0');
    Emit8(CD, 0x80); Emit8(CD, 0xFA); Emit8(CD, 9);
    size_t ja_done = CD->size; Emit8(CD, 0x77); Emit8(CD, 0);
    Emit8(CD, 0x48); Emit8(CD, 0x6B); Emit8(CD, 0xC0); Emit8(CD, 10);
    Emit8(CD, 0x48); Emit8(CD, 0x01); Emit8(CD, 0xD0);
    Emit8(CD, 0x48); Emit8(CD, 0xFF); Emit8(CD, 0xC1);
    { int8_t rb = (int8_t)((int64_t)parse_loop-(int64_t)(CD->size+2));
      Emit8(CD, 0xEB); Emit8(CD, (uint8_t)rb); }
    size_t done = CD->size;
    CD->data[ja_done + 1] = (uint8_t)(done - (ja_done + 2));

    Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0xAF); Emit8(CD, 0xC3); // imul rax,rbx 
    Emit8(CD, 0x59); Emit8(CD, 0x59); Emit8(CD, 0x5B);
    Emit8(CD, 0x5A); Emit8(CD, 0x5E); Emit8(CD, 0x5F);
    Emit8(CD, 0xC3);
}

// exit(0) 
static void EmitBuiltinExit(Context *context) {
    assert(context);
    context->b_exit = CD->size;

    Emit8(CD, 0xB8); Emit32(CD, 60);
    Emit8(CD, 0x48); Emit8(CD, 0x31); Emit8(CD, 0xFF);
    Emit8(CD, 0x0F); Emit8(CD, 0x05);
}

// _start 
static void EmitStart(Context *context) {
    assert(context);
    context->b_start = CD->size;

    Emit8(CD, 0x48); Emit8(CD, 0x83); Emit8(CD, 0xE4); Emit8(CD, 0xF0);
    Emit8(CD, 0x48); Emit8(CD, 0x83); Emit8(CD, 0xEC); Emit8(CD, 0x08);
    Emit8(CD, 0xE8); RelocAdd(context, CD->size, "main", 0); Emit32(CD, 0);
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0xC7);
    Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, 0xC0); Emit32(CD, 60);
    Emit8(CD, 0x0F); Emit8(CD, 0x05);
}

static void BuildData(Context *context) {
    assert(context);
    Buf *data = &context->data;

    context->fmt_int_off = data->size;
    Emit8(data, '%'); Emit8(data, 'l'); Emit8(data, 'l'); Emit8(data, 'd');
    Emit8(data, '\n'); Emit8(data, 0);

    context->fmt_char_off = data->size;
    Emit8(data, '%'); Emit8(data, 'c'); Emit8(data, 0);

    while (data->size % 8) Emit8(data, 0);

    context->ram_off = data->size;
    for (int i = 0; i < RAM_SIZE; i++) {
        Emit64(data, 0);
    }

    context->scan_buf_off = data->size;
    for (int i = 0; i < 16; i++) {
        Emit8(data, 0);
    }
}

static int ResolveDataSym(Context *context, const char *name, size_t *out) { // TODO: redo
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

    if (strcmp(name, "scan_buf") == 0) {
        *out = context->scan_buf_off;
        return 1;
    }

    return 0;
}

static void LinkRelocs(Context *context) {
    assert(context);

    size_t seg1 = HDRS_TOTAL + context->code.size;
    size_t data_off = (seg1 + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    uint64_t data_vaddr = ELF_BASE + data_off;

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

        } else {
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
    fwrite(header, 1, 64, file);

    // PHDR code: PT_LOAD RX = 5 
    uint8_t ph[56] = {};
    { uint32_t value = 1; memcpy(ph, &value, 4); }
    { uint32_t value = 5; memcpy(ph + 4, &value, 4); }
    { uint64_t value = 0; memcpy(ph + 8, &value,8); }
    { uint64_t value = ELF_BASE; memcpy(ph + 16, &value, 8); memcpy(ph + 24, &value, 8); }
    { uint64_t value = seg1; memcpy(ph + 32, &value, 8); memcpy(ph + 40, &value, 8); }
    { uint64_t value = PAGE_SIZE; memcpy(ph + 48, &value, 8); }
    fwrite(ph, 1, 56, file);

    // PHDR data: PT_LOAD RW = 6 
    memset(ph, 0, 56);
    { uint32_t value = 1; memcpy(ph, &value, 4); }
    { uint32_t value = 6; memcpy(ph + 4, &value, 4); }
    memcpy(ph + 8, &data_off, 8);
    memcpy(ph + 16, &data_vaddr,8); memcpy(ph + 24, &data_vaddr, 8);

    { uint64_t value = context->data.size; memcpy(ph + 32, &value, 8); memcpy(ph + 40, &value, 8); }
    { uint64_t value = PAGE_SIZE; memcpy(ph + 48, &value, 8); }
    fwrite(ph, 1, 56, file);

    fwrite(context->code.data, 1, context->code.size, file);

    size_t pad = data_off - seg1;
    if (pad > 0) {
        uint8_t *zeroes = (uint8_t *) calloc (1, pad);
        if (!zeroes) {
            perror("Error calloc.\n");
            return;
        }

        fwrite(zeroes, 1, pad, file);
        free(zeroes);
    }
    fwrite(context->data.data, 1, context->data.size, file);
    fclose(file);
}

typedef struct {
    int ram_base;
    int param_count;
} Sub;

static int IsOp(LangNode_t *node, OperationTypes op) {
    return node && node->type == kOperation && node->value.operation == op;
}
static int CountArgs(LangNode_t *node) {
    if (!node) return 0;
    if (!IsOp(node, kOperationComma)) return 1;

    return CountArgs(node->left) + CountArgs(node->right);
}

static void CleanPos(VariableArr *VariableArr) {
    assert(VariableArr);

    for (size_t i = 0; i < VariableArr->size; i++) {
        VariableArr->var_array[i].pos_in_code = -1;
    }
}

static void MakeLabel(char *buf, size_t size, const char *pre, int n) {
    assert(buf);
    assert(pre);

    snprintf(buf, size, "__%s_%d", pre, n);
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
        #pragma GCC diagnostic pop
    }
}

static int FindVarPos(VariableArr *VariableArr, LangNode_t *node, AsmInfo *info) {
    assert(VariableArr);
    assert(node);
    assert(info);

    int var_idx = -1;
    for (size_t i = 0; i < VariableArr->size; i++) {
        if (strcmp(VariableArr->var_array[i].variable_name, VariableArr->var_array[node->value.pos].variable_name) == 0) {
            if (VariableArr->var_array[i].pos_in_code == -1) {
                var_idx = VariableArr->var_array[i].pos_in_code = info->counter++;
            } else {
                var_idx = VariableArr->var_array[i].pos_in_code;
            }
        }
    }

    return var_idx;
}

static int ResolveShift(VariableArr *VariableArr, LangNode_t *node, AsmInfo *info, Sub *sub) {
    assert(VariableArr);
    assert(node);
    assert(info);
    assert(sub);

    LangNode_t *check = node;
    if (IsOp(node, kOperationGetAddr) || IsOp(node, kOperationCallAddr)) {
        check = node->left;
    }

    int var_idx = -1;
    for (size_t i = 0; i < VariableArr->size; i++) {
        if (VariableArr->var_array[check->value.pos].variable_name &&
            VariableArr->var_array[i].variable_name &&
            strcmp(VariableArr->var_array[i].variable_name, VariableArr->var_array[check->value.pos].variable_name) == 0) {
            if (VariableArr->var_array[i].pos_in_code == -1) {
                var_idx = VariableArr->var_array[i].pos_in_code = info->counter++;
            } else {
                var_idx = VariableArr->var_array[i].pos_in_code;
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

static void CodeGenerateExpr(Context*, LangNode_t*, VariableArr*, AsmInfo*, Sub*);
static void CodeGenerateStatement(Context*, LangNode_t*, VariableArr*, AsmInfo*, Sub*);

// pop rax; [&ram[r12 + shift]] = rax 
static void CodeGeneratePopToVar(Context *context, VariableArr *VariableArr, LangNode_t *node, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(VariableArr);
    assert(node);
    assert(info);
    assert(sub);

    int shift = ResolveShift(VariableArr, node, info, sub);
    EmitPop(context, FindRegCode("rax"));
    EmitVarAddr(context, shift);

    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0x01); // mov [rcx], rax 
}

// rax=[rbp + frame_offset]; [&ram[r12 + shift]] = rax 
static void CodeGenerateStoreParam(Context *context, VariableArr *VariableArr, LangNode_t *node, AsmInfo *info, Sub *sub, int frame_offset) {
    assert(context);
    assert(VariableArr);
    assert(node);
    assert(info);
    assert(sub);

    int shift = ResolveShift(VariableArr, node, info, sub);
    if (frame_offset >= -128 && frame_offset <= 127) {
        Emit8(CD, 0x48); Emit8(CD, 0x8B); Emit8(CD, 0x45);
        Emit8(CD, (uint8_t)(int8_t)frame_offset);
    } else {
        Emit8(CD, 0x48); Emit8(CD, 0x8B); Emit8(CD, 0x85);
        Emit32(CD, (uint32_t)(int32_t)frame_offset);
    }

    EmitVarAddr(context, shift);
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0x01);
}

static void CodeGenerateParamsToRam(Context *context, LangNode_t *args, VariableArr *VariableArr, AsmInfo *info, Sub *sub, int *frame_offset) {
    assert(context);
    assert(VariableArr);
    assert(info);
    assert(sub);
    assert(frame_offset);
    if (!args) return;

    if (!IsOp(args, kOperationComma)) {
        CodeGenerateStoreParam(context, VariableArr, args, info, sub, *frame_offset); *frame_offset += 8; return;
    }

    if (args->left) {
        CodeGenerateParamsToRam(context, args->left,  VariableArr, info, sub, frame_offset);
    }

    if (args->right) {
        CodeGenerateParamsToRam(context, args->right, VariableArr, info, sub, frame_offset);
    }
}

static void CodeGenerateParamsToStack(Context *context, LangNode_t *args, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(VariableArr);
    assert(info);
    assert(sub);
    if (!args) return;

    if (!IsOp(args, kOperationComma)) {
        CodeGenerateExpr(context, args, VariableArr, info, sub);
        return;
    }

    if (args->left) {
        CodeGenerateParamsToStack(context, args->right, VariableArr, info, sub);
    }

    if (args->right) {
        CodeGenerateParamsToStack(context, args->left,  VariableArr, info, sub);
    }
}

static void CodeGenerateAddrOf(Context *context, LangNode_t *var, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(var);
    assert(VariableArr);
    assert(info);
    assert(sub);

    int shift = FindVarPos(VariableArr, var, info) - sub->param_count;
    EmitVarAddr(context, shift);
    EmitPush(context, FindRegCode("rcx"));
}

static void CodeGenerateDeref(Context *context, LangNode_t *ptr, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(ptr);
    assert(VariableArr);
    assert(info);
    assert(sub);

    CodeGenerateAddrOf(context, ptr, VariableArr, info, sub);
    EmitPop(context, FindRegCode("rcx"));
    Emit8(CD, 0xFF); Emit8(CD, 0x31); // push [rcx] 
}

static void CodeGenerateAddrAssign(Context *context, LangNode_t *dn, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(dn);
    assert(VariableArr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, dn->left, VariableArr, info, sub);
    EmitPop(context, 1);
    EmitPop(context, 0);
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0x01);
}

static void CodeGenerateBinOp(Context *context, LangNode_t *node, VariableArr *VariableArr, AsmInfo *info, Sub *sub, OperationTypes op) {
    assert(context);
    assert(node);
    assert(VariableArr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left,  VariableArr, info, sub);
    CodeGenerateExpr(context, node->right, VariableArr, info, sub);
    EmitPop(context, FindRegCode("rbx"));
    EmitPop(context, FindRegCode("rax"));

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (op) {
        case kOperationAdd:
            Emit8(CD, 0x48); Emit8(CD, 0x01); Emit8(CD, ModRM(3,3,0));
            break;

        case kOperationSub:
            Emit8(CD, 0x48); Emit8(CD, 0x29); Emit8(CD, ModRM(3,3,0));
            break;

        case kOperationMul:
            Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0xAF); Emit8(CD, ModRM(3,0,3));
            break;

        case kOperationDiv:
            Emit8(CD, 0x48); Emit8(CD, 0x99); // cqo 
            Emit8(CD, 0x48); Emit8(CD, 0xF7); Emit8(CD, ModRM(3,7,3));
            break;

        default:
            break;
        #pragma GCC diagnostic pop
    }

    EmitPush(context, FindRegCode("rax"));
}

static void CodeGeneratePrintInt(Context *context, LangNode_t *node, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(VariableArr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, VariableArr, info, sub);
    EmitPop(context, FindRegCode("rsi"));
    Emit8(CD, 0x49); Emit8(CD, 0x89); Emit8(CD, 0xE5); // mov r13, rsp

    EmitAlignStack(context);
    EmitXorEax(context);
    EmitCall(context, "my_printf");
    Emit8(CD, 0x4C); Emit8(CD, 0x89); Emit8(CD, 0xEC); // mov rsp, r13 
}

static void CodeGeneratePrintChar(Context *context, LangNode_t *node, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(VariableArr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, VariableArr, info, sub);
    EmitPop(context, FindRegCode("rsi"));
    Emit8(CD, 0x49); Emit8(CD, 0x89); Emit8(CD, 0xE5);

    EmitAlignStack(context);
    EmitXorEax(context);
    EmitCall(context, "my_printf_char");
    Emit8(CD, 0x4C); Emit8(CD, 0x89); Emit8(CD, 0xEC);
}

static void CodeGenerateReadInt(Context *context) {
    assert(context);

    EmitCall(context, "my_scanf");
    EmitPush(context, FindRegCode("rax"));
}

static void CodeGenerateArrAssign(Context *context, LangNode_t *stmt, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(VariableArr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, stmt->right, VariableArr, info, sub);

    int ab = FindVarPos(VariableArr, stmt->left->left, info) - sub->param_count;
    EmitMovData(context, FindRegCode("rcx"), "ram");
    EmitMovRR(context, FindRegCode("rdi"), FindRegCode("r12"));
    EmitAddRegImm(context, 7, (int64_t)ab);

    CodeGenerateExpr(context, stmt->left->right, VariableArr, info, sub);
    EmitPop(context, FindRegCode("rax"));
    Emit8(CD, 0x48); Emit8(CD, 0x01); Emit8(CD, ModRM(3, 0, 7)); // add rdi, rax 
    EmitPop(context, FindRegCode("rax"));
    Emit8(CD, 0x48); Emit8(CD, 0x89); Emit8(CD, 0x04); Emit8(CD, Sib(3, 7, 1));
}

static void CodeGenerateArrDecl(Context *context, LangNode_t *stmt, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(VariableArr);
    assert(info);
    assert(sub);

    VariableArr->var_array[stmt->left->left->left->value.pos].pos_in_code = info->counter;
    int size = (int)stmt->left->left->right->value.number;

    for (int i = 0; i < size; i++) {
        int shift = info->counter + i - sub->param_count;
        EmitVarAddr(context, shift);
        Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, 0x01); Emit32(CD, 0);
    }

    info->counter += size;
}

static void CodeGenerateIf(Context *context, LangNode_t *stmt, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(VariableArr);
    assert(info);
    assert(sub);

    LangNode_t *cond = stmt->left;
    int ni = info->label_if++, ne = info->label_else++;
    int has_else = IsOp(stmt->right, kOperationElse);
    char lbl_else[64] = {}, lbl_end[64] = {};
    MakeLabel(lbl_else, sizeof(lbl_else), "else", ne);
    MakeLabel(lbl_end, sizeof(lbl_end), "end_if", ni);

    CodeGenerateExpr(context, cond->left, VariableArr, info, sub);
    CodeGenerateExpr(context, cond->right, VariableArr, info, sub);
    EmitPop(context, 3);
    EmitPop(context, 0);
    EmitCmpRaxRbx(context);
    EmitJCC(context, ChooseJCC(cond), lbl_else);

    if (has_else) {
        CodeGenerateStatement(context, stmt->right->left, VariableArr, info, sub);
    } else {
        CodeGenerateStatement(context, stmt->right, VariableArr, info, sub);
    }

    EmitJmp(context, lbl_end);
    LabelAdd(context, lbl_else, CD->size);
    if (has_else) {
        CodeGenerateStatement(context, stmt->right->right, VariableArr, info, sub);
    }

    LabelAdd(context, lbl_end, CD->size);
}

static void CodeGenerateWhile(Context *context, LangNode_t *stmt, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(VariableArr);
    assert(info);
    assert(sub);

    int sn = info->label_counter++, en = info->label_counter++;
    char ls[ELF_HEADER_SIZE] = {}, le[ELF_HEADER_SIZE] = {};
    MakeLabel(ls, sizeof(ls), "wstart", sn);
    MakeLabel(le, sizeof(le), "wend", en);

    LabelAdd(context, ls, CD->size);
    CodeGenerateExpr(context, stmt->left->left, VariableArr, info, sub);
    CodeGenerateExpr(context, stmt->left->right, VariableArr, info, sub);
    EmitPop(context, 3);
    EmitPop(context, 0);

    EmitCmpRaxRbx(context);
    EmitJCC(context, ChooseJCC(stmt->left), le);
    CodeGenerateStatement(context, stmt->right, VariableArr, info, sub);
    EmitJmp(context, ls);
    LabelAdd(context, le, CD->size);
}

static void CodeGenerateReturn(Context *context, LangNode_t *stmt, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(VariableArr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, stmt->left, VariableArr, info, sub);
    EmitPop(context, 0);
    EmitEpilogue(context);
    EmitRet(context);
}

static void CodeGenerateExpr(Context *context, LangNode_t *expr, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(VariableArr);
    assert(info);
    assert(sub);
    if (!expr) return;

    switch (expr->type) {
        case kNumber: {
            int64_t n = (int64_t)expr->value.number;
            if (n >= 0 && n <= 0x7FFFFFFF) {
                Emit8(CD, 0x48); Emit8(CD, 0xC7); Emit8(CD, ModRM(3,0,0));
                Emit32(CD, (uint32_t)(int32_t)n);
            } else {
                EmitMovR64Imm64(context, 0, n);
            }
            EmitPush(context, 0);
            break;
        }
        
        case kVariable: {
            int shift = FindVarPos(VariableArr, expr, info) - sub->param_count;
            EmitVarAddr(context, shift);
            Emit8(CD, 0xFF); Emit8(CD, 0x31); // push [rcx] 
            break;
        }
        
        case kOperation:
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wswitch-enum"
            switch (expr->value.operation) {
                case kOperationAdd:
                    CodeGenerateBinOp(context, expr, VariableArr, info, sub, kOperationAdd);
                    break;

                case kOperationSub:
                    CodeGenerateBinOp(context, expr, VariableArr, info, sub, kOperationSub);
                    break;

                case kOperationMul:
                    CodeGenerateBinOp(context, expr, VariableArr,info,  sub,kOperationMul);
                    break;

                case kOperationDiv:
                    CodeGenerateBinOp(context, expr, VariableArr, info, sub, kOperationDiv);
                    break;

                case kOperationSQRT:
                    CodeGenerateExpr(context, expr->left, VariableArr, info, sub);
                    EmitPop(context, 0);
                    // cvtsi2sd xmm0, rax; sqrtsd xmm0, xmm0; cvttsd2si rax, xmm0 
                    Emit8(CD, 0xF2); Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0x2A); Emit8(CD, 0xC0);
                    Emit8(CD, 0xF2); Emit8(CD, 0x0F); Emit8(CD, 0x51); Emit8(CD, 0xC0);
                    Emit8(CD, 0xF2); Emit8(CD, 0x48); Emit8(CD, 0x0F); Emit8(CD, 0x2C); Emit8(CD, 0xC0);
                    EmitPush(context, 0);
                    break;

                case kOperationCallAddr:
                    CodeGenerateAddrOf(context, expr->left, VariableArr, info, sub);
                    break;

                case kOperationGetAddr:
                    CodeGenerateDeref(context, expr->left, VariableArr, info, sub);
                    break;

                case kOperationCall: {
                    const char *callee = VariableArr->var_array[expr->left->value.pos].variable_name;
                    int na = CountArgs(expr->right);
                    CodeGenerateParamsToStack(context, expr->right, VariableArr, info, sub);
                    EmitCall(context, callee);
                    if (na > 0) EmitAddRegImm(context, FindRegCode("rsp"), (int64_t)(na * 8));
                    EmitPush(context, 0);
                    break;
                }

                case kOperationArrPos: {
                    int ab = FindVarPos(VariableArr, expr->left, info) - sub->param_count;
                    EmitMovData(context, FindRegCode("rcx"), "ram");
                    EmitMovRR(context, FindRegCode("rdi"), FindRegCode("r12"));
                    EmitAddRegImm(context, 7, (int64_t)ab);

                    CodeGenerateExpr(context, expr->right, VariableArr, info, sub);
                    EmitPop(context, FindRegCode("rax"));
                    Emit8(CD, 0x48); Emit8(CD, 0x01); Emit8(CD, ModRM(3, 0, 7));

                    // push [rcx + rdi * 8] 
                    Emit8(CD, 0xFF); Emit8(CD, 0x04); Emit8(CD, Sib(3, 7, 1));
                    break;
                }

                default:
                    break;
                #pragma GCC diagnostic pop
                
            }

            break;

        default: 
            break;
    }
}

static void CodeGenerateStatement(Context *context, LangNode_t *stmt, VariableArr *VariableArr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(VariableArr);
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
                    CodeGenerateAddrOf(context, stmt->left, VariableArr, info, sub);
                        break;

                case kOperationGetAddr:
                    CodeGenerateDeref(context, stmt->left, VariableArr, info, sub);
                        break;

                case kOperationCall: {
                    const char *callee = VariableArr->var_array[stmt->left->value.pos].variable_name;
                    int na = CountArgs(stmt->right);
                    CodeGenerateParamsToStack(context, stmt->right, VariableArr, info, sub);
                    EmitCall(context, callee);
                    if (na > 0) {
                        EmitAddRegImm(context, 4, (int64_t)(na * 8));
                    }

                    break;
                }

                case kOperationIs:
                    if (IsOp(stmt->left, kOperationArrPos)) {
                        CodeGenerateArrAssign(context, stmt, VariableArr, info, sub);
                        break;
                    }

                    CodeGenerateExpr(context, stmt->right, VariableArr, info, sub);
                    if (IsOp(stmt->left, kOperationGetAddr)) {
                        CodeGenerateAddrAssign(context, stmt, VariableArr, info, sub);
                        break;
                    }
                    CodeGenerateStatement(context, stmt->left, VariableArr, info, sub);
                    break;

                case kOperationReturn:
                    CodeGenerateReturn(context, stmt, VariableArr, info, sub);
                    break;

                case kOperationWrite:
                    CodeGeneratePrintInt (context, stmt, VariableArr, info, sub);
                    break;

                case kOperationWriteChar:
                    CodeGeneratePrintChar(context, stmt, VariableArr, info, sub);
                    break;

                case kOperationRead:
                    CodeGenerateReadInt(context);
                    CodeGeneratePopToVar(context, VariableArr, stmt->left, info, sub);
                    break;

                case kOperationThen:
                    CodeGenerateStatement(context, stmt->left,  VariableArr, info, sub);
                    CodeGenerateStatement(context, stmt->right, VariableArr, info, sub);
                    break;

                case kOperationIf: 
                    CodeGenerateIf(context, stmt, VariableArr, info, sub);
                    break;

                case kOperationWhile: 
                    CodeGenerateWhile(context, stmt, VariableArr, info, sub);
                    break;

                case kOperationTernary:
                    CodeGenerateStatement(context, stmt->left->right, VariableArr, info, sub);
                    CodeGenerateStatement(context, stmt->left->left, VariableArr, info, sub);
                    break;

                case kOperationArrDecl: 
                    CodeGenerateArrDecl(context,stmt,VariableArr,info,sub);
                    break;

                case kOperationDraw: // TODO
                    break;

                default:
                    CodeGenerateExpr(context, stmt, VariableArr, info, sub);
                    break;
            }
            #pragma GCC diagnostic pop
            break;

        case kVariable:
            CodeGeneratePopToVar(context, VariableArr, stmt, info, sub);
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

static void CodeGenerateFunction(Context *context, LangNode_t *fn, VariableArr *VariableArr, int *ram_base, AsmInfo *info) {
    assert(context);
    assert(VariableArr);
    assert(ram_base);
    assert(info);
    if (!fn) return;

    CleanPos(VariableArr);
    
    info->counter = 0;

    LangNode_t *args = fn->right->left;
    const char *fname = VariableArr->var_array[fn->left->value.pos].variable_name;
    int is_main = (strcmp(MAIN, fname) == 0);

    LabelAdd(context, fname, CD->size);
    if (is_main) LabelAdd(context, "main", CD->size);

    Sub sub = {*ram_base, 0 };
    EmitPrologue(context);

    if (is_main) {
        // xor r12d, r12d 
        Emit8(CD, 0x45); Emit8(CD, 0x31); Emit8(CD, 0xE4);
    }

    int pc = VariableArr->var_array[fn->left->value.pos].variable_value;
    sub.param_count = pc;
    if (pc > 0) {
        EmitAddRegImm(context, 12, (int64_t)pc);
    }

    int frame_offset = 16;
    if (args) {
        CodeGenerateParamsToRam(context, args, VariableArr, info, &sub, &frame_offset);
    }

    *ram_base += pc;
    sub.ram_base = *ram_base;

    CodeGenerateStatement(context, fn->right->right, VariableArr, info, &sub);

    *ram_base -= pc;
    EmitEpilogue(context);

    if (is_main) {
        EmitCall(context, "my_exit");
    } else { 
        EmitRet(context);
    }
}

static void CodeGenerateProgram(Context *context, LangNode_t *root, VariableArr *VariableArr, int *ram_base, AsmInfo *info) {
    assert(context);
    assert(VariableArr);
    assert(ram_base);
    assert(info);
    if (!root) return;

    info->counter = 0;
    if (IsOp(root, kOperationFunction)) {
        CodeGenerateFunction(context, root, VariableArr, ram_base, info);
    }

    if (root->left) {
        CodeGenerateProgram(context, root->left,  VariableArr, ram_base, info);
    }

    if (root->right) {
        CodeGenerateProgram(context, root->right, VariableArr, ram_base, info);
    }
}

void CompileTreeToELF(LangNode_t *root, VariableArr *arr, const char *elf_path) {
    assert(root);
    assert(arr);
    assert(elf_path);

    Context context = {};
    ContextInit(&context);
    BuildData(&context);

    EmitBuiltinPrintf(&context);
    EmitBuiltinPrintfChar(&context);
    EmitBuiltinScanf(&context);
    EmitBuiltinExit(&context);
    EmitStart(&context);

    LabelAdd(&context, "my_printf", context.b_printf);
    LabelAdd(&context, "my_printf_char", context.b_printf_char);
    LabelAdd(&context, "my_scanf", context.b_scanf);
    LabelAdd(&context, "my_exit", context.b_exit);

    AsmInfo info = {};
    int ram_base = 0;
    CodeGenerateProgram(&context, root, arr, &ram_base, &info);

    LinkRelocs(&context);
    WriteElf(&context, elf_path);

    printf("Compiled: %s  (code=%zu  data=%zu)\n", elf_path, context.code.size, context.data.size);

    ContextFree(&context);
}