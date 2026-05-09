/*
 * TreeToBin.c: переводит AST в готовый ELF-файл под Linux x86-64
 *
 * Все байтовые константы, формулы REX/ModRM/SIB и форматы инструкций
 * соответствуют методичке `x86_64_bytecode.md` (см. репозиторий)
 * В комментариях ниже ссылки вида "методичка: гл. 3.4" указывают на
 * нужную главу, там лежит разбор соответствующего опкода
 *
 * Что делает код по шагам:
 *   1. ContextInit / BuildData / BuildGOT: буферы кода/данных,
 *      форматы printf и пустые слоты GOT (методичка: гл. 17, 18, 19)
 *   2. LoadLib: читает my_lib.elf, достаёт оттуда .text, символы и
 *      релокации стандартных функций (методичка: гл. 19)
 *   3. MergeAndPatchLibrary: приклеивает .text библиотеки к нашему
 *      сегменту кода и правит релокации под новый vaddr
 *   4. EmitPLT: `jmp [rip+got_slot]` для каждой стандартной функции
 *      (методичка: гл. 6.1, 12.3)
 *   5. EmitStart: точка входа, выравниваем стек, call main, exit
 *   6. CodeGenerateProgram: проходим по дереву и пишем инструкции
 *   7. FinalizeAndWrite: заполняем GOT, патчим релокации,
 *      записываем ELF на диск (методичка: гл. 17, 18)
 *
 * Как работают вызовы функций:
 *   - аргументы передаются через стек (push в обратном порядке);
 *   - возвращаемое значение лежит в rax;
 *   - r13: туда сохраняем rsp вокруг вызовов в библиотеку;
 *   - rcx: scratch для адресации переменных через rbp
 *
 * Расклад кадра (см. также EmitVarAddrBySlot):
 *   [rbp + 16 + 8*i]: i-й параметр
 *   [rbp + 8]:        return address
 *   [rbp]:            saved caller's rbp
 *   [rbp - 8*1..]:    локальные переменные / массивы
 */

#include "Back-End/TreeToBin.h"

#include <assert.h>
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/CommonFunctions.h"
#include "Common/CommonBackFunctions.h"
#include "Back-End/DSL.h"

#define CODE (&context->code)

#define STANDART_FUNCTIONS_NUMBER 4
#define REGS_NUMBER 35

// Таблица всех регистров x86-64 (имя, код, ширина в битах)
// См. методичка, гл. 7
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

static int LoadLib(LibBlob *blob, const char *path);
static void ContextFree(Context *context);
static void MergeAndPatchLibrary(Context *context, LibBlob *blob, size_t *blob_base);
static void FinalizeAndWrite(Context *context, PltGot *plt_got, const char *elf_path);
static void FreeLib(LibBlob *blob);

static void ContextInit(Context *context);
static void BuildData(Context *context);
static void BuildGOT(Context *context, PltGot *plt_got);
static void EmitPLT(Context *context, PltGot *plt_got);
static void EmitStart(Context *context);
static void CodeGenerateProgram(Context *context, LangNode_t *root, VariableArr *arr, AsmInfo *info);

// Точка входа: компилирует AST в готовый ELF по пути elf_path
void CompileTreeToELF(LangNode_t *root, VariableArr *arr, const char *elf_path) {
    assert(root);
    assert(arr);
    assert(elf_path);

    Context context = {};
    PltGot plt_got = {};
    LibBlob blob = {};

    ContextInit(&context);
    BuildData(&context);
    BuildGOT(&context, &plt_got);

    if (!LoadLib(&blob, "my_lib.elf")) {
        ContextFree(&context);
        return;
    }

    size_t blob_base = 0;
    MergeAndPatchLibrary(&context, &blob, &blob_base);

    EmitPLT(&context, &plt_got);
    EmitStart(&context);

    AsmInfo info = {};
    CodeGenerateProgram(&context, root, arr, &info);

    FinalizeAndWrite(&context, &plt_got, elf_path);

    FreeLib(&blob);
    ContextFree(&context);
}

static void BufGrow(Buf *buf, size_t need);
static void Patch64(Buf *buf, size_t offset, uint64_t value);

// Приклеиваем .text загруженной библиотеки к нашему сегменту кода и
// пересчитываем её абсолютные релокации под новый vaddr
static void MergeAndPatchLibrary(Context *context, LibBlob *blob, size_t *blob_base) {
    assert(context);
    assert(blob);
    assert(blob_base);

    *blob_base = context->code.size;

    BufGrow(CODE, blob->size);
    memcpy(context->code.data + context->code.size, blob->data, blob->size);
    context->code.size += blob->size;

    context->b_printf = *blob_base + blob->printf_off;
    context->b_scanf  = *blob_base + blob->scanf_off;
    context->b_exit   = *blob_base + blob->exit_off;
    context->b_draw   = *blob_base + blob->draw_off;

    // Сдвигаем абсолютные адреса внутри .text с link-time vaddr на наш
    uint64_t blob_vaddr = ELF_BASE + HDRS_TOTAL + *blob_base;
    for (uint32_t i = 0; i < blob->reloc_count; i++) {
        size_t patch_offset = *blob_base + blob->relocs[i];
        uint64_t current_value = 0;
        memcpy(&current_value, context->code.data + patch_offset, 8);
        Patch64(CODE, patch_offset, current_value - blob->link_base + blob_vaddr);
    }
}

static void LinkRelocs(Context *context, PltGot *plt_got, uint64_t data_vaddr);
static void WriteElf(Context *context, const char *path);

