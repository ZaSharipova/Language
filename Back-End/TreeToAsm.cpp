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
    FILE *file;
    VariableArr *arr;
    AsmInfo *asm_info;
    SubAsmInfo *sub_info;
} AsmGroup;

#define FPRINTF(fmt, ...)                                                         \
    do {                                                                          \
        for (int k = 0; k < ctx->sub_info->indent; k++) fprintf(ctx->file, "\t"); \
        fprintf(ctx->file, fmt "\n", ##__VA_ARGS__);                              \
    } while(0)

#define FPRINTF_LABEL(fmt, ...)                      \
    do {                                             \
        fprintf(ctx->file, fmt "\n", ##__VA_ARGS__); \
    } while(0)

#define CASE_BINARY_OP(node, op_name, asm_op, ctx)         \
    case kOperation##op_name:                              \
        PrintExpr(node->left, ctx);                        \
        ctx->sub_info->comment = NULL;                     \
        PrintExpr(node->right, ctx);                       \
        FPRINTF(#asm_op "\n");                             \
        break

#define CASE_UNARY_OP(node, op_name, asm_op, ctx)          \
    case kOperation##op_name:                              \
        PrintExpr(node->left, ctx);                        \
        FPRINTF(#asm_op "\n");                             \
        break

static const char *ChooseCompareMode(LangNode_t *node);

static void PrintFunction(LangNode_t *func_node, int *ram_base, AsmGroup *ctx);
static void PrintExpr(LangNode_t *expr, AsmGroup *ctx);
static void PrintExprOperationCase(LangNode_t *expr, AsmGroup *ctx);
static void FindVarPosPopMN(LangNode_t *node, AsmGroup *ctx);
static void PushParamsToStack(LangNode_t *args_node, AsmGroup *ctx);
static void PushParamsToRam(LangNode_t *args_node, AsmGroup *ctx);
static void PrintStatement(LangNode_t *stmt, AsmGroup *ctx);
static void PrintStatementOperationCase(LangNode_t *stmt, AsmGroup *ctx);

static void PrintIfToAsm(LangNode_t *stmt, AsmGroup *ctx);
static void PrintWhileToAsm(LangNode_t *stmt, AsmGroup *ctx);
static void PrintReturn(LangNode_t *stmt, AsmGroup *ctx);

static void PrintIsForArray(LangNode_t *stmt, AsmGroup *ctx);
static void PrintArrDeclare(LangNode_t *stmt, AsmGroup *ctx);
static void PrintAddressOf(LangNode_t *var_node, AsmGroup *ctx);
static void PrintDereference(LangNode_t *ptr_node, AsmGroup *ctx);
static void PrintAddressAssignment(LangNode_t *deref_node, AsmGroup *ctx);
static void CountValueWithParamCount(int shift, const char *add_sub, const char *reg, AsmGroup *ctx);

void PrintProgram(FILE *file, LangNode_t *root, VariableArr *arr, int *ram_base, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(asm_info);
    if (!root) return;

    asm_info->counter = 0;

    SubAsmInfo sub_info = {0};
    AsmGroup ctx_val = {file, arr, asm_info, &sub_info};

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

static void PrintFunction(LangNode_t *func_node, int *ram_base, AsmGroup *ctx) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(ctx->asm_info);
    if (!func_node) return;

    int param_count = 0;
    CleanPositions(ctx->arr);
    ctx->asm_info->counter = 0;

    LangNode_t *args = func_node->right->left;

    FPRINTF_LABEL(":%s", ctx->arr->var_array[func_node->left->value.pos].variable_name);
    param_count = ctx->arr->var_array[func_node->left->value.pos].variable_value;

    SubAsmInfo sub_info_val = {*ram_base, param_count, ctx->sub_info->indent, "pushing parameters"};
    ctx->sub_info = &sub_info_val;

    CountValueWithParamCount(param_count, "ADD", "RAX\n", ctx);
    PushParamsToRam(args, ctx);

    *ram_base += param_count;
    ctx->sub_info->ram_base = *ram_base;
    ctx->sub_info->comment = "printing statement";
    PrintStatement(func_node->right->right, ctx);

    ctx->sub_info->comment = "pushing parameters";
    CountValueWithParamCount(param_count, "SUB", "RAX\n", ctx);
    *ram_base -= param_count;

    if (strcmp(MAIN, ctx->arr->var_array[func_node->left->value.pos].variable_name) != 0) {
        FPRINTF("RET\n");
    } else {
        FPRINTF("HLT\n");
    }
}

static void FindVarPosPopMN(LangNode_t *node, AsmGroup *ctx) {
    assert(ctx->file);
    assert(ctx->arr);
    assert(node);
    assert(ctx->asm_info);

    int var_idx = -1;
    LangNode_t *check_node = node;

    for (size_t i = 0; i < ctx->arr->size; i++) {
        if (IsThatOperation(node, kOperationGetAddr) || IsThatOperation(node, kOperationCallAddr)) {
            check_node = node->left;
        }

        if (ctx->arr->var_array[check_node->value.pos].variable_name && ctx->arr->var_array[i].variable_name
                && strcmp(ctx->arr->var_array[i].variable_name, ctx->arr->var_array[check_node->value.pos].variable_name) == 0) {

            if (ctx->arr->var_array[i].pos_in_code == -1) {
                var_idx = ctx->arr->var_array[i].pos_in_code = ctx->asm_info->counter++;
            } else {
                var_idx = ctx->arr->var_array[i].pos_in_code;
            }

            ctx->sub_info->comment = "counting pos for popm";
            CountValueWithParamCount((-1) * ctx->sub_info->param_count + var_idx, "ADD", "RCX", ctx);
            FPRINTF("POPM [RCX]\n");
            break;
        }
    }

    if (var_idx == -1) {
        fprintf(stderr, "Unknown variable\n");
        return;
    }
}

static void PushParamsToStack(LangNode_t *args_node, AsmGroup *ctx) {
    assert(ctx->arr);
    assert(ctx->asm_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        ctx->sub_info->comment = "counting expression in order to push params to stack";
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

static void PushParamsToRam(LangNode_t *args_node, AsmGroup *ctx) {
    assert(ctx->arr);
    assert(ctx->asm_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        FindVarPosPopMN(args_node, ctx);
        return;
    }

    if (args_node->left) {
        PushParamsToRam(args_node->left, ctx);
    }
    if (args_node->right) {
        PushParamsToRam(args_node->right, ctx);
    }
}

static void PrintStatement(LangNode_t *stmt, AsmGroup *ctx) {
    assert(ctx->arr);
    assert(ctx->asm_info);
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            PrintStatementOperationCase(stmt, ctx);
            break;

        case kVariable:
            FindVarPosPopMN(stmt, ctx);
            break;

        case kNumber:
            FPRINTF("PUSH %.0f", stmt->value.number);
            break;

        default:
            fprintf(stderr, "No such switch case.\n");
            break;
    }
}

static void PrintExpr(LangNode_t *expr, AsmGroup *ctx) {
    assert(ctx->arr);
    assert(ctx->asm_info);
    if (!expr) return;

    switch (expr->type) {
        case kNumber:
            FPRINTF("PUSH %.0f", expr->value.number);
            break;

        case kVariable:
            ctx->sub_info->comment = "finding pos for variable";
            CountValueWithParamCount((-1) * ctx->sub_info->param_count + FindVarPos(ctx->arr, expr, ctx->asm_info), "ADD", "RCX", ctx);
            FPRINTF("PUSHM [RCX]\n");
            break;

        case kOperation:
            PrintExprOperationCase(expr, ctx);
            break;

        default:
            printf("no such option in expr->type.\n");
    }
}

static const char *ChooseCompareMode(LangNode_t *node) {
    if (!node || node->type != kOperation) {
        return "NULL";
    }

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (node->value.operation) {
        case kOperationA:  return "JBE";
        case kOperationAE: return "JB";
        case kOperationB:  return "JAE";
        case kOperationBE: return "JA";
        case kOperationE:  return "JNE";
        case kOperationNE: return "JE";
        default:           return "NULL";
    }
    #pragma GCC diagnostic pop
}

static void PrintIfToAsm(LangNode_t *stmt, AsmGroup *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);

    LangNode_t *condition = stmt->left;

    ctx->sub_info->comment = "if condition left part";
    PrintExpr(condition->left, ctx);
    ctx->sub_info->comment = "if condition right part";
    PrintExpr(condition->right, ctx);

    int this_if = ctx->asm_info->label_if++;
    int this_else = ctx->asm_info->label_else++;

    FPRINTF("%s :else_%d", ChooseCompareMode(condition), this_else);

    ctx->sub_info->indent++;
    ctx->sub_info->comment = "if";
    if (IsThatOperation(stmt->right, kOperationElse)) {
        PrintStatement(stmt->right->left, ctx);
        FPRINTF("JMP :end_if_%d", this_if);
    } else {
        PrintStatement(stmt->right, ctx);
        FPRINTF("JMP :end_if_%d", this_if);
    }

    FPRINTF_LABEL("\n:else_%d", this_else);
    ctx->sub_info->indent--;
    ctx->sub_info->comment = "else";
    if (IsThatOperation(stmt->right, kOperationElse)) {
        PrintStatement(stmt->right->right, ctx);
    }

    FPRINTF_LABEL(":end_if_%d", this_if);
}

static void PrintWhileToAsm(LangNode_t *stmt, AsmGroup *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);

    int start_label = ctx->asm_info->label_counter++;
    int end_label = ctx->asm_info->label_counter++;

    FPRINTF_LABEL("\n:while_start_%d", start_label);

    ctx->sub_info->comment = "condition";
    PrintExpr(stmt->left->left, ctx);
    ctx->sub_info->comment = "while body";
    PrintExpr(stmt->left->right, ctx);

    FPRINTF("%s :while_end_%d", ChooseCompareMode(stmt->left), end_label);

    ctx->sub_info->indent++;
    ctx->sub_info->comment = "while body 2.0";
    PrintStatement(stmt->right, ctx);

    ctx->sub_info->indent--;
    FPRINTF("JMP :while_start_%d", start_label);
    FPRINTF_LABEL("\n:while_end_%d", end_label);
}

static void PrintReturn(LangNode_t *stmt, AsmGroup *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);

    PrintExpr(stmt->left, ctx);
    ctx->sub_info->comment = "counting pos for return";
    CountValueWithParamCount(ctx->sub_info->param_count, "SUB", "RAX\n", ctx);

    FPRINTF("RET\n");
}

static void PrintArrDeclare(LangNode_t *stmt, AsmGroup *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);

    ctx->arr->var_array[stmt->left->left->left->value.pos].pos_in_code = ctx->asm_info->counter;

    FPRINTF(";declaring array");
    for (size_t i = 0; i < (size_t)stmt->left->left->right->value.number; i++) {
        FPRINTF("PUSH 0");
        ctx->sub_info->comment = "counting pos for declaring array";
        CountValueWithParamCount((-1) * ctx->sub_info->param_count + ctx->asm_info->counter + (int)i, "ADD", "RCX", ctx);
        FPRINTF("POPM [RCX]\n");
    }

    ctx->asm_info->counter += (int)stmt->left->left->right->value.number;
}

static void PrintAddressOf(LangNode_t *var_node, AsmGroup *ctx) {
    assert(ctx->file);
    assert(var_node);
    assert(ctx->arr);
    assert(ctx->asm_info);
    assert(var_node->type == kVariable);

    FPRINTF("PUSHR RAX ;%s", ctx->sub_info->comment ? ctx->sub_info->comment : "");
    FPRINTF("PUSH %d", (-1) * ctx->sub_info->param_count + FindVarPos(ctx->arr, var_node, ctx->asm_info));
    FPRINTF("ADD");
}

static void PrintDereference(LangNode_t *ptr_node, AsmGroup *ctx) {
    assert(ctx->file);
    assert(ptr_node);
    assert(ctx->arr);
    assert(ctx->asm_info);

    PrintAddressOf(ptr_node, ctx);
    FPRINTF("POPR RCX");
    FPRINTF("PUSHM [RCX]");
}

static void PrintAddressAssignment(LangNode_t *deref_node, AsmGroup *ctx) {
    assert(ctx->file);
    assert(deref_node);
    assert(ctx->arr);
    assert(ctx->asm_info);

    ctx->sub_info->comment = "address assignment";
    PrintExpr(deref_node->left, ctx);
    FPRINTF("POPR RCX");
    FPRINTF("POPM [RCX]\n");
}

static void PrintIsForArray(LangNode_t *stmt, AsmGroup *ctx) {
    assert(ctx->file);
    assert(stmt);
    assert(ctx->arr);
    assert(ctx->asm_info);

    ctx->sub_info->comment = "expr for array is";
    PrintExpr(stmt->right, ctx);
    FPRINTF("PUSHR RAX");
    FPRINTF("PUSH %d", (-1) * ctx->sub_info->param_count + FindVarPos(ctx->arr, stmt->left->left, ctx->asm_info));

    ctx->sub_info->comment = "arr pos for array is";
    PrintExpr(stmt->left->right, ctx);
    FPRINTF("ADD");
    FPRINTF("ADD");
    FPRINTF("POPR RCX");
    FPRINTF("POPM [RCX]");
}

static void CountValueWithParamCount(int shift, const char *add_sub, const char *reg, AsmGroup *ctx) {
    assert(add_sub);
    assert(reg);

    FPRINTF("PUSHR RAX ;%s", ctx->sub_info->comment ? ctx->sub_info->comment : "");
    FPRINTF("PUSH %d", shift);
    FPRINTF("%s", add_sub);
    FPRINTF("POPR %s", reg);
}

static void PrintStatementOperationCase(LangNode_t *stmt, AsmGroup *ctx) {
    assert(ctx->arr);
    assert(ctx->asm_info);
    if (!stmt) return;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (stmt->value.operation) {
        case kOperationHLT:
            FPRINTF("HLT\n");
            break;

        case kOperationCallAddr:
            PrintAddressOf(stmt->left, ctx);
            break;

        case kOperationGetAddr:
            PrintDereference(stmt->left, ctx);
            break;

        case kOperationCall:
            PushParamsToStack(stmt->right, ctx);
            FPRINTF("CALL :%s\n", ctx->arr->var_array[stmt->left->value.pos].variable_name);
            break;

        case kOperationIs:
            if (IsThatOperation(stmt->left, kOperationArrPos)) {
                PrintIsForArray(stmt, ctx);
                break;
            }
            PrintExpr(stmt->right, ctx);
            if (IsThatOperation(stmt->left, kOperationGetAddr)) {
                PrintAddressAssignment(stmt, ctx);
                break;
            }
            ctx->sub_info->comment = "is left part";
            PrintStatement(stmt->left, ctx);
            break;

        case kOperationReturn:
            ctx->sub_info->comment = "return";
            PrintReturn(stmt, ctx);
            break;

        CASE_UNARY_OP(stmt, Write, OUT, ctx);
        CASE_UNARY_OP(stmt, WriteChar, OUTC, ctx);

        case kOperationRead:
            FPRINTF("IN\n");
            FindVarPosPopMN(stmt->left, ctx);
            break;

        case kOperationThen:
            ctx->sub_info->comment = "then left";
            PrintStatement(stmt->left, ctx);
            ctx->sub_info->comment = "then right";
            PrintStatement(stmt->right, ctx);
            break;

        case kOperationIf:
            PrintIfToAsm(stmt, ctx);
            break;

        case kOperationWhile:
            PrintWhileToAsm(stmt, ctx);
            break;

        case kOperationTernary:
            ctx->sub_info->comment = "ternary";
            PrintStatement(stmt->left->right, ctx);
            PrintStatement(stmt->left->left, ctx);
            break;

        case kOperationArrDecl:
            PrintArrDeclare(stmt, ctx);
            break;

        case kOperationDraw:
            FPRINTF("DRAW");
            break;

        default:
            PrintExpr(stmt, ctx);
            break;
    }
    #pragma GCC diagnostic pop
}

static void PrintExprOperationCase(LangNode_t *expr, AsmGroup *ctx) {
    assert(ctx->file);
    assert(expr);
    assert(ctx->arr);
    assert(ctx->asm_info);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (expr->value.operation) {
        case kOperationCallAddr:
            PrintAddressOf(expr->left, ctx);
            break;

        case kOperationGetAddr:
            PrintDereference(expr->left, ctx);
            break;

        CASE_UNARY_OP(expr, SQRT, SQRT, ctx);
        CASE_BINARY_OP(expr, Add, ADD, ctx);
        CASE_BINARY_OP(expr, Sub, SUB, ctx);
        CASE_BINARY_OP(expr, Mul, MUL, ctx);
        CASE_BINARY_OP(expr, Div, DIV, ctx);

        case kOperationCall:
            PushParamsToStack(expr->right, ctx);
            FPRINTF("CALL :%s\n", ctx->arr->var_array[expr->left->value.pos].variable_name);
            break;

        case kOperationArrPos:
            FPRINTF("PUSHR RAX ;%s", "finding array pos");
            FPRINTF("PUSH %d", (-1) * ctx->sub_info->param_count + FindVarPos(ctx->arr, expr->left, ctx->asm_info));
            PrintExpr(expr->right, ctx);
            FPRINTF("ADD");
            FPRINTF("POPR RCX");
            FPRINTF("PUSHM [RCX]\n");

        default:
            break;
    }
    #pragma GCC diagnostic pop
}