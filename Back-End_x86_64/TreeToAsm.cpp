#include "Back-End/TreeToAsm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/CommonFunctions.h"
#include "Common/StackFunctions.h"

#define EMIT(fmt, ...)                                          \
    do {                                                        \
        for (int k = 0; k < indent; k++) fprintf(file, "\t");   \
        fprintf(file, fmt "\n", ##__VA_ARGS__);                 \
    } while (0)

#define EMIT_LABEL(fmt, ...)                    \
    do {                                        \
        fprintf(file, fmt "\n", ##__VA_ARGS__); \
    } while (0)

#define EMIT_COMMENT(fmt, ...)                                  \
    do {                                                        \
        for (int k = 0; k < indent; k++) fprintf(file, "\t");   \
        fprintf(file, "; " fmt "\n", ##__VA_ARGS__);            \
    } while (0)

#define EMIT_VAR_ADDR(shift)                                    \
    do {                                                        \
        EMIT("lea rcx, [rel ram]");                             \
        EMIT("mov rdi, r12");                                   \
        if ((shift) >= 0) {                                     \
            EMIT("add rdi, %d", (shift));                       \
        } else {                                                \
            EMIT("sub rdi, %d", -(shift));                      \
        }                                                       \
        EMIT("lea rcx, [rcx + rdi*8]");                         \
    } while (0)

#define CALLEE_SAVED_SIZE 24

static void CleanPositions(VariableArr *arr);
static const char *ChooseCompareMode(LangNode_t *node);
static int  CountArgs(LangNode_t *args_node);

static void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base, AsmInfo *asm_info, int indent);
static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment);
static void PrintExprOperationCase(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment);

static void PopToVar(FILE *file, VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info, int indent);
static void StoreParamFromFrame(FILE *file, VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info, int indent, int frame_off);
static int  FindVarPos(VariableArr *arr, LangNode_t *node, AsmInfo *asm_info);

static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, int *frame_off);

static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment);
static void PrintStatementOperationCase(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment);

static void PrintIfToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void PrintWhileToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void PrintReturn(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment);

static void PrintIsForArray(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void PrintArrDeclare(FILE *file, LangNode_t *stmt, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent);
static void PrintAddressOf(FILE *file, LangNode_t *var_node, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent, const char *comment);
static void PrintDereference(FILE *file, LangNode_t *ptr_node, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent, const char *comment);
static void PrintAddressAssignment(FILE *file, LangNode_t *deref_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);

static void EmitPrologue(FILE *file, int indent) {
    assert(file);

    EMIT("push rbp");
    EMIT("mov rbp, rsp");
    EMIT("push r12");
    EMIT("push r13");
    EMIT("push rbx");
}

static void EmitEpilogue(FILE *file, int indent) {
    assert(file);

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
        fprintf(file, "default rel\n\n");
        fprintf(file, "section .data\n");
        fprintf(file, "\tfmt_int: db \"%%lld\", 10, 0\n");
        fprintf(file, "\tfmt_scan: db \"%%lld\", 0\n");
        fprintf(file, "\tfmt_char: db \"%%c\", 0\n\n");
        fprintf(file, "section .bss\n");
        fprintf(file, "\tram: resq 65536\n");
        fprintf(file, "\tscan_buf: resq 1\n\n");
        fprintf(file, "section .text\n");
        fprintf(file, "\textern printf, scanf, exit\n");
        fprintf(file, "\tglobal main\n\n");
    }

    asm_info->counter = 0;

    if (IsThatOperation(root, kOperationFunction)) {
        PrintFunction(file, root, arr, ram_base, asm_info, 1);
    }

    if (root->left) {
        PrintProgram(file, root->left,  arr, ram_base, asm_info);
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

    if (is_main) {
        EMIT_LABEL("main:");
    }
    EMIT_LABEL("%s:", func_name);

    EmitPrologue(file, indent);

    if (is_main) {
        EMIT("xor r12d, r12d");
    }

    param_count = arr->var_array[func_node->left->value.pos].variable_value;

    if (param_count > 0) {
        EMIT("add r12, %d", param_count);
    }

    int frame_off = 16;
    PushParamsToRam(file, args, arr, *ram_base, param_count, asm_info, indent, &frame_off);

    *ram_base += param_count;
    PrintStatement(file, func_node->right->right, arr, *ram_base, param_count, asm_info, indent, NULL);
    *ram_base -= param_count;

    EmitEpilogue(file, indent);

    if (is_main) {
        EMIT("xor edi, edi");
        EMIT("call exit");
    } else {
        EMIT("ret");
    }
    fprintf(file, "\n");
}

static int FindVarPos(VariableArr *arr, LangNode_t *node, AsmInfo *asm_info) {
    assert(arr);
    assert(node);
    assert(asm_info);

    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (strcmp(arr->var_array[i].variable_name, arr->var_array[node->value.pos].variable_name) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = asm_info->counter++;
            } else {
                var_idx = arr->var_array[i].pos_in_code;
            }
        }
    }
    return var_idx;
}

