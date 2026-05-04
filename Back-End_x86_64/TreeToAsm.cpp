#include "Back-End/TreeToAsm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/CommonFunctions.h"
#include "Common/StackFunctions.h"
#include "Common/CommonBackFunctions.h"

typedef struct {
    int ram_base;
    int param_count;
    int indent;
    const char *comment;
} SubAsmInfo;

typedef struct {
    FILE        *file;
    VariableArr *arr;
    AsmInfo     *asm_info;
    SubAsmInfo  *sub_info;
} AsmCtx;

#define EMIT(fmt, ...)                                                          \
    do {                                                                        \
        for (int k = 0; k < ctx->sub_info->indent; k++) fprintf(ctx->file, "\t"); \
        fprintf(ctx->file, fmt "\n", ##__VA_ARGS__);                            \
    } while (0)

#define EMIT_LABEL(fmt, ...)                         \
    do {                                             \
        fprintf(ctx->file, fmt "\n", ##__VA_ARGS__); \
    } while (0)

#define EMIT_COMMENT(fmt, ...)                                                  \
    do {                                                                        \
        for (int k = 0; k < ctx->sub_info->indent; k++) fprintf(ctx->file, "\t"); \
        fprintf(ctx->file, "; " fmt "\n", ##__VA_ARGS__);                       \
    } while (0)

#define EMIT_BLANK()                             \
    do {                                         \
        fprintf(ctx->file, "\n");                \
    } while (0)

#define EMIT_SECTION(title)                                                     \
    do {                                                                        \
        fprintf(ctx->file, "\n");                                               \
        for (int k = 0; k < ctx->sub_info->indent; k++) fprintf(ctx->file, "\t"); \
        fprintf(ctx->file, "; --- %s ---\n", title);                            \
    } while (0)

#define EMIT_VAR_ADDR(shift)                                    \
    do {                                                        \
        EMIT("lea rcx, [ram]");                                 \
        EMIT("mov rdi, r12");                                   \
        if ((shift) >= 0) {                                     \
            EMIT("add rdi, %d", (shift));                       \
        } else {                                                \
            EMIT("sub rdi, %d", -(shift));                      \
        }                                                       \
        EMIT("lea rcx, [rcx + rdi*8]");                         \
    } while (0)

#define CALLEE_SAVED_SIZE 24

static const char *ChooseCompareMode(LangNode_t *node);

static void PrintFunction(LangNode_t *func_node, int *ram_base, AsmCtx *ctx);
static void PrintExpr(LangNode_t *expr, AsmCtx *ctx);
static void PrintExprOperationCase(LangNode_t *expr, AsmCtx *ctx);

static void PopToVar(LangNode_t *node, AsmCtx *ctx);
static void StoreParamFromFrame(LangNode_t *node, AsmCtx *ctx, int frame_off);
static int  ResolveVarShift(LangNode_t *node, AsmCtx *ctx);

static void PushParamsToStack(LangNode_t *args_node, AsmCtx *ctx);
static void PushParamsToRam(LangNode_t *args_node, AsmCtx *ctx, int *frame_off);

static void PrintStatement(LangNode_t *stmt, AsmCtx *ctx);
static void PrintStatementOperationCase(LangNode_t *stmt, AsmCtx *ctx);

static void PrintIfToAsm(LangNode_t *stmt, AsmCtx *ctx);
static void PrintWhileToAsm(LangNode_t *stmt, AsmCtx *ctx);
static void PrintReturn(LangNode_t *stmt, AsmCtx *ctx);

static void PrintIsForArray(LangNode_t *stmt, AsmCtx *ctx);
static void PrintArrDeclare(LangNode_t *stmt, AsmCtx *ctx);
static void PrintAddressOf(LangNode_t *var_node, AsmCtx *ctx);
static void PrintDereference(LangNode_t *ptr_node, AsmCtx *ctx);
static void PrintAddressAssignment(LangNode_t *deref_node, AsmCtx *ctx);

static void EmitPrologue(AsmCtx *ctx) {
    assert(ctx->file);

    EMIT_SECTION("prologue");
    EMIT("push rbp");
    EMIT("mov rbp, rsp");
    EMIT("push r12");
    EMIT("push r13");
    EMIT("push rbx");
}

static void EmitEpilogue(AsmCtx *ctx) {
    assert(ctx->file);

    EMIT_SECTION("epilogue");
    EMIT("lea rsp, [rbp - %d]", CALLEE_SAVED_SIZE);
    EMIT("pop rbx");
    EMIT("pop r13");
    EMIT("pop r12");
    EMIT("pop rbp");
}

void PrintProgram(FILE *file, LangNode_t *root, VariableArr *arr, int *ram_base, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(asm_info);
    if (!root) return;

    static int header_printed = 0;
    if (!header_printed) {
        header_printed = 1;

        fprintf(file, ";---------------------------------------------\n");
        fprintf(file, "; Generated assembly\n");
        fprintf(file, ";---------------------------------------------\n\n");

        fprintf(file, "default rel\n\n");

        fprintf(file, "section .data\n");
        fprintf(file, "\tfmt_int:  db \"%%d\", 10, 0\n");
        fprintf(file, "\tfmt_char: db \"%%c\", 0\n\n");

        fprintf(file, "section .bss\n");
        fprintf(file, "\tram: resq 65536\n\n");

        fprintf(file, "section .text\n");
        //fprintf(file, "\textern my_printf, my_scanf, my_exit\n");
        fprintf(file, "\tglobal main\n\n");
    }

    asm_info->counter = 0;

    SubAsmInfo sub_info = {0};
    AsmCtx ctx_val = {file, arr, asm_info, &sub_info};

    if (IsThatOperation(root, kOperationFunction)) {
        PrintFunction(root, ram_base, &ctx_val);
    }

    if (root->left) {
        PrintProgram(file, root->left, arr, ram_base, asm_info);
    }

    if (root->right) {
        PrintProgram(file, root->right, arr, ram_base, asm_info);
    }
}

static void PrintFunction(LangNode_t *func_node, int *ram_base, AsmCtx *ctx) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(ctx->asm_info);
    if (!func_node) return;

    int param_count = 0;
    CleanPositions(ctx->arr);
    ctx->asm_info->counter = 0;

    LangNode_t *args = func_node->right->left;
    const char *func_name = ctx->arr->var_array[func_node->left->value.pos].variable_name;
    int is_main = (strcmp(MAIN, func_name) == 0);

    fprintf(ctx->file, ";---------------------------------------------\n");
    fprintf(ctx->file, "; function: %s\n", func_name);
    fprintf(ctx->file, ";---------------------------------------------\n");

    if (is_main) {
        EMIT_LABEL("main:");
    }
    EMIT_LABEL("%s:", func_name);

    SubAsmInfo sub_info_val = {*ram_base, 0, ctx->sub_info->indent, "prologue"};
    ctx->sub_info = &sub_info_val;

    EmitPrologue(ctx);

    if (is_main) {
        EMIT_BLANK();
        EMIT_COMMENT("r12 = ram base pointer (starts at 0 for main)");
        EMIT("xor r12d, r12d");
    }

    param_count = ctx->arr->var_array[func_node->left->value.pos].variable_value;
    ctx->sub_info->param_count = param_count;

    if (param_count > 0) {
        EMIT_BLANK();
        EMIT_COMMENT("reserve %d slot(s) for parameters", param_count);
        EMIT("add r12, %d", param_count);
    }

    int frame_off = 16;
    if (args) {
        EMIT_SECTION("load parameters from stack frame");
        PushParamsToRam(args, ctx, &frame_off);
    }

    *ram_base += param_count;
    ctx->sub_info->ram_base = *ram_base;

    EMIT_SECTION("function body");
    PrintStatement(func_node->right->right, ctx);
    *ram_base -= param_count;

    EmitEpilogue(ctx);

    if (is_main) {
        EMIT_BLANK();
        EMIT_COMMENT("exit(0)");
        //EMIT("xor edi, edi");
        EMIT("call my_exit");
    } else {
        EMIT("ret");
    }
    fprintf(ctx->file, "\n\n");
}

static int ResolveVarShift(LangNode_t *node, AsmCtx *ctx) {
    assert(ctx->arr);
    assert(node);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    LangNode_t *check = node;
    if (IsThatOperation(node, kOperationGetAddr) || IsThatOperation(node, kOperationCallAddr)) {
        check = node->left;
    }

    int var_idx = -1;
    for (size_t i = 0; i < ctx->arr->size; i++) {
        if (ctx->arr->var_array[check->value.pos].variable_name && ctx->arr->var_array[i].variable_name
                && strcmp(ctx->arr->var_array[i].variable_name, ctx->arr->var_array[check->value.pos].variable_name) == 0) {
            if (ctx->arr->var_array[i].pos_in_code == -1) {
                var_idx = ctx->arr->var_array[i].pos_in_code = ctx->asm_info->counter++;
            } else {
                var_idx = ctx->arr->var_array[i].pos_in_code;
            }
            break;
        }
    }

    if (var_idx == -1) {
        fprintf(stderr, "Unknown variable\n");
        return 0;
    }

    return var_idx - ctx->sub_info->param_count;
}

static void StoreParamFromFrame(LangNode_t *node, AsmCtx *ctx, int frame_off) {
    assert(ctx->arr);
    assert(node);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    int shift = ResolveVarShift(node, ctx);
    EMIT_COMMENT("param [rbp+%d] -> ram[r12%+d]", frame_off, shift);
    EMIT("mov rax, [rbp + %d]", frame_off);
    EMIT_VAR_ADDR(shift);
    EMIT("mov [rcx], rax");
}

static void PopToVar(LangNode_t *node, AsmCtx *ctx) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(node);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    int shift = ResolveVarShift(node, ctx);
    EMIT_COMMENT("store to var (shift=%d)", shift);
    EMIT("pop rax");
    EMIT_VAR_ADDR(shift);
    EMIT("mov [rcx], rax");
}

static void PushParamsToStack(LangNode_t *args_node, AsmCtx *ctx) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        PrintExpr(args_node, ctx);
        return;
    }

    if (args_node->left) {
        PushParamsToStack(args_node->right, ctx);
    }

    if (args_node->right) {
        PushParamsToStack(args_node->left, ctx);
    }
}