// Заполняем GOT адресами стандартных функций, обрабатываем все
// накопленные релокации и пишем готовый ELF на диск
static void FinalizeAndWrite(Context *context, PltGot *plt_got, const char *elf_path) {
    assert(context);
    assert(plt_got);
    assert(elf_path);

    size_t seg1 = HDRS_TOTAL + context->code.size;
    size_t data_offset = (seg1 + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    uint64_t data_vaddr = ELF_BASE + data_offset;
    uint64_t base = ELF_BASE + HDRS_TOTAL;

    // Записываем в подготовленные слоты GOT реальные vaddr функций
    uint64_t addrs[STANDART_FUNCTIONS_NUMBER] = {
        base + context->b_printf,
        base + context->b_scanf,
        base + context->b_exit,
        base + context->b_draw,
    };

    for (int i = 0; i < STANDART_FUNCTIONS_NUMBER; i++) {
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

// Дописываем в конец буфера 1/4/8 байт (little-endian, методичка: гл. 15)

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

// Перезаписываем 4/8 байт по фиксированному смещению (для патчинга
// forward-переходов и релокаций; методичка: гл. 12.2)

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
    BufInit(CODE, 131072);
    BufInit(&context->data, 512);
}

static void ContextFree(Context *context) {
    assert(context);
    BufFree(CODE);
    BufFree(&context->data);
}

// Регистрируем метку: имя и текущее смещение в буфере кода
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

// Регистрируем релокацию (rel32 / abs64 / GOT-rel32) на символ
// с именем name; реальные значения подставит LinkRelocs
// См. методичка: гл. 12.2 (patch-холдеры)
static void RelocAdd(Context *context, size_t offset, const char *name, RelocType type) {
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

// REX с включённым W-битом + R/B по необходимости. См. методичка, гл. 3.2
static uint8_t RexW(int reg, int rm) {
    uint8_t result = REX_W_BYTE;

    if (reg >= 8) result |= REX_R_BIT;
    if (rm  >= 8) result |= REX_B_BIT;

    return result;
}


// mov r64, imm64  (REX.W + B8+r + imm64). Методичка: гл. 8.2
static void EmitMovR64Imm64(Context *context, int reg, int64_t value) {
    assert(context);

    uint8_t rex = REX_W_BYTE;
    if (reg >= 8) rex |= REX_B_BIT;

    Emit8(CODE, rex);
    Emit8(CODE, (uint8_t)(OP_MOV_R64_IMM64 + (reg & 7)));
    Emit64(CODE, (uint64_t)value);
}

// push r64. Методичка: гл. 9.1, 9.2
static void EmitPush(Context *context, int reg) {
    assert(context);

    if (reg >= 8) Emit8(CODE, REX_B_BYTE);
    Emit8(CODE, (uint8_t)(OP_PUSH_R + (reg & 7)));
}

// pop r64. Методичка: гл. 9.1, 9.2
static void EmitPop(Context *context, int reg) {
    assert(context);

    if (reg >= 8) Emit8(CODE, REX_B_BYTE);
    Emit8(CODE, (uint8_t)(OP_POP_R + (reg & 7)));
}

// mov dst, src  (REX.W 89 /r). Методичка: гл. 8.4
static void EmitMovRR(Context *context, int dst, int src) {
    assert(context);

    Emit8(CODE, RexW(src, dst));
    Emit8(CODE, OP_MOV_RM_R);
    Emit8(CODE, ModRM(3, src, dst));
}

// add/sub reg, imm: сами выбираем ширину imm8 или imm32
// /0 = add, /5 = sub. Методичка: гл. 10.1, 10.2
static void EmitAddRegImm(Context *context, int reg, int64_t imm) {
    assert(context);
    if (imm == 0) return;

    uint8_t slash = (imm > 0) ? 0 : 5;
    int64_t abs_value = (imm > 0) ? imm : -imm;

    if (abs_value <= 127) {
        BYTE(RexW(0, reg));
        BYTE(OP_ADD_RM_IMM8);
        BYTE(ModRM(3, slash, reg));
        BYTE(abs_value);
    } else {
        BYTE(RexW(0, reg));
        BYTE(OP_ADD_RM_IMM32);
        BYTE(ModRM(3, slash, reg));
        DWORD(abs_value);
    }
}

// call rel32: оставляем плейсхолдер 0, регистрируем kRel32-релокацию
// Методичка: гл. 12 (общее), 12.2 (патчинг)
static void EmitCall(Context *context, const char *name) {
    assert(context);
    assert(name);

    BYTE(OP_CALL_REL32);
    RelocAdd(context, CODE->size, name, kRel32);
    DWORD(0);
}

// jmp rel32. Методичка: гл. 12
static void EmitJmp(Context *context, const char *name) {
    assert(context);
    assert(name);

    BYTE(OP_JMP_REL32);
    RelocAdd(context, CODE->size, name, kRel32);
    DWORD(0);
}

// jcc rel32 (0F 8X). cc выбирается в ChooseJCC. Методичка: гл. 12.1
static void EmitJCC(Context *context, uint8_t cc, const char *name) {
    assert(context);
    assert(name);

    BYTE(OP_TWO_BYTE_PREFIX); BYTE(cc);
    RelocAdd(context, CODE->size, name, kRel32);
    DWORD(0);
}

// mov reg, imm64 с релокацией kAbs64 на символ в data-сегменте
// (адрес дописывается в LinkRelocs)
static void EmitMovData(Context *context, int reg, const char *symbol) {
    assert(context);
    assert(symbol);

    MOV_R_IMM64(reg, 0);
    RelocAdd(context, CODE->size - 8, symbol, kAbs64);
}

// and rsp, -16: выравниваем стек на 16 байт перед call
// Методичка: гл. 14.3
static void EmitAlignStack(Context *context) {
    assert(context);

    BYTE(RexW(0, kRSP));
    BYTE(OP_ADD_RM_IMM8);
    BYTE(ModRM(3, 4, kRSP));    // /4 = and
    BYTE(IMM8_MINUS_16);
}

// ret. Методичка: гл. 13
static void EmitRet(Context *context) {
    assert(context);
    BYTE(OP_RET);
}

/*
 * Адресация переменных в кадре функции
 *
 *   [rbp + 16 + 8 * i] -- i-й параметр (i = 0 .. param_count - 1)
 *   [rbp + 8]          -- return address
 *   [rbp]              -- saved caller's rbp
 *   [rbp - 8 * 1]      -- 0-я локальная ячейка
 *   ...
 *   [rbp - frame_size] -- последняя локальная ячейка
 *
 * Slot (pos_in_code):
 *   0 .. param_count-1: параметры       ([rbp + 16 + 8*slot])
 *   >= param_count    : локальные       ([rbp - 8*((slot - param_count) + 1)])
 */

// lea rcx, [rbp + disp]. Используем disp8 если |disp| <= 127, иначе disp32
// Методичка: гл. 11 (LEA), 4.3 (mod)
static void EmitLeaRcxRbp(Context *context, int32_t disp) {
    assert(context);

    BYTE(RexW(kRCX, kRBP));
    BYTE(OP_LEA);
    if (disp >= -128 && disp <= 127) {
        BYTE(ModRM(1, kRCX, kRBP));             // mod=1, disp8
        BYTE((int8_t)disp);
    } else {
        BYTE(ModRM(2, kRCX, kRBP));             // mod=2, disp32
        DWORD(disp);
    }
}

// rcx = адрес ячейки переменной по её slot'у
static void EmitVarAddrBySlot(Context *context, int slot, int param_count) {
    assert(context);

    if (slot < param_count) {
        int32_t disp = 16 + 8 * slot;
        LEA_RCX_RBP(disp);
    } else {
        int local_index = slot - param_count;
        int32_t disp = -8 * (local_index + 1);
        LEA_RCX_RBP(disp);
    }
}

// push rbp; mov rbp, rsp; sub rsp, frame_size
// Методичка: гл. 14.5
static void EmitPrologue(Context *context, int frame_size) {
    assert(context);

    PUSH(kRBP);
    MOV_RR(kRBP, kRSP);
    if (frame_size > 0) {
        ADD_R_IMM(kRSP, -(int64_t)frame_size);
    }
}

// mov rsp, rbp; pop rbp. Методичка: гл. 14.5
static void EmitEpilogue(Context *context) {
    assert(context);

    MOV_RR(kRSP, kRBP);
    POP(kRBP);
}

// Точка входа _start: выравнивает стек, зовёт main, передаёт код
// возврата в my_exit (PLT-стаб, оттуда в libmy.exit, потом syscall)
static void EmitStart(Context *context) {
    assert(context);

    context->b_start = CODE->size;

    ALIGN_STACK();
    ADD_R_IMM(kRSP, -8);                // выравнивание под call

    CALL("main");                       // rax = main()
    MOV_RR(kRDI, kRAX);                 // rdi = код возврата
    MOV_R_IMM32(kRAX, 60);              // rax = SYS_exit (резерв, см. my_exit)

    CALL("my_exit");                    // exit через библиотеку
}


// Резервируем 4 слота GOT по 8 байт. Реальные адреса
// вписываются в FinalizeAndWrite. Методичка: гл. 19.4 (.got)
static void BuildGOT(Context *context, PltGot *plt_got) {
    assert(context);
    assert(plt_got);

    Buf *data = &context->data;
    while (data->size % 8) {
        BYTE(0);
    }

    plt_got->got_off = data->size;

    Emit64(data, 0);
    Emit64(data, 1);
    Emit64(data, 2);
    Emit64(data, 3);
}

// Кладём в data-сегмент строковые форматы для printf:
// "%d\0" и "%c\0". Их offset'ы лежат в context->fmt_*_off
static void BuildData(Context *context) {
    assert(context);

    Buf *data = &context->data;

    context->fmt_int_off = data->size;
    Emit8(data, '%');  Emit8(data, 'd'); Emit8(data, 0);

    context->fmt_char_off = data->size;
    Emit8(data, '%');  Emit8(data, 'c'); Emit8(data, 0);

    while (data->size % 8) {
        Emit8(data, 0);
    }
}

// PLT-stub: jmp qword [rip + got_slot]. После загрузки в GOT уже
// лежит реальный адрес функции. Методичка: гл. 6.1, 12.3
static void EmitPltStub(Context *context, const char *label_name, const char *got_name, size_t *out_plt_off) {
    assert(context);
    assert(label_name);
    assert(got_name);
    assert(out_plt_off);

    *out_plt_off = CODE->size;
    LabelAdd(context, label_name, *out_plt_off);

    JMP_RIP_REL32();                                         // FF 25 ...
    RelocAdd(context, CODE->size, got_name, kGOTRel32);
    DWORD(0);
}

// Делаем PLT-стаб для каждой стандартной функции
static void EmitPLT(Context *context, PltGot *plt_got) {
    assert(context);
    assert(plt_got);

    EmitPltStub(context, "my_printf", "__got_printf", &plt_got->plt_printf);
    EmitPltStub(context, "my_scanf",  "__got_scanf",  &plt_got->plt_scanf);
    EmitPltStub(context, "my_exit",   "__got_exit",   &plt_got->plt_exit);
    EmitPltStub(context, "my_draw",   "__got_draw",   &plt_got->plt_draw);
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

// По имени символа в релокации возвращаем его абсолютный vaddr
// Поддерживает: GOT-слоты, символы из data-сегмента, метки в коде
static uint64_t GetSymbolAddress(Context *context, PltGot *plt_got, Relocation *rel, uint64_t data_vaddr) {
    assert(context);
    assert(plt_got);
    assert(rel);

    if (rel->type == kGOTRel32) {
        const char *got_names[] = {"__got_printf", "__got_scanf", "__got_exit", "__got_draw"};

        for (size_t i = 0; i < 4; i++) {
            if (strcmp(rel->name, got_names[i]) == 0) {
                return data_vaddr + plt_got->got_off + i * 8;
            }
        }

        return 0;
    }

    size_t data_off = 0;
    if (ResolveDataSym(context, rel->name, &data_off)) {
        return data_vaddr + data_off;
    }

    int label_index = LabelFind(context, rel->name);
    if (label_index >= 0) {
        return ELF_BASE + HDRS_TOTAL + context->labels[label_index].offset;
    }

    return 0;
}

// Проходим по всем накопленным релокациям и подставляем настоящие адреса:
//   kRel32:    call/jmp/jcc rel32 и RIP-relative
//   kAbs64:    abs64 (для mov r64, imm64 с символом)
//   kGOTRel32: RIP-relative до конкретного GOT-слота
// Методичка: гл. 12 (rel32), гл. 6 (RIP-relative)
static void LinkRelocs(Context *context, PltGot *plt_got, uint64_t data_vaddr) {
    assert(context);
    assert(plt_got);

    for (int i = 0; i < context->number_relocs; i++) {
        Relocation *rel = &context->relocs[i];
        uint64_t sym_addr = GetSymbolAddress(context, plt_got, rel, data_vaddr);
        uint64_t patch_rip = ELF_BASE + HDRS_TOTAL + rel->offset + 4;   // +4: после rel32

        if (rel->type == kRel32) {
            if (!sym_addr) {
                fprintf(stderr, "Undefined: %s.\n", rel->name);
                continue;
            }

            Patch32(CODE, rel->offset, (uint32_t)(sym_addr - patch_rip));
        } else if (rel->type == kAbs64) {
            Patch64(CODE, rel->offset, sym_addr);
        } else if (rel->type == kGOTRel32) {
            if (!sym_addr) {
                fprintf(stderr, "Unknown GOT sym: %s.\n", rel->name);
                continue;
            }

            Patch32(CODE, rel->offset, (uint32_t)(sym_addr - patch_rip));
        }
    }
}


static void WriteElfHeader(FILE *file, uint64_t entry);
static void WriteCodeSegmentPhdr(FILE *file, size_t seg1);
static void WriteDataSegmentPhdr(FILE *file, size_t data_off, uint64_t data_vaddr, size_t data_size);
static void WritePadding(FILE *file, size_t pad);

// Собираем финальный ELF: Ehdr + 2 Phdr + код + padding + data
// Методичка: гл. 16.3, 21.1
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

    WritePadding(file, data_off - seg1);                    // выравнивание data на страницу
    fwrite(context->data.data, 1, context->data.size, file);

    fclose(file);
}

// Elf64_Ehdr: магия + класс + точка входа + offset Phdr. Методичка: гл. 17
static void WriteElfHeader(FILE *file, uint64_t entry) {
    assert(file);

    Elf64_Ehdr eh = {};

    eh.e_ident[EI_MAG0]    = ELFMAG0;          // 0x7F
    eh.e_ident[EI_MAG1]    = ELFMAG1;          // 'E'
    eh.e_ident[EI_MAG2]    = ELFMAG2;          // 'L'
    eh.e_ident[EI_MAG3]    = ELFMAG3;          // 'F'
    eh.e_ident[EI_CLASS]   = ELFCLASS64;
    eh.e_ident[EI_DATA]    = ELFDATA2LSB;      // little-endian
    eh.e_ident[EI_VERSION] = EV_CURRENT;
    eh.e_ident[EI_OSABI]   = ELFOSABI_SYSV;

    eh.e_type      = ET_EXEC;
    eh.e_machine   = EM_X86_64;
    eh.e_version   = EV_CURRENT;
    eh.e_entry     = entry;
    eh.e_phoff     = sizeof(Elf64_Ehdr);
    eh.e_shoff     = 0;                        // секций нет
    eh.e_flags     = 0;
    eh.e_ehsize    = sizeof(Elf64_Ehdr);
    eh.e_phentsize = sizeof(Elf64_Phdr);
    eh.e_phnum     = 2;                        // code + data
    eh.e_shentsize = 0;
    eh.e_shnum     = 0;
    eh.e_shstrndx  = 0;

    fwrite(&eh, sizeof(eh), 1, file);
}

// PT_LOAD для кода (R/W/X). Методичка: гл. 18.2, 18.3
static void WriteCodeSegmentPhdr(FILE *file, size_t seg1) {
    assert(file);

    Elf64_Phdr page_header = {};

    page_header.p_type   = PT_LOAD;
    page_header.p_flags  = PF_R | PF_W | PF_X;
    page_header.p_offset = 0;
    page_header.p_vaddr  = ELF_BASE;
    page_header.p_paddr  = ELF_BASE;
    page_header.p_filesz = seg1;
    page_header.p_memsz  = seg1;
    page_header.p_align  = PAGE_SIZE;

    fwrite(&page_header, sizeof(page_header), 1, file);
}

// PT_LOAD для data (R/W). Методичка: гл. 18.4 (offset vs vaddr)
static void WriteDataSegmentPhdr(FILE *file, size_t data_off, uint64_t data_vaddr, size_t data_size) {
    assert(file);

    Elf64_Phdr page_header = {};

    page_header.p_type   = PT_LOAD;
    page_header.p_flags  = PF_R | PF_W;
    page_header.p_offset = data_off;
    page_header.p_vaddr  = data_vaddr;
    page_header.p_paddr  = data_vaddr;
    page_header.p_filesz = data_size;
    page_header.p_memsz  = data_size;
    page_header.p_align  = PAGE_SIZE;

    fwrite(&page_header, sizeof(page_header), 1, file);
}

// Дописываем pad нулей для выравнивания data-сегмента на страницу
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

// Выбираем условный jcc по операции сравнения. Опкоды лежат в методичке, гл. 12.1
// Логика инвертирована: на условии "истина" мы НЕ прыгаем, а на "ложь"
// прыгаем в else или конец цикла
static uint8_t ChooseJCC(LangNode_t *cond) {
    if (!cond || cond->type != kOperation) return JCC_JE;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (cond->value.operation) {
        case kOperationA:  return JCC_JLE;      // >  не прыгаем; иначе jle
        case kOperationAE: return JCC_JL;       // >= не прыгаем; иначе jl
        case kOperationB:  return JCC_JGE;      // <  не прыгаем; иначе jge
        case kOperationBE: return JCC_JG;       // <= не прыгаем; иначе jg
        case kOperationE:  return JCC_JNE;      // == не прыгаем; иначе jne
        case kOperationNE: return JCC_JE;       // != не прыгаем; иначе je
        default:           return JCC_JE;
    }
    #pragma GCC diagnostic pop
}

// Считаем локальные слоты (включая массивы) в теле функции,
// заодно заполняем pos_in_code для каждой переменной
static void CountLocalSlots(LangNode_t *node, VariableArr *arr, AsmInfo *info) {
    if (!node) return;

    if (IsThatOperation(node, kOperationArrDecl)) {
        LangNode_t *arr_pos = node->left;
        if (arr_pos && arr_pos->left && arr_pos->right) {
            size_t var_pos = arr_pos->left->value.pos;
            int size = (int)arr_pos->right->value.number;

            if (arr->var_array[var_pos].pos_in_code == -1) {
                arr->var_array[var_pos].pos_in_code = info->counter;
                info->counter += size;
            }
        }

        return;
    }

    if (node->type == kVariable) {
        size_t pos = node->value.pos;
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

// Ищем slot переменной по узлу AST
static int GetVarSlot(VariableArr *arr, LangNode_t *node) {
    assert(arr);
    assert(node);

    LangNode_t *check = node;
    if (IsThatOperation(node, kOperationGetAddr) || IsThatOperation(node, kOperationCallAddr)) {
        check = node->left;
    }

    size_t var_pos = check->value.pos;
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

// pop rax; rcx = &var; mov [rcx], rax: снимаем верхушку стека в переменную
static void CodeGeneratePopToVar(Context *context, VariableArr *arr, LangNode_t *node, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(arr);
    assert(node);
    assert(info);
    assert(sub);
    (void)info;

    int slot = GetVarSlot(arr, node);
    POP(kRAX);
    VAR_ADDR(slot, sub->param_count);
    MOV_MEM_R(kRCX, kRAX);
}

// Кладём на стек адрес переменной (для &var и индексирования)
static void CodeGenerateAddrOf(Context *context, LangNode_t *var, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(var);
    assert(arr);
    assert(info);
    assert(sub);
    (void)info;

    int slot = GetVarSlot(arr, var);
    VAR_ADDR(slot, sub->param_count);
    PUSH(kRCX);
}

// Разыменование указателя: на верхушке стека был адрес, кладём вместо него значение по этому адресу
static void CodeGenerateDeref(Context *context, LangNode_t *ptr, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(ptr);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateAddrOf(context, ptr, arr, info, sub);
    POP(kRCX);
    PUSH_MEM(kRCX);
}

// *p = value: справа на стеке value, слева узел разыменования
static void CodeGenerateAddrAssign(Context *context, LangNode_t *deref_node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(deref_node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, deref_node->left, arr, info, sub);
    POP(kRCX);                          // адрес
    POP(kRAX);                          // значение
    MOV_MEM_R(kRCX, kRAX);
}

// Бинарная операция: rax = rax `op` rbx, потом push rax
// IDIV затирает rdx (cqo делает sign-extend rax в rdx:rax). Методичка, гл. 10.5
static void CodeGenerateBinOp(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub, OperationTypes op) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    CodeGenerateExpr(context, node->right, arr, info, sub);
    POP(kRBX);
    POP(kRAX);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (op) {
        case kOperationAdd: ADD_RR (kRAX, kRBX); break;
        case kOperationSub: SUB_RR (kRAX, kRBX); break;
        case kOperationMul: IMUL_RR(kRAX, kRBX); break;
        case kOperationDiv: IDIV_R (kRBX);       break;

        default: break;
    }
    #pragma GCC diagnostic pop

    PUSH(kRAX);
}

// Сохраняем и восстанавливаем rsp через r13. Эту пару зовём вокруг
// ALIGN_STACK + CALL в библиотеку, чтобы корректно вернуть стек
// после сдвига от and rsp, -16

static void EmitSaveRspToR13(Context *context) {
    assert(context);
    MOV_RR(kR13, kRSP);
}

static void EmitRestoreRspFromR13(Context *context) {
    assert(context);
    MOV_RR(kRSP, kR13);
}

// print(int): rsi = значение, rdi = "%d", xor eax (нет xmm-args)
// Методичка: гл. 14.1, 14.3
static void CodeGeneratePrintInt(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    POP(kRSI);
    MOV_DATA(kRDI, "fmt_int");

    SAVE_RSP_R13();
    ALIGN_STACK();
    XOR_EAX();
    CALL("my_printf");
    RESTORE_RSP_R13();
}

// print(char): то же, но формат "%c"
static void CodeGeneratePrintChar(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, node->left, arr, info, sub);
    POP(kRSI);
    MOV_DATA(kRDI, "fmt_char");

    SAVE_RSP_R13();
    ALIGN_STACK();
    XOR_EAX();
    CALL("my_printf");
    RESTORE_RSP_R13();
}

// read(int): значение возвращается в rax, потом push rax
static void CodeGenerateReadInt(Context *context) {
    assert(context);

    SAVE_RSP_R13();
    ALIGN_STACK();
    CALL("my_scanf");
    RESTORE_RSP_R13();
    PUSH(kRAX);
}

// draw(arr): rdi = адрес массива
static void CodeGenerateDraw(Context *context, LangNode_t *node, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(node);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateAddrOf(context, node->left, arr, info, sub);
    POP(kRDI);
    SAVE_RSP_R13();
    ALIGN_STACK();
    CALL("my_draw");
    RESTORE_RSP_R13();
}

// arr[index] = value: rax = value, rdi = index;
// адрес ячейки = base - 8*index, где base = lea [rbp - 8*(local_index+1)]
// Методичка: гл. 5.1, 11
static void CodeGenerateArrAssign(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, stmt->right, arr, info, sub);          // stack: [value]
    CodeGenerateExpr(context, stmt->left->right, arr, info, sub);    // stack: [value, index]
    POP(kRDI);
    POP(kRAX);

    int slot = GetVarSlot(arr, stmt->left->left);
    int local_index = slot - sub->param_count;
    int32_t base_disp = -8 * (local_index + 1);

    LEA_RCX_RBP(base_disp);
    SHL_R_IMM8(kRDI, 3);                // rdi *= 8
    SUB_RR(kRCX, kRDI);                 // rcx = base - 8*index
    MOV_MEM_R(kRCX, kRAX);
}

// Объявление массива: зануляем size слотов подряд начиная с base_slot
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
        VAR_ADDR(slot, sub->param_count);
        MOV_MEM_IMM32(kRCX, 0);
    }
}

// if (cond) { then } [else { else }]:
//   считаем cond, потом cmp rax, rbx, потом jcc else_label
//   then; jmp end_label
//   else_label: else
//   end_label:
// Forward-переходы патчатся через kRel32-релокации. Методичка: гл. 12.2
static void CodeGenerateIf(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    LangNode_t *cond = stmt->left;
    int if_number = info->label_if++, else_number = info->label_else++;
    int has_else = IsThatOperation(stmt->right, kOperationElse);
    char else_label[DEFAULT_LABEL_SIZE] = {}, end_label[DEFAULT_LABEL_SIZE] = {};

    MakeLabel(else_label, sizeof(else_label), "else", else_number);
    MakeLabel(end_label,  sizeof(end_label),  "end_if", if_number);

    CodeGenerateExpr(context, cond->left, arr, info, sub);
    CodeGenerateExpr(context, cond->right, arr, info, sub);
    POP(kRBX);
    POP(kRAX);
    CMP_RAX_RBX();

    JCC(ChooseJCC(cond), else_label);

    if (has_else) {
        CodeGenerateStatement(context, stmt->right->left, arr, info, sub);
    } else {
        CodeGenerateStatement(context, stmt->right, arr, info, sub);
    }

    JMP(end_label);
    LabelAdd(context, else_label, CODE->size);

    if (has_else) {
        CodeGenerateStatement(context, stmt->right->right, arr, info, sub);
    }

    LabelAdd(context, end_label, CODE->size);
}

// while (cond) { body }:
//   start: считаем cond, потом cmp, потом jcc end
//          body; jmp start
//   end:
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
    MakeLabel(end_label,   sizeof(end_label),   "wend",   end_number);

    LabelAdd(context, start_label, CODE->size);
    CodeGenerateExpr(context, stmt->left->left, arr, info, sub);
    CodeGenerateExpr(context, stmt->left->right, arr, info, sub);
    POP(kRBX);
    POP(kRAX);

    CMP_RAX_RBX();
    JCC(ChooseJCC(stmt->left), end_label);
    CodeGenerateStatement(context, stmt->right, arr, info, sub);
    JMP(start_label);
    LabelAdd(context, end_label, CODE->size);
}