static int ResolveVarShift(VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info) {
    assert(arr);
    assert(node);
    assert(asm_info);

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

    return var_idx - param_count;
}

static void StoreParamFromFrame(FILE *file, VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info, int indent, int frame_off) {
    assert(arr);
    assert(node);
    assert(asm_info);

    int shift = ResolveVarShift(arr, node, param_count, asm_info);
    EMIT_COMMENT("param [rbp+%d] -> ram[r12%+d]", frame_off, shift);
    EMIT("mov rax, [rbp + %d]", frame_off);
    EMIT_VAR_ADDR(shift);
    EMIT("mov [rcx], rax");
}

static void PopToVar(FILE *file, VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(arr);
    assert(node);
    assert(asm_info);

    int shift = ResolveVarShift(arr, node, param_count, asm_info);
    EMIT("pop rax");
    EMIT_VAR_ADDR(shift);
    EMIT("mov [rcx], rax");
}

static int CountArgs(LangNode_t *args_node) {
    if (!args_node) return 0;
    if (!IsThatOperation(args_node, kOperationComma)) return 1;

    return CountArgs(args_node->left) + CountArgs(args_node->right);
}

static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        PrintExpr(file, args_node, arr, ram_base, param_count, asm_info, indent, "push arg");
        return;
    }

    if (args_node->left) {
        PushParamsToStack(file, args_node->right, arr, ram_base, param_count, asm_info, indent);
    }

    if (args_node->right) {
        PushParamsToStack(file, args_node->left,  arr, ram_base, param_count, asm_info, indent);
    }
}

static void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, int *frame_off) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(frame_off);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        StoreParamFromFrame(file, arr, args_node, param_count, asm_info, indent, *frame_off);
        *frame_off += 8;
        return;
    }

    if (args_node->left) {
        PushParamsToRam(file, args_node->left,  arr, ram_base, param_count, asm_info, indent, frame_off);
    }

    if (args_node->right) {
        PushParamsToRam(file, args_node->right, arr, ram_base, param_count, asm_info, indent, frame_off);
    }
}

static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(comment);
    if (!expr) return;

    switch (expr->type) {
        case kNumber:
            EMIT("mov rax, %lld", (long long)expr->value.number);
            EMIT("push rax");
            break;

        case kVariable: {
            int shift = FindVarPos(arr, expr, asm_info) - param_count;
            EMIT_COMMENT("load var (shift=%d)", shift);
            EMIT_VAR_ADDR(shift);
            EMIT("push qword [rcx]");
            break;
        }

        case kOperation:
            PrintExprOperationCase(file, expr, arr, ram_base, param_count, asm_info, indent, comment);
            break;
    }
}