static void PushParamsToRam(LangNode_t *args_node, AsmCtx *ctx, int *frame_off) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);
    assert(frame_off);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        StoreParamFromFrame(args_node, ctx, *frame_off);
        *frame_off += 8;
        return;
    }

    if (args_node->left) {
        PushParamsToRam(args_node->left, ctx, frame_off);
    }

    if (args_node->right) {
        PushParamsToRam(args_node->right, ctx, frame_off);
    }
}

static void PrintExpr(LangNode_t *expr, AsmCtx *ctx) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);
    if (!expr) return;

    switch (expr->type) {
        case kNumber:
            EMIT("mov rax, %lld", (long long)expr->value.number);
            EMIT("push rax");
            break;

        case kVariable: {
            int shift = FindVarPos(ctx->arr, expr, ctx->asm_info) - ctx->sub_info->param_count;
            EMIT_COMMENT("load var (shift=%d)", shift);
            EMIT_VAR_ADDR(shift);
            EMIT("push qword [rcx]");
            break;
        }

        case kOperation:
            PrintExprOperationCase(expr, ctx);
            break;

        default:
            printf("No such expr->type.\n");
    }
}

static void EmitBinaryOp(LangNode_t *node, AsmCtx *ctx, const char *op_instr, const char *op_name) {
    assert(ctx->file);
    assert(node);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);
    assert(op_instr);

    EMIT_BLANK();
    EMIT_COMMENT("%s", op_name);

    PrintExpr(node->left, ctx);
    PrintExpr(node->right, ctx);

    EMIT("pop rbx");
    EMIT("pop rax");

    if (strcmp(op_instr, "imul") == 0) {
        EMIT("imul rax, rbx");
    } else if (strcmp(op_instr, "idiv") == 0) {
        EMIT("cqo");
        EMIT("idiv rbx");
    } else {
        EMIT("%s rax, rbx", op_instr);
    }

    EMIT("push rax");
}

