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

#define EMIT(fmt, ...)                                                    \
    do {                                                                  \
        for (int k = 0; k < sub_info->indent; k++) fprintf(file, "\t");   \
        fprintf(file, fmt "\n", ##__VA_ARGS__);                           \
    } while (0)

#define EMIT_LABEL(fmt, ...)                    \
    do {                                        \
        fprintf(file, fmt "\n", ##__VA_ARGS__); \
    } while (0)

#define EMIT_COMMENT(fmt, ...)                                            \
    do {                                                                  \
        for (int k = 0; k < sub_info->indent; k++) fprintf(file, "\t");   \
        fprintf(file, "; " fmt "\n", ##__VA_ARGS__);                      \
    } while (0)

#define EMIT_BLANK()                            \
    do {                                        \
        fprintf(file, "\n");                    \
    } while (0)

#define EMIT_SECTION(title)                                               \
    do {                                                                  \
        fprintf(file, "\n");                                              \
        for (int k = 0; k < sub_info->indent; k++) fprintf(file, "\t");   \
        fprintf(file, "; --- %s ---\n", title);                           \
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

static void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base, AsmInfo *asm_info, int indent);
static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintExprOperationCase(FILE *file, LangNode_t *expr, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);

static void PopToVar(FILE *file, VariableArr *arr, LangNode_t *node, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void StoreParamFromFrame(FILE *file, VariableArr *arr, LangNode_t *node, AsmInfo *asm_info, SubAsmInfo *sub_info, int frame_off);
static int  ResolveVarShift(VariableArr *arr, LangNode_t *node, AsmInfo *asm_info, SubAsmInfo *sub_info);

static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info, int *frame_off);

static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintStatementOperationCase(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);

static void PrintIfToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintWhileToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintReturn(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);

static void PrintIsForArray(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintArrDeclare(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintAddressOf(FILE *file, LangNode_t *var_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintDereference(FILE *file, LangNode_t *ptr_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintAddressAssignment(FILE *file, LangNode_t *deref_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);

static void EmitPrologue(FILE *file, SubAsmInfo *sub_info) {
    assert(file);

    EMIT_SECTION("prologue");
    EMIT("push rbp");
    EMIT("mov rbp, rsp");
    EMIT("push r12");
    EMIT("push r13");
    EMIT("push rbx");
}

static void EmitEpilogue(FILE *file, SubAsmInfo *sub_info) {
    assert(file);

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

    if (IsThatOperation(root, kOperationFunction)) {
        PrintFunction(file, root, arr, ram_base, asm_info, 1);
    }

    if (root->left) {
        PrintProgram(file, root->left, arr, ram_base, asm_info);
    }

    if (root->right) {
        PrintProgram(file, root->right, arr, ram_base, asm_info);
    }
}

static void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(asm_info);
    if (!func_node) return;

    int param_count = 0;
    CleanPositions(arr);
    asm_info->counter = 0;

    LangNode_t *args = func_node->right->left;
    const char *func_name = arr->var_array[func_node->left->value.pos].variable_name;
    int is_main = (strcmp(MAIN, func_name) == 0);

    fprintf(file, ";---------------------------------------------\n");
    fprintf(file, "; function: %s\n", func_name);
    fprintf(file, ";---------------------------------------------\n");

    if (is_main) {
        EMIT_LABEL("main:");
    }
    EMIT_LABEL("%s:", func_name);

    SubAsmInfo sub_info_val = {*ram_base, 0, indent, "prologue"};
    SubAsmInfo *sub_info = &sub_info_val;

    EmitPrologue(file, sub_info);

    if (is_main) {
        EMIT_BLANK();
        EMIT_COMMENT("r12 = ram base pointer (starts at 0 for main)");
        EMIT("xor r12d, r12d");
    }

    param_count = arr->var_array[func_node->left->value.pos].variable_value;
    sub_info->param_count = param_count;

    if (param_count > 0) {
        EMIT_BLANK();
        EMIT_COMMENT("reserve %d slot(s) for parameters", param_count);
        EMIT("add r12, %d", param_count);
    }

    int frame_off = 16;
    if (args) {
        EMIT_SECTION("load parameters from stack frame");
        PushParamsToRam(file, args, arr, asm_info, sub_info, &frame_off);
    }

    *ram_base += param_count;
    sub_info->ram_base = *ram_base;

    EMIT_SECTION("function body");
    PrintStatement(file, func_node->right->right, arr, asm_info, sub_info);
    *ram_base -= param_count;

    EmitEpilogue(file, sub_info);

    if (is_main) {
        EMIT_BLANK();
        EMIT_COMMENT("exit(0)");
        //EMIT("xor edi, edi");
        EMIT("call my_exit");
    } else {
        EMIT("ret");
    }
    fprintf(file, "\n\n");
}

static int ResolveVarShift(VariableArr *arr, LangNode_t *node, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(arr);
    assert(node);
    assert(asm_info);
    assert(sub_info);

    LangNode_t *check = node;
    if (IsThatOperation(node, kOperationGetAddr) || IsThatOperation(node, kOperationCallAddr)) {
        check = node->left;
    }

    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (arr->var_array[check->value.pos].variable_name && arr->var_array[i].variable_name
                && strcmp(arr->var_array[i].variable_name, arr->var_array[check->value.pos].variable_name) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = asm_info->counter++;
            } else {
                var_idx = arr->var_array[i].pos_in_code;
            }
            break;
        }
    }

    if (var_idx == -1) {
        fprintf(stderr, "Unknown variable\n");
        return 0;
    }

    return var_idx - sub_info->param_count;
}

static void StoreParamFromFrame(FILE *file, VariableArr *arr, LangNode_t *node, AsmInfo *asm_info, SubAsmInfo *sub_info, int frame_off) {
    assert(arr);
    assert(node);
    assert(asm_info);
    assert(sub_info);

    int shift = ResolveVarShift(arr, node, asm_info, sub_info);
    EMIT_COMMENT("param [rbp+%d] -> ram[r12%+d]", frame_off, shift);
    EMIT("mov rax, [rbp + %d]", frame_off);
    EMIT_VAR_ADDR(shift);
    EMIT("mov [rcx], rax");
}

static void PopToVar(FILE *file, VariableArr *arr, LangNode_t *node, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(node);
    assert(asm_info);
    assert(sub_info);

    int shift = ResolveVarShift(arr, node, asm_info, sub_info);
    EMIT_COMMENT("store to var (shift=%d)", shift);
    EMIT("pop rax");
    EMIT_VAR_ADDR(shift);
    EMIT("mov [rcx], rax");
}

static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(sub_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        PrintExpr(file, args_node, arr, asm_info, sub_info);
        return;
    }

    if (args_node->left) {
        PushParamsToStack(file, args_node->right, arr, asm_info, sub_info);
    }

    if (args_node->right) {
        PushParamsToStack(file, args_node->left, arr, asm_info, sub_info);
    }
}

static void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info, int *frame_off) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(sub_info);
    assert(frame_off);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        StoreParamFromFrame(file, arr, args_node, asm_info, sub_info, *frame_off);
        *frame_off += 8;
        return;
    }

    if (args_node->left) {
        PushParamsToRam(file, args_node->left, arr, asm_info, sub_info, frame_off);
    }

    if (args_node->right) {
        PushParamsToRam(file, args_node->right, arr, asm_info, sub_info, frame_off);
    }
}