static void EmitBinaryOp(FILE *file, LangNode_t *node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, 
        int indent, const char *op_instr, const char *comment) {
    assert(file);
    assert(node);
    assert(arr);
    assert(asm_info);
    assert(op_instr);
    assert(comment);

    PrintExpr(file, node->left,  arr, ram_base, param_count, asm_info, indent, comment);
    PrintExpr(file, node->right, arr, ram_base, param_count, asm_info, indent, NULL);
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

static void PrintExprOperationCase(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment) {
    assert(file);
    assert(expr);
    assert(arr);
    assert(asm_info);
    assert(comment);

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    switch (expr->value.operation) {
        case kOperationCallAddr:
            PrintAddressOf(file, expr->left, arr, param_count, asm_info, indent, comment);
            break;

        case kOperationGetAddr:
            PrintDereference(file, expr->left, arr, param_count, asm_info, indent, comment);
            break;

        case kOperationSQRT:
            PrintExpr(file, expr->left, arr, ram_base, param_count, asm_info, indent, comment);
            EMIT("pop rax");
            EMIT("cvtsi2sd xmm0, rax");
            EMIT("sqrtsd xmm0, xmm0");
            EMIT("cvttsd2si rax, xmm0");
            EMIT("push rax");
            break;

        case kOperationAdd:
            EmitBinaryOp(file, expr, arr, ram_base, param_count, asm_info, indent, "add", comment);
            break;
        case kOperationSub:
            EmitBinaryOp(file, expr, arr, ram_base, param_count, asm_info, indent, "sub", comment);
            break;
        case kOperationMul:
            EmitBinaryOp(file, expr, arr, ram_base, param_count, asm_info, indent, "imul", comment);
            break;
        case kOperationDiv:
            EmitBinaryOp(file, expr, arr, ram_base, param_count, asm_info, indent, "idiv", comment);
            break;

        case kOperationCall: {
            PushParamsToStack(file, expr->right, arr, ram_base, param_count, asm_info, indent);
            const char *callee = arr->var_array[expr->left->value.pos].variable_name;
            EMIT("call %s", callee);

            int nargs = CountArgs(expr->right);
            if (nargs > 0) {
                EMIT("add rsp, %d", nargs * 8);
            }

            EMIT("push rax");
            break;
        }

        case kOperationArrPos: {
            int arr_base = FindVarPos(arr, expr->left, asm_info) - param_count;
            EMIT_COMMENT("array access [base shift=%d]", arr_base);
            EMIT("lea rcx, [rel ram]");
            EMIT("mov rdi, r12");

            if (arr_base >= 0) {
                EMIT("add rdi, %d", arr_base);
            } else {
                EMIT("sub rdi, %d", -arr_base);
            }

            PrintExpr(file, expr->right, arr, ram_base, param_count, asm_info, indent, comment);
            EMIT("pop rax");
            EMIT("add rdi, rax");
            EMIT("push qword [rcx + rdi*8]");
            break;
        }

        default:
            break;
    }
    #pragma clang diagnostic pop
}

static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(comment);
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            PrintStatementOperationCase(file, stmt, arr, ram_base, param_count, asm_info, indent, comment);
            break;

        case kVariable:
            PopToVar(file, arr, stmt, param_count, asm_info, indent);
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

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    switch (node->value.operation) {
        case kOperationA: return "jle";
        case kOperationAE: return "jl";
        case kOperationB: return "jge";
        case kOperationBE: return "jg";
        case kOperationE: return "jne";
        case kOperationNE: return "je";
        default: return "je";
    }
    #pragma clang diagnostic pop
}

static void PrintIfToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    LangNode_t *condition = stmt->left;

    PrintExpr(file, condition->left,  arr, ram_base, param_count, asm_info, indent, "if lhs");
    PrintExpr(file, condition->right, arr, ram_base, param_count, asm_info, indent, "if rhs");

    EMIT("pop rbx");
    EMIT("pop rax");
    EMIT("cmp rax, rbx");

    int this_if = asm_info->label_if++;
    int this_else = asm_info->label_else++;

    EMIT("%s .else_%d", ChooseCompareMode(condition), this_else);

    if (IsThatOperation(stmt->right, kOperationElse)) {
        PrintStatement(file, stmt->right->left, arr, ram_base, param_count, asm_info, indent + 1, "if-true");
        EMIT("jmp .end_if_%d", this_if);
    } else {
        PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info, indent + 1, "if-true");
        EMIT("jmp .end_if_%d", this_if);
    }

    EMIT_LABEL(".else_%d:", this_else);
    if (IsThatOperation(stmt->right, kOperationElse)) {
        PrintStatement(file, stmt->right->right, arr, ram_base, param_count, asm_info, indent, "else");
    }

    EMIT_LABEL(".end_if_%d:", this_if);
}