static void PrintExprOperationCase(LangNode_t *expr, AsmCtx *ctx) {
    assert(ctx->file);
    assert(expr);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (expr->value.operation) {
        case kOperationCallAddr:
            PrintAddressOf(expr->left, ctx);
            break;

        case kOperationGetAddr:
            PrintDereference(expr->left, ctx);
            break;

        case kOperationSQRT:
            EMIT_BLANK();
            EMIT_COMMENT("sqrt");
            PrintExpr(expr->left, ctx);
            EMIT("pop rax");
            EMIT("cvtsi2sd xmm0, rax");
            EMIT("sqrtsd xmm0, xmm0");
            EMIT("cvttsd2si rax, xmm0");
            EMIT("push rax");
            break;

        case kOperationAdd:
            EmitBinaryOp(expr, ctx, "add", "addition");
            break;
        case kOperationSub:
            EmitBinaryOp(expr, ctx, "sub", "subtraction");
            break;
        case kOperationMul:
            EmitBinaryOp(expr, ctx, "imul", "multiplication");
            break;
        case kOperationDiv:
            EmitBinaryOp(expr, ctx, "idiv", "division");
            break;

        case kOperationCall: {
            const char *callee = ctx->arr->var_array[expr->left->value.pos].variable_name;
            int nargs = CountArgs(expr->right);

            EMIT_BLANK();
            EMIT_COMMENT("call %s(%d args), result on stack", callee, nargs);

            PushParamsToStack(expr->right, ctx);
            EMIT("call %s", callee);

            if (nargs > 0) {
                EMIT("add rsp, %d", nargs * 8);
            }

            EMIT("push rax");
            break;
        }

        case kOperationArrPos: {
            int arr_base = FindVarPos(ctx->arr, expr->left, ctx->asm_info) - ctx->sub_info->param_count;
            EMIT_BLANK();
            EMIT_COMMENT("array read [base shift=%d]", arr_base);
            EMIT("lea rcx, [ram]");
            EMIT("mov rdi, r12");

            if (arr_base >= 0) {
                EMIT("add rdi, %d", arr_base);
            } else {
                EMIT("sub rdi, %d", -arr_base);
            }

            PrintExpr(expr->right, ctx);
            EMIT("pop rax");
            EMIT("add rdi, rax");
            EMIT("push qword [rcx + rdi*8]");
            break;
        }

        default:
            break;
    }
    #pragma GCC diagnostic pop
}