static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(sub_info);
    if (!expr) return;

    switch (expr->type) {
        case kNumber:
            EMIT("mov rax, %lld", (long long)expr->value.number);
            EMIT("push rax");
            break;

        case kVariable: {
            int shift = FindVarPos(arr, expr, asm_info) - sub_info->param_count;
            EMIT_COMMENT("load var (shift=%d)", shift);
            EMIT_VAR_ADDR(shift);
            EMIT("push qword [rcx]");
            break;
        }

        case kOperation:
            PrintExprOperationCase(file, expr, arr, asm_info, sub_info);
            break;

        default:
            printf("No such expr->type.\n");
    }
}

static void EmitBinaryOp(FILE *file, LangNode_t *node, VariableArr *arr, AsmInfo *asm_info,
        SubAsmInfo *sub_info, const char *op_instr, const char *op_name) {
    assert(file);
    assert(node);
    assert(arr);
    assert(asm_info);
    assert(sub_info);
    assert(op_instr);

    EMIT_BLANK();
    EMIT_COMMENT("%s", op_name);

    PrintExpr(file, node->left, arr, asm_info, sub_info);
    PrintExpr(file, node->right, arr, asm_info, sub_info);

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

static void PrintExprOperationCase(FILE *file, LangNode_t *expr, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(expr);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (expr->value.operation) {
        case kOperationCallAddr:
            PrintAddressOf(file, expr->left, arr, asm_info, sub_info);
            break;

        case kOperationGetAddr:
            PrintDereference(file, expr->left, arr, asm_info, sub_info);
            break;

        case kOperationSQRT:
            EMIT_BLANK();
            EMIT_COMMENT("sqrt");
            PrintExpr(file, expr->left, arr, asm_info, sub_info);
            EMIT("pop rax");
            EMIT("cvtsi2sd xmm0, rax");
            EMIT("sqrtsd xmm0, xmm0");
            EMIT("cvttsd2si rax, xmm0");
            EMIT("push rax");
            break;

        case kOperationAdd:
            EmitBinaryOp(file, expr, arr, asm_info, sub_info, "add", "addition");
            break;
        case kOperationSub:
            EmitBinaryOp(file, expr, arr, asm_info, sub_info, "sub", "subtraction");
            break;
        case kOperationMul:
            EmitBinaryOp(file, expr, arr, asm_info, sub_info, "imul", "multiplication");
            break;
        case kOperationDiv:
            EmitBinaryOp(file, expr, arr, asm_info, sub_info, "idiv", "division");
            break;

        case kOperationCall: {
            const char *callee = arr->var_array[expr->left->value.pos].variable_name;
            int nargs = CountArgs(expr->right);

            EMIT_BLANK();
            EMIT_COMMENT("call %s(%d args), result on stack", callee, nargs);

            PushParamsToStack(file, expr->right, arr, asm_info, sub_info);
            EMIT("call %s", callee);

            if (nargs > 0) {
                EMIT("add rsp, %d", nargs * 8);
            }

            EMIT("push rax");
            break;
        }

        case kOperationArrPos: {
            int arr_base = FindVarPos(arr, expr->left, asm_info) - sub_info->param_count;
            EMIT_BLANK();
            EMIT_COMMENT("array read [base shift=%d]", arr_base);
            EMIT("lea rcx, [ram]");
            EMIT("mov rdi, r12");

            if (arr_base >= 0) {
                EMIT("add rdi, %d", arr_base);
            } else {
                EMIT("sub rdi, %d", -arr_base);
            }

            PrintExpr(file, expr->right, arr, asm_info, sub_info);
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

static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(sub_info);
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            PrintStatementOperationCase(file, stmt, arr, asm_info, sub_info);
            break;

        case kVariable:
            PopToVar(file, arr, stmt, asm_info, sub_info);
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

static void PrintIfToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    LangNode_t *condition = stmt->left;
    int this_if = asm_info->label_if++;
    int this_else = asm_info->label_else++;
    int has_else = IsThatOperation(stmt->right, kOperationElse);

    EMIT_SECTION("if");
    EMIT_COMMENT("evaluate condition");
    PrintExpr(file, condition->left, arr, asm_info, sub_info);
    PrintExpr(file, condition->right, arr, asm_info, sub_info);

    EMIT("pop rbx");
    EMIT("pop rax");
    EMIT("cmp rax, rbx");
    EMIT("%s .else_%d", ChooseCompareMode(condition), this_else);

    EMIT_BLANK();
    EMIT_COMMENT("then branch");
    sub_info->indent++;

    if (has_else) {
        PrintStatement(file, stmt->right->left, arr, asm_info, sub_info);
    } else {
        PrintStatement(file, stmt->right, arr, asm_info, sub_info);
    }

    EMIT("jmp .end_if_%d", this_if);
    sub_info->indent--;

    EMIT_BLANK();
    EMIT_LABEL(".else_%d:", this_else);

    if (has_else) {
        EMIT_COMMENT("else branch");
        PrintStatement(file, stmt->right->right, arr, asm_info, sub_info);
    }

    EMIT_LABEL(".end_if_%d:", this_if);
}

static void PrintWhileToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    int start_label = asm_info->label_counter++;
    int end_label = asm_info->label_counter++;

    EMIT_SECTION("while loop");
    EMIT_LABEL(".while_start_%d:", start_label);

    EMIT_COMMENT("evaluate condition");
    PrintExpr(file, stmt->left->left, arr, asm_info, sub_info);
    PrintExpr(file, stmt->left->right, arr, asm_info, sub_info);

    EMIT("pop rbx");
    EMIT("pop rax");
    EMIT("cmp rax, rbx");
    EMIT("%s .while_end_%d", ChooseCompareMode(stmt->left), end_label);

    EMIT_BLANK();
    EMIT_COMMENT("loop body");
    sub_info->indent++;
    PrintStatement(file, stmt->right, arr, asm_info, sub_info);
    sub_info->indent--;

    EMIT_BLANK();
    EMIT("jmp .while_start_%d", start_label);
    EMIT_LABEL(".while_end_%d:", end_label);
}

static void PrintReturn(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    EMIT_SECTION("return");
    PrintExpr(file, stmt->left, arr, asm_info, sub_info);

    EMIT("pop rax");

    EmitEpilogue(file, sub_info);
    EMIT("ret");
}

static void PrintArrDeclare(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    arr->var_array[stmt->left->left->left->value.pos].pos_in_code = asm_info->counter;

    int arr_size = (int)stmt->left->left->right->value.number;

    EMIT_BLANK();
    EMIT_COMMENT("declare array[%d], base at counter=%d", arr_size, asm_info->counter);

    for (int i = 0; i < arr_size; i++) {
        int shift = asm_info->counter + i - sub_info->param_count;
        EMIT_VAR_ADDR(shift);
        EMIT("mov qword [rcx], 0");
    }

    asm_info->counter += arr_size;
}

static void PrintIsForArray(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("array element assignment");

    PrintExpr(file, stmt->right, arr, asm_info, sub_info);
    int arr_base = FindVarPos(arr, stmt->left->left, asm_info) - sub_info->param_count;

    EMIT("lea rcx, [ram]");
    EMIT("mov rdi, r12");
    if (arr_base >= 0) {
        EMIT("add rdi, %d", arr_base);
    } else {
        EMIT("sub rdi, %d", -arr_base);
    }

    EMIT_COMMENT("compute index");
    PrintExpr(file, stmt->left->right, arr, asm_info, sub_info);
    EMIT("pop rax");
    EMIT("add rdi, rax");

    EMIT("pop rax");
    EMIT("mov [rcx + rdi*8], rax");
}

static void PrintAddressOf(FILE *file, LangNode_t *var_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(var_node);
    assert(arr);
    assert(asm_info);
    assert(var_node->type == kVariable);
    assert(sub_info);

    int shift = FindVarPos(arr, var_node, asm_info) - sub_info->param_count;

    EMIT_BLANK();
    EMIT_COMMENT("address-of (shift=%d)", shift);
    EMIT_VAR_ADDR(shift);
    EMIT("push rcx");
}

static void PrintDereference(FILE *file, LangNode_t *ptr_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(ptr_node);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    EMIT_COMMENT("dereference");
    PrintAddressOf(file, ptr_node, arr, asm_info, sub_info);
    EMIT("pop rcx");
    EMIT("push qword [rcx]");
}

static void PrintAddressAssignment(FILE *file, LangNode_t *deref_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(deref_node);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    EMIT_COMMENT("store via pointer");
    PrintExpr(file, deref_node->left, arr, asm_info, sub_info);
    EMIT("pop rcx");
    EMIT("pop rax");
    EMIT("mov [rcx], rax");
}

static void EmitPrintInt(FILE *file, LangNode_t *node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(node);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("print integer");
    PrintExpr(file, node->left, arr, asm_info, sub_info);
    EMIT("pop rsi");
    EMIT("mov r13, rsp");
    EMIT("and rsp, -16");
    EMIT("lea rdi, [fmt_int]");
    EMIT("xor eax, eax");
    EMIT("call my_printf");
    EMIT("mov rsp, r13");
}

static void EmitPrintChar(FILE *file, LangNode_t *node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(node);
    assert(arr);
    assert(asm_info);
    assert(sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("print character");
    PrintExpr(file, node->left, arr, asm_info, sub_info);
    EMIT("pop rsi");
    EMIT("mov r13, rsp");
    EMIT("and rsp, -16");
    EMIT("lea rdi, [fmt_char]");
    EMIT("xor eax, eax");
    EMIT("call my_printf");
    EMIT("mov rsp, r13");
}

static void EmitReadInt(FILE *file, SubAsmInfo *sub_info) {
    assert(file);
    assert(sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("read integer from stdin");
    EMIT("call my_scanf");
    EMIT("push rax");
}

static void PrintStatementOperationCase(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(sub_info);
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
            PrintAddressOf(file, stmt->left, arr, asm_info, sub_info);
            break;

        case kOperationGetAddr:
            PrintDereference(file, stmt->left, arr, asm_info, sub_info);
            break;

        case kOperationCall: {
            const char *callee = arr->var_array[stmt->left->value.pos].variable_name;
            int number_args = CountArgs(stmt->right);

            EMIT_BLANK();
            EMIT_COMMENT("call %s(%d args), discard result", callee, number_args);

            PushParamsToStack(file, stmt->right, arr, asm_info, sub_info);
            EMIT("call %s", callee);
            if (number_args > 0) {
                EMIT("add rsp, %d", number_args * 8);
            }
            break;
        }

        case kOperationIs:
            if (IsThatOperation(stmt->left, kOperationArrPos)) {
                PrintIsForArray(file, stmt, arr, asm_info, sub_info);
                break;
            }

            EMIT_BLANK();
            EMIT_COMMENT("assignment");
            PrintExpr(file, stmt->right, arr, asm_info, sub_info);

            if (IsThatOperation(stmt->left, kOperationGetAddr)) {
                PrintAddressAssignment(file, stmt, arr, asm_info, sub_info);
                break;
            }

            PrintStatement(file, stmt->left, arr, asm_info, sub_info);
            break;

        case kOperationReturn:
            PrintReturn(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationWrite:
            EmitPrintInt(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationWriteChar:
            EmitPrintChar(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationRead:
            EmitReadInt(file, sub_info);
            PopToVar(file, arr, stmt->left, asm_info, sub_info);
            break;

        case kOperationThen:
            PrintStatement(file, stmt->left, arr, asm_info, sub_info);
            PrintStatement(file, stmt->right, arr, asm_info, sub_info);
            break;

        case kOperationIf:
            PrintIfToAsm(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationWhile:
            PrintWhileToAsm(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationTernary:
            EMIT_BLANK();
            EMIT_COMMENT("ternary");
            PrintStatement(file, stmt->left->right, arr, asm_info, sub_info);
            PrintStatement(file, stmt->left->left, arr, asm_info, sub_info);
            break;

        case kOperationArrDecl:
            PrintArrDeclare(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationDraw:
            EMIT_BLANK();
            EMIT_COMMENT("DRAW -- not implemented for x86-64");
            break;

        default:
            PrintExpr(file, stmt, arr, asm_info, sub_info);
            break;
    }
    #pragma GCC diagnostic pop
}