// return expr: кладём значение в rax, эпилог, ret
static void CodeGenerateReturn(Context *context, LangNode_t *stmt, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(stmt);
    assert(arr);
    assert(info);
    assert(sub);

    CodeGenerateExpr(context, stmt->left, arr, info, sub);
    POP(kRAX);
    EPILOGUE();
    RET();
}

// Аргументы кладутся на стек в обратном порядке (правый первый, левый последний),
// чтобы при чтении через [rbp + 16 + 8*i] параметр 0 был ближе к rbp
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

// Кодоген выражения: результат всегда оказывается на верхушке стека
static void CodeGenerateExpr(Context *context, LangNode_t *expr, VariableArr *arr, AsmInfo *info, Sub *sub) {
    assert(context);
    assert(arr);
    assert(info);
    assert(sub);
    if (!expr) return;

    switch (expr->type) {
        case kNumber: {
            // Литерал: imm32 короче (7 байт) если влезает, иначе imm64 (10 байт)
            // Методичка: гл. 8.1, 8.2
            int64_t number = (int64_t)expr->value.number;
            if (number >= INT32_MIN && number <= INT32_MAX) {
                MOV_R_IMM32(kRAX, number);
            } else {
                MOV_R_IMM64(kRAX, number);
            }
            PUSH(kRAX);
            break;
        }

        case kVariable: {
            // push qword [&var]
            int slot = GetVarSlot(arr, expr);
            VAR_ADDR(slot, sub->param_count);
            PUSH_MEM(kRCX);
            break;
        }

        case kOperation:
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wswitch-enum"
            switch (expr->value.operation) {
                case kOperationAdd:
                    CodeGenerateBinOp(context, expr, arr, info, sub, kOperationAdd); break;
                case kOperationSub:
                    CodeGenerateBinOp(context, expr, arr, info, sub, kOperationSub); break;
                case kOperationMul:
                    CodeGenerateBinOp(context, expr, arr, info, sub, kOperationMul); break;
                case kOperationDiv:
                    CodeGenerateBinOp(context, expr, arr, info, sub, kOperationDiv); break;

                case kOperationSQRT:
                    // sqrt через xmm0: int, double, sqrt, обратно int
                    // Опкоды SSE лежат в методичке, гл. 10.12
                    CodeGenerateExpr(context, expr->left, arr, info, sub);
                    POP(kRAX);

                    CVTSI2SD_XMM0_R(kRAX);   // xmm0 = (double)rax
                    SQRTSD_XMM_XMM(0, 0);    // xmm0 = sqrt(xmm0)
                    CVTTSD2SI_R_XMM0(kRAX);  // rax  = (int64_t)xmm0 (truncate)

                    PUSH(kRAX);
                    break;

                case kOperationCallAddr:
                    CodeGenerateAddrOf(context, expr->left, arr, info, sub); break;
                case kOperationGetAddr:
                    CodeGenerateDeref(context, expr->left, arr, info, sub); break;

                case kOperationCall: {
                    // Кладём аргументы, делаем вызов, чистим стек, кладём результат
                    const char *callee = arr->var_array[expr->left->value.pos].variable_name;
                    int num_args = CountArgs(expr->right);
                    CodeGenerateParamsToStack(context, expr->right, arr, info, sub);
                    CALL(callee);
                    if (num_args > 0) {
                        ADD_R_IMM(kRSP, (num_args * 8));
                    }

                    PUSH(kRAX);
                    break;
                }

                case kOperationArrPos: {
                    // arr[i] : push qword [base - 8*i]
                    // Методичка: гл. 5 (адресация массивов)
                    int slot = GetVarSlot(arr, expr->left);
                    int local_index = slot - sub->param_count;

                    CodeGenerateExpr(context, expr->right, arr, info, sub);
                    POP(kRDI);

                    int32_t base_disp = -8 * (local_index + 1);
                    LEA_RCX_RBP(base_disp);
                    SHL_R_IMM8(kRDI, 3);
                    SUB_RR(kRCX, kRDI);
                    PUSH_MEM(kRCX);
                    break;
                }

                default: break;
            }
            #pragma GCC diagnostic pop
            break;

        default:
            break;
    }
}