static void PrintStatement(LangNode_t *stmt, AsmCtx *ctx) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            PrintStatementOperationCase(stmt, ctx);
            break;

        case kVariable:
            PopToVar(stmt, ctx);
            break;

        case kNumber:
            EMIT("mov rax, %lld", (long long)stmt->value.number);
            EMIT("push rax");
            break;

        default:
            fprintf(stderr, "No such switch case.\n");
            break;
    }
}

static const char *ChooseCompareMode(LangNode_t *node) {
    if (!node || node->type != kOperation) return "je";

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (node->value.operation) {
        case kOperationA:  return "jle";
        case kOperationAE: return "jl";
        case kOperationB:  return "jge";
        case kOperationBE: return "jg";
        case kOperationE:  return "jne";
        case kOperationNE: return "je";
        default:           return "je";
    }
    #pragma GCC diagnostic pop
}

static void PrintIfToAsm(LangNode_t *stmt, AsmCtx *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    LangNode_t *condition = stmt->left;
    int this_if = ctx->asm_info->label_if++;
    int this_else = ctx->asm_info->label_else++;
    int has_else = IsThatOperation(stmt->right, kOperationElse);

    EMIT_SECTION("if");
    EMIT_COMMENT("evaluate condition");
    PrintExpr(condition->left, ctx);
    PrintExpr(condition->right, ctx);

    EMIT("pop rbx");
    EMIT("pop rax");
    EMIT("cmp rax, rbx");
    EMIT("%s .else_%d", ChooseCompareMode(condition), this_else);

    EMIT_BLANK();
    EMIT_COMMENT("then branch");
    ctx->sub_info->indent++;

    if (has_else) {
        PrintStatement(stmt->right->left, ctx);
    } else {
        PrintStatement(stmt->right, ctx);
    }

    EMIT("jmp .end_if_%d", this_if);
    ctx->sub_info->indent--;

    EMIT_BLANK();
    EMIT_LABEL(".else_%d:", this_else);

    if (has_else) {
        EMIT_COMMENT("else branch");
        PrintStatement(stmt->right->right, ctx);
    }

    EMIT_LABEL(".end_if_%d:", this_if);
}