static void PrintWhileToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    int start_label = asm_info->label_counter++;
    int end_label = asm_info->label_counter++;

    EMIT_LABEL(".while_start_%d:", start_label);

    PrintExpr(file, stmt->left->left,  arr, ram_base, param_count, asm_info, indent, "while lhs");
    PrintExpr(file, stmt->left->right, arr, ram_base, param_count, asm_info, indent, "while rhs");

    EMIT("pop rbx");
    EMIT("pop rax");
    EMIT("cmp rax, rbx");
    EMIT("%s .while_end_%d", ChooseCompareMode(stmt->left), end_label);

    PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info, indent + 1, "while body");

    EMIT("jmp .while_start_%d", start_label);
    EMIT_LABEL(".while_end_%d:", end_label);
}

static void PrintReturn(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);
    assert(comment);

    PrintExpr(file, stmt->left, arr, ram_base, param_count, asm_info, indent, comment);

    EMIT("pop rax");

    EmitEpilogue(file, indent);
    EMIT("ret");
}

static void PrintArrDeclare(FILE *file, LangNode_t *stmt, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    arr->var_array[stmt->left->left->left->value.pos].pos_in_code = asm_info->counter;

    int arr_size = (int)stmt->left->left->right->value.number;
    EMIT_COMMENT("declare array[%d]", arr_size);

    for (int i = 0; i < arr_size; i++) {
        int shift = asm_info->counter + i - param_count;
        EMIT_VAR_ADDR(shift);
        EMIT("mov qword [rcx], 0");
    }

    asm_info->counter += arr_size;
}

static void PrintIsForArray(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    PrintExpr(file, stmt->right, arr, ram_base, param_count, asm_info, indent, "arr assign val");
    int arr_base = FindVarPos(arr, stmt->left->left, asm_info) - param_count;

    EMIT("lea rcx, [rel ram]");
    EMIT("mov rdi, r12");
    if (arr_base >= 0) {
        EMIT("add rdi, %d", arr_base);
    } else {
        EMIT("sub rdi, %d", -arr_base);
    }

    PrintExpr(file, stmt->left->right, arr, ram_base, param_count, asm_info, indent, "arr idx");
    EMIT("pop rax");
    EMIT("add rdi, rax");

    EMIT("pop rax");
    EMIT("mov [rcx + rdi*8], rax");
}

static void PrintAddressOf(FILE *file, LangNode_t *var_node, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent, const char *comment) {
    assert(file);
    assert(var_node);
    assert(arr);
    assert(asm_info);
    assert(comment);
    assert(var_node->type == kVariable);

    int shift = FindVarPos(arr, var_node, asm_info) - param_count;
    EMIT_COMMENT("address of (shift=%d) %s", shift, comment ? comment : "");
    EMIT_VAR_ADDR(shift);
    EMIT("push rcx");
}

static void PrintDereference(FILE *file, LangNode_t *ptr_node, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent, const char *comment) {
    assert(file);
    assert(ptr_node);
    assert(arr);
    assert(asm_info);
    assert(comment);

    PrintAddressOf(file, ptr_node, arr, param_count, asm_info, indent, comment);
    EMIT("pop rcx");
    EMIT("push qword [rcx]");
}

static void PrintAddressAssignment(FILE *file, LangNode_t *deref_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(deref_node);
    assert(arr);
    assert(asm_info);

    PrintExpr(file, deref_node->left, arr, ram_base, param_count, asm_info, indent, "addr assign");
    EMIT("pop rcx");
    EMIT("pop rax");
    EMIT("mov [rcx], rax");
}

static void EmitPrintInt(FILE *file, LangNode_t *node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment) {
    assert(file);
    assert(node);
    assert(arr);
    assert(asm_info);
    assert(comment);

    PrintExpr(file, node->left, arr, ram_base, param_count, asm_info, indent, comment);
    EMIT("pop rsi");
    EMIT("mov r13, rsp");
    EMIT("and rsp, -16");
    EMIT("lea rdi, [rel fmt_int]");
    EMIT("xor eax, eax");
    EMIT("call printf");
    EMIT("mov rsp, r13");
}