// Кодоген statement'а: ничего не оставляет на стеке (кроме случаев,
// когда сам statement это выражение в default-ветке)
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
                    CALL("my_exit");
                    break;

                case kOperationCallAddr:
                    CodeGenerateAddrOf(context, stmt->left, arr, info, sub);
                    break;

                case kOperationGetAddr:
                    CodeGenerateDeref(context, stmt->left, arr, info, sub);
                    break;

                case kOperationCall: {
                    // Вызов как statement: результат отбрасываем
                    const char *callee = arr->var_array[stmt->left->value.pos].variable_name;
                    int num_args = CountArgs(stmt->right);
                    CodeGenerateParamsToStack(context, stmt->right, arr, info, sub);
                    CALL(callee);
                    if (num_args > 0) {
                        ADD_R_IMM(kRSP, (int64_t)(num_args * 8));
                    }
                    break;
                }

                case kOperationIs:
                    // Присваивание: левая часть может быть массивом, разыменованием или переменной
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

                case kOperationReturn:    CodeGenerateReturn(context, stmt, arr, info, sub); break;
                case kOperationWrite:     CodeGeneratePrintInt(context, stmt, arr, info, sub); break;
                case kOperationWriteChar: CodeGeneratePrintChar(context, stmt, arr, info, sub); break;

                case kOperationRead:
                    CodeGenerateReadInt(context);
                    CodeGeneratePopToVar(context, arr, stmt->left, info, sub);
                    break;

                case kOperationThen:
                    CodeGenerateStatement(context, stmt->left, arr, info, sub);
                    CodeGenerateStatement(context, stmt->right, arr, info, sub);
                    break;

                case kOperationIf:    CodeGenerateIf(context, stmt, arr, info, sub); break;
                case kOperationWhile: CodeGenerateWhile(context, stmt, arr, info, sub); break;

                case kOperationTernary:
                    CodeGenerateStatement(context, stmt->left->right, arr, info, sub);
                    CodeGenerateStatement(context, stmt->left->left, arr, info, sub);
                    break;

                case kOperationArrDecl: CodeGenerateArrDecl(context, stmt, arr, info, sub); break;
                case kOperationDraw:    CodeGenerateDraw(context, stmt, arr, info, sub); break;

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
            MOV_R_IMM32(kRAX, number);
            PUSH(kRAX);
            break;
        }

        default:
            break;
    }
}