static void PrintWhileToAsm(LangNode_t *stmt, AsmCtx *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    int start_label = ctx->asm_info->label_counter++;
    int end_label = ctx->asm_info->label_counter++;

    EMIT_SECTION("while loop");
    EMIT_LABEL(".while_start_%d:", start_label);

    EMIT_COMMENT("evaluate condition");
    PrintExpr(stmt->left->left, ctx);
    PrintExpr(stmt->left->right, ctx);

    EMIT("pop rbx");
    EMIT("pop rax");
    EMIT("cmp rax, rbx");
    EMIT("%s .while_end_%d", ChooseCompareMode(stmt->left), end_label);

    EMIT_BLANK();
    EMIT_COMMENT("loop body");
    ctx->sub_info->indent++;
    PrintStatement(stmt->right, ctx);
    ctx->sub_info->indent--;

    EMIT_BLANK();
    EMIT("jmp .while_start_%d", start_label);
    EMIT_LABEL(".while_end_%d:", end_label);
}

static void PrintReturn(LangNode_t *stmt, AsmCtx *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    EMIT_SECTION("return");
    PrintExpr(stmt->left, ctx);

    EMIT("pop rax");

    EmitEpilogue(ctx);
    EMIT("ret");
}

static void PrintArrDeclare(LangNode_t *stmt, AsmCtx *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    ctx->arr->var_array[stmt->left->left->left->value.pos].pos_in_code = ctx->asm_info->counter;

    int arr_size = (int)stmt->left->left->right->value.number;

    EMIT_BLANK();
    EMIT_COMMENT("declare array[%d], base at counter=%d", arr_size, ctx->asm_info->counter);

    for (int i = 0; i < arr_size; i++) {
        int shift = ctx->asm_info->counter + i - ctx->sub_info->param_count;
        EMIT_VAR_ADDR(shift);
        EMIT("mov qword [rcx], 0");
    }

    ctx->asm_info->counter += arr_size;
}

static void PrintIsForArray(LangNode_t *stmt, AsmCtx *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("array element assignment");

    PrintExpr(stmt->right, ctx);
    int arr_base = FindVarPos(ctx->arr, stmt->left->left, ctx->asm_info) - ctx->sub_info->param_count;

    EMIT("lea rcx, [ram]");
    EMIT("mov rdi, r12");
    if (arr_base >= 0) {
        EMIT("add rdi, %d", arr_base);
    } else {
        EMIT("sub rdi, %d", -arr_base);
    }

    EMIT_COMMENT("compute index");
    PrintExpr(stmt->left->right, ctx);
    EMIT("pop rax");
    EMIT("add rdi, rax");

    EMIT("pop rax");
    EMIT("mov [rcx + rdi*8], rax");
}

static void PrintAddressOf(LangNode_t *var_node, AsmCtx *ctx) {
    assert(ctx->file);
    assert(var_node);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(var_node->type == kVariable);
    assert(ctx->sub_info);

    int shift = FindVarPos(ctx->arr, var_node, ctx->asm_info) - ctx->sub_info->param_count;

    EMIT_BLANK();
    EMIT_COMMENT("address-of (shift=%d)", shift);
    EMIT_VAR_ADDR(shift);
    EMIT("push rcx");
}

static void PrintDereference(LangNode_t *ptr_node, AsmCtx *ctx) {
    assert(ctx->file);
    assert(ptr_node);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    EMIT_COMMENT("dereference");
    PrintAddressOf(ptr_node, ctx);
    EMIT("pop rcx");
    EMIT("push qword [rcx]");
}