static void EmitPrintChar(FILE *file, LangNode_t *node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent, const char *comment) {
    assert(file);
    assert(node);
    assert(arr);
    assert(asm_info);
    assert(comment);

    PrintExpr(file, node->left, arr, ram_base, param_count, asm_info, indent, comment);
    EMIT("pop rsi");
    EMIT("mov r13, rsp");
    EMIT("and rsp, -16");
    EMIT("lea rdi, [rel fmt_char]");
    EMIT("xor eax, eax");
    EMIT("call printf");
    EMIT("mov rsp, r13");
}

static void EmitReadInt(FILE *file, int indent) {
    assert(file);

    EMIT("mov r13, rsp");
    EMIT("and rsp, -16");
    EMIT("lea rdi, [rel fmt_scan]");
    EMIT("lea rsi, [rel scan_buf]");
    EMIT("xor eax, eax");
    EMIT("call scanf");
    EMIT("mov rsp, r13");
    EMIT("push qword [rel scan_buf]");
}

static void PrintStatementOperationCase(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, 
        int indent, const char *comment) {
    assert(file);
    assert(arr);
    assert(asm_info);
    assert(comment);
    if (!stmt) return;

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    switch (stmt->value.operation) {
        case kOperationHLT:
            EMIT("xor edi, edi");
            EMIT("call exit");
            break;

        case kOperationCallAddr:
            PrintAddressOf(file, stmt->left, arr, param_count, asm_info, indent, comment);
            break;

        case kOperationGetAddr:
            PrintDereference(file, stmt->left, arr, param_count, asm_info, indent, comment);
            break;

        case kOperationCall: {
            PushParamsToStack(file, stmt->right, arr, ram_base, param_count, asm_info, indent);
            const char *callee = arr->var_array[stmt->left->value.pos].variable_name;
            EMIT("call %s", callee);
            int number_args = CountArgs(stmt->right);
            if (number_args > 0) {
                EMIT("add  rsp, %d", number_args * 8);
            }
            break;
        }

        case kOperationIs:
            if (IsThatOperation(stmt->left, kOperationArrPos)) {
                PrintIsForArray(file, stmt, arr, ram_base, param_count, asm_info, indent);
                break;
            }

            PrintExpr(file, stmt->right, arr, ram_base, param_count, asm_info, indent, comment);
            if (IsThatOperation(stmt->left, kOperationGetAddr)) {
                PrintAddressAssignment(file, stmt, arr, ram_base, param_count, asm_info, indent);
                break;
            }

            PrintStatement(file, stmt->left, arr, ram_base, param_count, asm_info, indent, "assign lhs");
            break;

        case kOperationReturn:
            PrintReturn(file, stmt, arr, ram_base, param_count, asm_info, indent, "return");
            break;

        case kOperationWrite:
            EmitPrintInt(file, stmt, arr, ram_base, param_count, asm_info, indent, comment);
            break;

        case kOperationWriteChar:
            EmitPrintChar(file, stmt, arr, ram_base, param_count, asm_info, indent, comment);
            break;

        case kOperationRead:
            EmitReadInt(file, indent);
            PopToVar(file, arr, stmt->left, param_count, asm_info, indent);
            break;

        case kOperationThen:
            PrintStatement(file, stmt->left,  arr, ram_base, param_count, asm_info, indent, "then L");
            PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info, indent, "then R");
            break;

        case kOperationIf:
            PrintIfToAsm(file, stmt, arr, ram_base, param_count, asm_info, indent);
            break;

        case kOperationWhile:
            PrintWhileToAsm(file, stmt, arr, ram_base, param_count, asm_info, indent);
            break;

        case kOperationTernary:
            PrintStatement(file, stmt->left->right, arr, ram_base, param_count, asm_info, indent, "ternary");
            PrintStatement(file, stmt->left->left,  arr, ram_base, param_count, asm_info, indent, "ternary");
            break;

        case kOperationArrDecl:
            PrintArrDeclare(file, stmt, arr, param_count, asm_info, indent);
            break;

        case kOperationDraw:
            EMIT_COMMENT("DRAW — not implemented for x86-64");
            break;

        default:
            PrintExpr(file, stmt, arr, ram_base, param_count, asm_info, indent, comment);
            break;
    }
    #pragma clang diagnostic pop
}

static void CleanPositions(VariableArr *arr) {
    assert(arr);

    for (size_t i = 0; i < arr->size; i++) {
        arr->var_array[i].pos_in_code = -1;
    }
}