// Параметрам функции назначаются slot'ы 0..param_count-1
// (адреса [rbp + 16 + 8*slot])
static void AssignParamSlots(LangNode_t *args, VariableArr *arr, int *slot_counter) {
    if (!args) return;

    if (!IsThatOperation(args, kOperationComma)) {
        if (args->type == kVariable) {
            size_t pos = args->value.pos;
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

// Кодоген одной функции: пролог, тело, эпилог, потом ret (или call my_exit для main)
// frame_size округляется до 16 ради ABI (методичка: гл. 14.3, 14.5)
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

    Sub sub = {0, param_count, frame_size};

    LabelAdd(context, func_name, CODE->size);
    if (is_main) {
        LabelAdd(context, "main", CODE->size);
    }

    PROLOGUE(frame_size);
    CodeGenerateStatement(context, body, arr, info, &sub);
    EPILOGUE();

    if (is_main) {
        CALL("my_exit");
    } else {
        RET();
    }
}

// Идём по дереву верхнего уровня и пишем код всех функций по очереди
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


// Читаем весь файл в память. Кто вызвал, тот и должен free()'нуть результат
static uint8_t* ReadELFToMemory(const char *path, long *out_size) {
    assert(path);
    assert(out_size);

    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size < (long)sizeof(Elf64_Ehdr)) {
        fprintf(stderr, "%s: too small for ELF.\n", path);
        fclose(file);
        return NULL;
    }

    uint8_t *file_buf = (uint8_t *) malloc ((size_t)file_size);
    if (!file_buf) {
        perror("Error malloc file_buf.\n");
        fclose(file);
        return NULL;
    }

    if (fread(file_buf, 1, (size_t)file_size, file) != (size_t)file_size) {
        fprintf(stderr, "%s: failed to read file.\n", path);
        free(file_buf);
        fclose(file);
        return NULL;
    }
    fclose(file);

    *out_size = file_size;
    return file_buf;
}

typedef struct {
    Elf64_Shdr *text;
    Elf64_Shdr *symtab;
    Elf64_Shdr *strtab;
    Elf64_Shdr *rela;
    int text_index;
} FoundSections;

// Собираем offset'ы R_X86_64_64-релокаций внутри .text (относительно
// её начала). Методичка: гл. 19.2 (SHT_RELA)
static int LoadRelocations(LibBlob *blob, uint8_t *file_buf, Elf64_Shdr *rela_sh, Elf64_Shdr *text_sh) {
    assert(blob);
    assert(file_buf);
    assert(rela_sh);
    assert(text_sh);

    blob->relocs = NULL;
    blob->reloc_count = 0;

    if (!rela_sh) return 1;

    Elf64_Rela *rels = (Elf64_Rela *)(file_buf + rela_sh->sh_offset);
    size_t rel_count = rela_sh->sh_size / sizeof(Elf64_Rela);

    if (rel_count == 0) return 1;

    blob->relocs = (uint32_t *) calloc (rel_count, sizeof(uint32_t));
    if (!blob->relocs) {
        perror("Error calloc relocs.\n");
        return 0;
    }

    for (size_t i = 0; i < rel_count; i++) {
        if (ELF64_R_TYPE(rels[i].r_info) == R_X86_64_64) {
            blob->relocs[blob->reloc_count++] = (uint32_t)(rels[i].r_offset - text_sh->sh_addr);
        }
    }

    return 1;
}

// Ищем .text / .symtab / .strtab / .rela.text по имени. Имена секций
// читаются из .shstrtab. Методичка: гл. 19.5
static FoundSections FindSections(Elf64_Ehdr *elf_header, Elf64_Shdr *section_header, const char *shstr) {
    assert(elf_header);
    assert(section_header);
    assert(shstr);

    FoundSections found = {NULL, NULL, NULL, NULL, -1};

    for (int i = 0; i < elf_header->e_shnum; i++) {
        const char *name = shstr + section_header[i].sh_name;

        if (strcmp(name, ".text") == 0) {
            found.text = &section_header[i];
            found.text_index = i;
        } else if (strcmp(name, ".symtab") == 0) {
            found.symtab = &section_header[i];
        } else if (strcmp(name, ".strtab") == 0) {
            found.strtab = &section_header[i];
        } else if (strcmp(name, ".rela.text") == 0) {
            found.rela = &section_header[i];
        }
    }

    return found;
}

// Ищем offset'ы стандартных функций в .text (относительно начала .text)
static int FindStandardSymbols(Elf64_Sym *syms, size_t sym_count, const char *strtab, Elf64_Shdr *text_sh, LibBlob *blob) {
    assert(syms);
    assert(sym_count);
    assert(strtab);
    assert(text_sh);
    assert(blob);

    const char *want_functions[] = {"my_printf", "my_scanf", "my_exit", "my_draw"};
    uint32_t *out_offs[] = {&blob->printf_off, &blob->scanf_off, &blob->exit_off, &blob->draw_off};
    int found[STANDART_FUNCTIONS_NUMBER] = {};

    for (size_t i = 0; i < sym_count; i++) {
        const char *name = strtab + syms[i].st_name;

        for (int k = 0; k < STANDART_FUNCTIONS_NUMBER; k++) {
            if (!found[k] && strcmp(name, want_functions[k]) == 0) {
                *out_offs[k] = (uint32_t)(syms[i].st_value - text_sh->sh_addr);
                found[k] = 1;
            }
        }
    }

    for (int k = 0; k < STANDART_FUNCTIONS_NUMBER; k++) {
        if (!found[k]) return 0;
    }

    return 1;
}

// Полная загрузка my_lib.elf:
//   - проверяем ELF64-магию (методичка: гл. 17.2);
//   - находим нужные секции;
//   - копируем .text в blob->data;
//   - запоминаем offset'ы стандартных функций и список релокаций
static int LoadLib(LibBlob *blob, const char *path) {
    assert(blob);
    assert(path);

    long out_size = 0;
    uint8_t *file_buf = ReadELFToMemory(path, &out_size);
    if (!file_buf) return 0;

    Elf64_Ehdr *elf_header = (Elf64_Ehdr *)file_buf;
    // ELFMAG = "\x7FELF", SELFMAG = 4
    if (memcmp(elf_header->e_ident, ELFMAG, SELFMAG) != 0
            || elf_header->e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "%s: not ELF64.\n", path);
        free(file_buf);
        return 0;
    }

    // shstrtab: секция, в которой лежат имена остальных секций;
    // её индекс хранится в e_shstrndx (методичка: гл. 17.6, 19.5)
    Elf64_Shdr *section_header = (Elf64_Shdr *)(file_buf + elf_header->e_shoff);
    const char *shstr = (const char *)(file_buf + section_header[elf_header->e_shstrndx].sh_offset);

    FoundSections found = FindSections(elf_header, section_header, shstr);
    if (!found.text || !found.symtab || !found.strtab) {
        fprintf(stderr, "%s: missing required ELF sections.\n", path);
        free(file_buf);
        return 0;
    }

    blob->size = found.text->sh_size;
    blob->link_base = found.text->sh_addr;

    blob->data = (uint8_t *) calloc (1, blob->size);
    if (!blob->data) {
        perror("Error calloc blob->data.\n");
        free(file_buf);
        return 0;
    }
    memcpy(blob->data, file_buf + found.text->sh_offset, blob->size);

    Elf64_Sym *syms = (Elf64_Sym *)(file_buf + found.symtab->sh_offset);
    size_t sym_count = found.symtab->sh_size / sizeof(Elf64_Sym);
    const char *strtab = (const char *)(file_buf + found.strtab->sh_offset);

    if (!FindStandardSymbols(syms, sym_count, strtab, found.text, blob)) {
        fprintf(stderr, "%s: standard symbol not found.\n", path);
        free(blob->data);
        free(file_buf);
        return 0;
    }

    if (!LoadRelocations(blob, file_buf, found.rela, found.text)) {
        free(blob->data);
        free(file_buf);
        return 0;
    }

    free(file_buf);
    return 1;
}

static void FreeLib(LibBlob *blob) {
    assert(blob);

    free(blob->data);
    free(blob->relocs);
    blob->data = NULL;
    blob->relocs = NULL;
}