static void PrintAddressAssignment(LangNode_t *deref_node, AsmCtx *ctx) {
    assert(ctx->file);
    assert(deref_node);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    EMIT_COMMENT("store via pointer");
    PrintExpr(deref_node->left, ctx);
    EMIT("pop rcx");
    EMIT("pop rax");
    EMIT("mov [rcx], rax");
}

static void EmitPrintInt(LangNode_t *node, AsmCtx *ctx) {
    assert(ctx->file);
    assert(node);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("print integer");
    PrintExpr(node->left, ctx);
    EMIT("pop rsi");
    EMIT("mov r13, rsp");
    EMIT("and rsp, -16");
    EMIT("lea rdi, [fmt_int]");
    EMIT("xor eax, eax");
    EMIT("call my_printf");
    EMIT("mov rsp, r13");
}

static void EmitPrintChar(LangNode_t *node, AsmCtx *ctx) {
    assert(ctx->file);
    assert(node);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("print character");
    PrintExpr(node->left, ctx);
    EMIT("pop rsi");
    EMIT("mov r13, rsp");
    EMIT("and rsp, -16");
    EMIT("lea rdi, [fmt_char]");
    EMIT("xor eax, eax");
    EMIT("call my_printf");
    EMIT("mov rsp, r13");
}

static void EmitReadInt(AsmCtx *ctx) {
    assert(ctx->file);
    assert(ctx->sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("read integer from stdin");
    EMIT("call my_scanf");
    EMIT("push rax");
}

static void PrintStatementOperationCase(LangNode_t *stmt, AsmCtx *ctx) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(ctx->sub_info);
    if (!stmt) return;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (stmt->value.operation) {
        case kOperationHLT:
            EMIT_BLANK();
            EMIT_COMMENT("halt");
            //EMIT("xor edi, edi");
            EMIT("call my_exit");
            break;

        case kOperationCallAddr:
            PrintAddressOf(stmt->left, ctx);
            break;

        case kOperationGetAddr:
            PrintDereference(stmt->left, ctx);
            break;

        case kOperationCall: {
            const char *callee = ctx->arr->var_array[stmt->left->value.pos].variable_name;
            int number_args = CountArgs(stmt->right);

            EMIT_BLANK();
            EMIT_COMMENT("call %s(%d args), discard result", callee, number_args);

            PushParamsToStack(stmt->right, ctx);
            EMIT("call %s", callee);
            if (number_args > 0) {
                EMIT("add rsp, %d", number_args * 8);
            }
            break;
        }

        case kOperationIs:
            if (IsThatOperation(stmt->left, kOperationArrPos)) {
                PrintIsForArray(stmt, ctx);
                break;
            }

            EMIT_BLANK();
            EMIT_COMMENT("assignment");
            PrintExpr(stmt->right, ctx);

            if (IsThatOperation(stmt->left, kOperationGetAddr)) {
                PrintAddressAssignment(stmt, ctx);
                break;
            }

            PrintStatement(stmt->left, ctx);
            break;

        case kOperationReturn:
            PrintReturn(stmt, ctx);
            break;

        case kOperationWrite:
            EmitPrintInt(stmt, ctx);
            break;

        case kOperationWriteChar:
            EmitPrintChar(stmt, ctx);
            break;

        case kOperationRead:
            EmitReadInt(ctx);
            PopToVar(stmt->left, ctx);
            break;

        case kOperationThen:
            PrintStatement(stmt->left, ctx);
            PrintStatement(stmt->right, ctx);
            break;

        case kOperationIf:
            PrintIfToAsm(stmt, ctx);
            break;

        case kOperationWhile:
            PrintWhileToAsm(stmt, ctx);
            break;

        case kOperationTernary:
            EMIT_BLANK();
            EMIT_COMMENT("ternary");
            PrintStatement(stmt->left->right, ctx);
            PrintStatement(stmt->left->left, ctx);
            break;

        case kOperationArrDecl:
            PrintArrDeclare(stmt, ctx);
            break;

        case kOperationDraw:
            EMIT_BLANK();
            EMIT_COMMENT("DRAW -- not implemented for x86-64");
            break;

        default:
            PrintExpr(stmt, ctx);
            break;
    }
    #pragma GCC diagnostic pop
}