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

#define DEFAULT_BUF_SIZE 64

typedef struct {
    int param_count;
    int frame_size;
    int indent;
} SubAsmInfo;

typedef struct {
    FILE *file;
    VariableArr *arr;
    AsmInfo *asm_info;
    SubAsmInfo *sub_info;
} AsmContext;

#define EMIT(fmt, ...)                                                                    \
    do {                                                                                  \
        for (int k = 0; k < context->sub_info->indent; k++) fprintf(context->file, "\t"); \
        fprintf(context->file, fmt "\n", ##__VA_ARGS__);                                  \
    } while (0)

#define EMIT_LABEL(fmt, ...)                              \
    do {                                                  \
        fprintf(context->file, fmt "\n", ##__VA_ARGS__);  \
    } while (0)

#define EMIT_COMMENT(fmt, ...)                                                            \
    do {                                                                                  \
        for (int k = 0; k < context->sub_info->indent; k++) fprintf(context->file, "\t"); \
        fprintf(context->file, "; " fmt "\n", ##__VA_ARGS__);                             \
    } while (0)

#define EMIT_BLANK()                              \
    do {                                          \
        fprintf(context->file, "\n");             \
    } while (0)

#define EMIT_SECTION(title)                                                               \
    do {                                                                                  \
        fprintf(context->file, "\n");                                                     \
        for (int k = 0; k < context->sub_info->indent; k++) fprintf(context->file, "\t"); \
        fprintf(context->file, "; --- %s ---\n", title);                                  \
    } while (0)

/* ----------------------------------------------------------------------
 * Frame layout (matches TreeToBin.c):
 *   [rbp + 16 + 8 * i]  -- i-th parameter   (i = 0 ... param_count - 1)
 *   [rbp + 8]           -- return address
 *   [rbp]               -- saved caller's rbp
 *   [rbp - 8 * 1]       -- 0th local cell
 *   [rbp - 8 * 2]       -- 1st local cell
 *   ...
 *
 * Arrays grow toward LOWER addresses: array[i] is at base - 8 * i.
 * ------------------------------------------------------------------- */

static int VarSlotDisp(int slot, int param_count) {
    if (slot < param_count) {
        return 16 + 8 * slot;
    }
    int local_index = slot - param_count;
    return -8 * (local_index + 1);
}

static void VarMemOperand(char *out, size_t capacity, int slot, int param_count) {
    assert(out);

    int disp = VarSlotDisp(slot, param_count);
    if (disp >= 0) {
        snprintf(out, capacity, "qword [rbp + %d]", disp);
    } else {
        snprintf(out, capacity, "qword [rbp - %d]", -disp);
    }
}

/* like VarMemOperand but without the size prefix - useful for lea */
static void VarAddrOperand(char *out, size_t capacity, int slot, int param_count) {
    assert(out);

    int disp = VarSlotDisp(slot, param_count);
    if (disp >= 0) {
        snprintf(out, capacity, "[rbp + %d]", disp);
    } else {
        snprintf(out, capacity, "[rbp - %d]", -disp);
    }
}

static const char *ChooseCompareMode(LangNode_t *node);
static void PrintFunction(LangNode_t *func_node, AsmContext *context);
static void PrintExpr(LangNode_t *expr, AsmContext *context);
static void PrintExprOperationCase(LangNode_t *expr, AsmContext *context);
static void PopToVar(LangNode_t *node, AsmContext *context);
static void PushParamsToStack(LangNode_t *args_node, AsmContext *context);
static void PrintStatement(LangNode_t *stmt, AsmContext *context);
static void PrintStatementOperationCase(LangNode_t *stmt, AsmContext *context);
static void PrintIfToAsm(LangNode_t *stmt, AsmContext *context);
static void PrintWhileToAsm(LangNode_t *stmt, AsmContext *context);
static void PrintReturn(LangNode_t *stmt, AsmContext *context);
static void PrintIsForArray(LangNode_t *stmt, AsmContext *context);
static void PrintArrDeclare(LangNode_t *stmt, AsmContext *context);
static void PrintAddressOf(LangNode_t *var_node, AsmContext *context);
static void PrintDereference(LangNode_t *ptr_node, AsmContext *context);
static void PrintAddressAssignment(LangNode_t *deref_node, AsmContext *context);

static int  GetVarSlot(VariableArr *arr, LangNode_t *node);
static void CountLocalSlots(LangNode_t *node, VariableArr *arr, AsmInfo *info);
static void AssignParamSlots(LangNode_t *args, VariableArr *arr, int *slot_counter);

static void EmitPrologue(AsmContext *context) {
    assert(context->file);

    EMIT_SECTION("prologue");
    EMIT("push rbp");
    EMIT("mov rbp, rsp");
    EMIT("push r12");
    EMIT("push rbx");
    if (context->sub_info->frame_size > 0) {
        EMIT("sub rsp, %d", context->sub_info->frame_size);
    }
}

static void EmitEpilogue(AsmContext *context) {
    assert(context->file);

    EMIT_SECTION("epilogue");
    if (context->sub_info->frame_size > 0) {
        EMIT("add rsp, %d", context->sub_info->frame_size);
    }

    EMIT("pop rbx");
    EMIT("pop r12");
    EMIT("pop rbp");
}

void PrintProgram(FILE *file, LangNode_t *root, VariableArr *arr, int *ram_base, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    (void)ram_base;
    if (!root) return;

    static bool header_printed = false;
    if (!header_printed) {
        header_printed = true;
        fprintf(file, ";---------------------------------------------\n");
        fprintf(file, "; (: Generated assembly :)\n");
        fprintf(file, ";---------------------------------------------\n\n");
        fprintf(file, "default rel\n\n");
        fprintf(file, "section .data\n");
        fprintf(file, "\tfmt_int:  db \"%%d\", 10, 0\n");
        fprintf(file, "\tfmt_char: db \"%%c\", 0\n\n");
        fprintf(file, "section .text\n");
        fprintf(file, "\tglobal main\n\n");
    }

    SubAsmInfo sub_info = {0, 0, 1};
    AsmContext ctx_val = {file, arr, asm_info, &sub_info};

    if (IsThatOperation(root, kOperationFunction)) {
        PrintFunction(root, &ctx_val);
    }

    if (root->left) {
        PrintProgram(file, root->left, arr, ram_base, asm_info);
    }

    if (root->right) {
        PrintProgram(file, root->right, arr, ram_base, asm_info);
    }
}

static void AssignParamSlots(LangNode_t *args, VariableArr *arr, int *slot_counter) {
    assert(arr);
    assert(slot_counter);
    if (!args) return;

    if (!IsThatOperation(args, kOperationComma)) {
        if (args->type == kVariable) {
            size_t pos = args->value.pos;
            const char *name = arr->var_array[pos].variable_name;

            for (size_t i = 0; i < arr->size; i++) {
                if (name && arr->var_array[i].variable_name && strcmp(arr->var_array[i].variable_name, name) == 0) {
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

static void CountLocalSlots(LangNode_t *node, VariableArr *arr, AsmInfo *info) {
    assert(arr);
    assert(info);
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
        if (name && arr->var_array[i].variable_name && strcmp(arr->var_array[i].variable_name, name) == 0) {
            if (arr->var_array[i].pos_in_code != -1) {
                return arr->var_array[i].pos_in_code;
            }

            break;
        }
    }

    fprintf(stderr, "Unknown variable: %s\n", name ? name : "(null)");
    return 0;
}

static void PrintFunction(LangNode_t *func_node, AsmContext *context) {
    assert(context->file);
    assert(context->arr);
    assert(context->asm_info);
    if (!func_node) return;

    CleanPositions(context->arr);
    context->asm_info->counter = 0;

    LangNode_t *args = func_node->right->left;
    LangNode_t *body = func_node->right->right;
    const char *func_name = context->arr->var_array[func_node->left->value.pos].variable_name;
    int is_main = (strcmp(MAIN, func_name) == 0);

    int param_count = context->arr->var_array[func_node->left->value.pos].variable_value;

    int slot_counter = 0;
    if (args) {
        AssignParamSlots(args, context->arr, &slot_counter);
    }

    if (slot_counter < param_count) {
        slot_counter = param_count;
    }

    context->asm_info->counter = slot_counter;
    CountLocalSlots(body, context->arr, context->asm_info);

    int total_slots = context->asm_info->counter;
    int local_slots = total_slots - param_count;
    if (local_slots < 0) local_slots = 0;

    int frame_size = local_slots * 8;
    if (frame_size % 16 != 0) frame_size += 8;

    SubAsmInfo sub_info_val = { param_count, frame_size, 1 };
    context->sub_info = &sub_info_val;

    fprintf(context->file, ";---------------------------------------------\n");
    fprintf(context->file, "; function: %s  (params=%d, locals=%d, frame=%d)\n", func_name, param_count, local_slots, frame_size);
    fprintf(context->file, ";---------------------------------------------\n");
    if (is_main) {
        EMIT_LABEL("main:");
    }
    EMIT_LABEL("%s:", func_name);

    EmitPrologue(context);

    EMIT_SECTION("function body");
    PrintStatement(body, context);

    EmitEpilogue(context);

    if (is_main) {
        EMIT_BLANK();
        EMIT_COMMENT("exit");
        EMIT("call my_exit");
    } else {
        EMIT("ret");
    }

    fprintf(context->file, "\n\n");
}

static void PopToVar(LangNode_t *node, AsmContext *context) {
    assert(context->file);
    assert(context->arr);
    assert(node);
    assert(context->sub_info);

    int slot = GetVarSlot(context->arr, node);
    char mem[DEFAULT_BUF_SIZE] = {};
    VarMemOperand(mem, sizeof(mem), slot, context->sub_info->param_count);

    EMIT_COMMENT("store to var (slot=%d)", slot);
    EMIT("pop %s", mem);                  // pop directly into the memory slot
}

static void PushParamsToStack(LangNode_t *args_node, AsmContext *context) {
    assert(context->file);
    assert(context->arr);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        PrintExpr(args_node, context);
        return;
    }

    if (args_node->left) {
        PushParamsToStack(args_node->right, context);
    }

    if (args_node->right) {
        PushParamsToStack(args_node->left, context);
    }
}

static void PrintExpr(LangNode_t *expr, AsmContext *context) {
    assert(context->file);
    assert(context->arr);
    assert(context->sub_info);
    if (!expr) return;

    switch (expr->type) {
        case kNumber:
            EMIT("mov rax, %lld", (long long)expr->value.number);
            EMIT("push rax");
            break;

        case kVariable: {
            int slot = GetVarSlot(context->arr, expr);
            char mem[DEFAULT_BUF_SIZE] = {};
            VarMemOperand(mem, sizeof(mem), slot, context->sub_info->param_count);

            EMIT_COMMENT("load var (slot=%d)", slot);
            EMIT("push %s", mem);          // push directly from memory slot
            break;
        }

        case kOperation: PrintExprOperationCase(expr, context); break;

        default:
            fprintf(stderr, "PrintExpr: unsupported type %d\n", expr->type);
            break;
    }
}

static void EmitBinaryOp(LangNode_t *node, AsmContext *context, const char *op_instr, const char *op_name) {
    assert(context->file);
    assert(node);
    assert(op_instr);

    EMIT_BLANK();
    EMIT_COMMENT("%s", op_name);
    PrintExpr(node->left, context);
    PrintExpr(node->right, context);
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

static void PrintExprOperationCase(LangNode_t *expr, AsmContext *context) {
    assert(context->file);
    assert(expr);
    assert(context->arr);
    assert(context->sub_info);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (expr->value.operation) {
        case kOperationCallAddr: PrintAddressOf(expr->left, context); break;
        case kOperationGetAddr: PrintDereference(expr->left, context); break;

        case kOperationSQRT:
            EMIT_BLANK();
            EMIT_COMMENT("sqrt");
            PrintExpr(expr->left, context);
            EMIT("pop rax");
            EMIT("cvtsi2sd xmm0, rax");
            EMIT("sqrtsd xmm0, xmm0");
            EMIT("cvttsd2si rax, xmm0");
            EMIT("push rax");
            break;

        case kOperationAdd: EmitBinaryOp(expr, context, "add", "addition"); break;
        case kOperationSub: EmitBinaryOp(expr, context, "sub", "subtraction"); break;
        case kOperationMul: EmitBinaryOp(expr, context, "imul", "multiplication"); break;
        case kOperationDiv: EmitBinaryOp(expr, context, "idiv", "division"); break;

        case kOperationCall: {
            const char *callee = context->arr->var_array[expr->left->value.pos].variable_name;
            int number_args = CountArgs(expr->right);
            EMIT_BLANK();
            EMIT_COMMENT("call %s (%d args), result to stack", callee, number_args);
            PushParamsToStack(expr->right, context);
            EMIT("call %s", callee);
            if (number_args > 0) {
                EMIT("add rsp, %d", number_args * 8);
            }

            EMIT("push rax");
            break;
        }

        case kOperationArrPos: {
            int slot = GetVarSlot(context->arr, expr->left);
            char addr[DEFAULT_BUF_SIZE] = {};
            VarAddrOperand(addr, sizeof(addr), slot, context->sub_info->param_count);

            EMIT_BLANK();
            EMIT_COMMENT("array read (base slot=%d)", slot);

            PrintExpr(expr->right, context);
            EMIT("pop rdi");

            EMIT("lea rcx, %s", addr);     // rcx = &array[0]
            EMIT("shl rdi, 3");
            EMIT("sub rcx, rdi");          // rcx = &array[i]
            EMIT("push qword [rcx]");
            break;
        }

        default:
            break;
    }
#pragma GCC diagnostic pop
}

static void PrintStatement(LangNode_t *stmt, AsmContext *context) {
    assert(context->file);
    assert(context->arr);
    assert(context->sub_info);
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation: PrintStatementOperationCase(stmt, context); break;
        case kVariable: PopToVar(stmt, context); break;

        case kNumber:
            EMIT("mov rax, %lld", (long long)stmt->value.number);
            EMIT("push rax");
            break;

        default:
            fprintf(stderr, "PrintStatement: unsupported type %d\n", stmt->type);
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

static void PrintIfToAsm(LangNode_t *stmt, AsmContext *context) {
    assert(context->file);
    assert(stmt);
    assert(context->sub_info);

    LangNode_t *condition = stmt->left;
    int this_if = context->asm_info->label_if++;
    int this_else = context->asm_info->label_else++;
    int has_else = IsThatOperation(stmt->right, kOperationElse);

    EMIT_SECTION("if");
    EMIT_COMMENT("evaluate condition");
    PrintExpr(condition->left, context);
    PrintExpr(condition->right, context);
    EMIT("pop rbx");
    EMIT("pop rax");
    EMIT("cmp rax, rbx");
    EMIT("%s .else_%d", ChooseCompareMode(condition), this_else);

    EMIT_BLANK();
    EMIT_COMMENT("then branch");
    context->sub_info->indent++;
    if (has_else) {
        PrintStatement(stmt->right->left, context);
    } else {
        PrintStatement(stmt->right, context);
    }

    EMIT("jmp .end_if_%d", this_if);
    context->sub_info->indent--;
    EMIT_BLANK();

    EMIT_LABEL(".else_%d:", this_else);
    if (has_else) {
        EMIT_COMMENT("else branch");
        PrintStatement(stmt->right->right, context);
    }

    EMIT_LABEL(".end_if_%d:", this_if);
}

static void PrintWhileToAsm(LangNode_t *stmt, AsmContext *context) {
    assert(context->file);
    assert(stmt);
    assert(context->sub_info);

    int start_label = context->asm_info->label_counter++;
    int end_label   = context->asm_info->label_counter++;

    EMIT_SECTION("while loop");
    EMIT_LABEL(".while_start_%d:", start_label);
    EMIT_COMMENT("evaluate condition");
    PrintExpr(stmt->left->left, context);
    PrintExpr(stmt->left->right, context);
    EMIT("pop rbx");
    EMIT("pop rax");
    EMIT("cmp rax, rbx");
    EMIT("%s .while_end_%d", ChooseCompareMode(stmt->left), end_label);

    EMIT_BLANK();
    EMIT_COMMENT("loop body");
    context->sub_info->indent++;
    PrintStatement(stmt->right, context);
    context->sub_info->indent--;
    EMIT_BLANK();
    EMIT("jmp .while_start_%d", start_label);
    EMIT_LABEL(".while_end_%d:", end_label);
}

static void PrintReturn(LangNode_t *stmt, AsmContext *context) {
    assert(context->file);
    assert(stmt);
    assert(context->sub_info);

    EMIT_SECTION("return");
    PrintExpr(stmt->left, context);
    EMIT("pop rax");
    EmitEpilogue(context);
    EMIT("ret");
}

static void PrintArrDeclare(LangNode_t *stmt, AsmContext *context) {
    assert(context->file);
    assert(stmt);
    assert(context->sub_info);

    int base_slot = context->arr->var_array[stmt->left->left->left->value.pos].pos_in_code;
    int arr_size = (int)stmt->left->left->right->value.number;

    EMIT_BLANK();
    EMIT_COMMENT("declare array[%d], base slot=%d", arr_size, base_slot);
    for (int i = 0; i < arr_size; i++) {
        int slot = base_slot + i;
        char mem[DEFAULT_BUF_SIZE] = {};
        VarMemOperand(mem, sizeof(mem), slot, context->sub_info->param_count);
        EMIT("mov %s, 0", mem);            // direct memory write, no lea
    }
}

static void PrintIsForArray(LangNode_t *stmt, AsmContext *context) {
    assert(context->file);
    assert(stmt);
    assert(context->sub_info);

    EMIT_BLANK();
    EMIT_COMMENT("array element assignment");

    /* stack: [value, index] */
    PrintExpr(stmt->right, context);                 // push value
    PrintExpr(stmt->left->right, context);           // push index

    EMIT("pop rdi");                                 // rdi = index
    EMIT("pop rax");                                 // rax = value

    int slot = GetVarSlot(context->arr, stmt->left->left);
    char addr[DEFAULT_BUF_SIZE] = {};
    VarAddrOperand(addr, sizeof(addr), slot, context->sub_info->param_count);

    EMIT("lea rcx, %s", addr);                       // rcx = &array[0]
    EMIT("shl rdi, 3");
    EMIT("sub rcx, rdi");                            // rcx = &array[index]
    EMIT("mov [rcx], rax");
}

static void PrintAddressOf(LangNode_t *var_node, AsmContext *context) {
    assert(context->file);
    assert(var_node);
    assert(context->sub_info);

    int slot = GetVarSlot(context->arr, var_node);
    char addr[DEFAULT_BUF_SIZE] = {};
    VarAddrOperand(addr, sizeof(addr), slot, context->sub_info->param_count);

    EMIT_BLANK();
    EMIT_COMMENT("address-of (slot=%d)", slot);
    EMIT("lea rcx, %s", addr);
    EMIT("push rcx");
}

static void PrintDereference(LangNode_t *ptr_node, AsmContext *context) {
    assert(context->file);
    assert(ptr_node);
    assert(context->sub_info);

    EMIT_COMMENT("dereference");
    PrintAddressOf(ptr_node, context);
    EMIT("pop rcx");
    EMIT("push qword [rcx]");
}

static void PrintAddressAssignment(LangNode_t *deref_node, AsmContext *context) {
    assert(context->file);
    assert(deref_node);
    assert(context->sub_info);

    EMIT_COMMENT("store via pointer");
    PrintExpr(deref_node->left, context);
    EMIT("pop rcx");
    EMIT("pop rax");
    EMIT("mov [rcx], rax");
}

static void EmitPrintInt(LangNode_t *node, AsmContext *context) {
    assert(node);
    assert(context);

    EMIT_BLANK();
    EMIT_COMMENT("print integer");
    PrintExpr(node->left, context);

    EMIT("pop rsi");
    EMIT("lea rdi, [fmt_int]");
    EMIT("mov r12, rsp");
    EMIT("and rsp, -16");
    EMIT("xor eax, eax");
    EMIT("call my_printf");
    EMIT("mov rsp, r12");
}

static void EmitPrintChar(LangNode_t *node, AsmContext *context) {
    assert(node);
    assert(context);

    EMIT_BLANK();
    EMIT_COMMENT("print character");
    PrintExpr(node->left, context);

    EMIT("pop rsi");
    EMIT("lea rdi, [fmt_char]");
    EMIT("mov r12, rsp");
    EMIT("and rsp, -16");
    EMIT("xor eax, eax");
    EMIT("call my_printf");
    EMIT("mov rsp, r12");
}

static void EmitReadInt(AsmContext *context) {
    assert(context);

    EMIT_BLANK();
    EMIT_COMMENT("read integer from stdin");
    EMIT("mov r12, rsp");
    EMIT("and rsp, -16");
    EMIT("call my_scanf");
    EMIT("mov rsp, r12");
    EMIT("push rax");
}

static void EmitDraw(LangNode_t *node, AsmContext *context) {
    assert(node);
    assert(context);

    EMIT_BLANK();
    EMIT_COMMENT("draw");

    PrintAddressOf(node->left, context);
    EMIT("pop rdi");
    EMIT("mov r12, rsp");
    EMIT("and rsp, -16");
    EMIT("call my_draw");
    EMIT("mov rsp, r12");
}

static void PrintStatementOperationCase(LangNode_t *stmt, AsmContext *context) {
    assert(context->file);
    assert(context->arr);
    assert(context->sub_info);
    if (!stmt) return;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (stmt->value.operation) {
        case kOperationHLT:
            EMIT_BLANK();
            EMIT_COMMENT("halt");
            EMIT("call my_exit");
            break;

        case kOperationCallAddr: PrintAddressOf(stmt->left, context); break;
        case kOperationGetAddr: PrintDereference(stmt->left, context); break;

        case kOperationCall: {
            const char *callee = context->arr->var_array[stmt->left->value.pos].variable_name;
            int number_args = CountArgs(stmt->right);

            EMIT_BLANK();
            EMIT_COMMENT("call %s (%d args), discard result", callee, number_args);
            PushParamsToStack(stmt->right, context);
            EMIT("call %s", callee);
            if (number_args > 0) {
                EMIT("add rsp, %d", number_args * 8);
            }

            break;
        }

        case kOperationIs:
            if (IsThatOperation(stmt->left, kOperationArrPos)) {
                PrintIsForArray(stmt, context);
                break;
            }

            EMIT_BLANK();
            EMIT_COMMENT("assignment");
            PrintExpr(stmt->right, context);
            if (IsThatOperation(stmt->left, kOperationGetAddr)) {
                PrintAddressAssignment(stmt, context);
                break;
            }

            PrintStatement(stmt->left, context);
            break;

        case kOperationReturn: PrintReturn(stmt, context); break;
        case kOperationWrite: EmitPrintInt(stmt, context); break;
        case kOperationWriteChar: EmitPrintChar(stmt, context); break;
        case kOperationRead: EmitReadInt(context); PopToVar(stmt->left, context); break;

        case kOperationThen:
            PrintStatement(stmt->left, context);
            PrintStatement(stmt->right, context);
            break;

        case kOperationIf: PrintIfToAsm(stmt, context); break;
        case kOperationWhile: PrintWhileToAsm(stmt, context); break;

        case kOperationTernary:
            EMIT_BLANK();
            EMIT_COMMENT("ternary");
            PrintStatement(stmt->left->right, context);
            PrintStatement(stmt->left->left, context);
            break;

        case kOperationArrDecl: PrintArrDeclare(stmt, context); break;
        case kOperationDraw: EmitDraw(stmt, context); break;

        default: PrintExpr(stmt, context); break;
    }
#pragma GCC diagnostic pop
}