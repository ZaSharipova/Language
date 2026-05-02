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

#define FPRINTF(fmt, ...)                                                 \
    do {                                                                  \
        for (int k = 0; k < sub_info->indent; k++) fprintf(file, "\t");   \
        fprintf(file, fmt "\n", ##__VA_ARGS__);                           \
    } while(0)

#define FPRINTF_LABEL(fmt, ...)                 \
    do {                                        \
        fprintf(file, fmt "\n", ##__VA_ARGS__); \
    } while(0)

#define CASE_BINARY_OP(node, op_name, asm_op, sub_info)        \
    case kOperation##op_name:                                  \
        PrintExpr(file, node->left, arr, asm_info, sub_info);  \
        sub_info->comment = NULL;                              \
        PrintExpr(file, node->right, arr, asm_info, sub_info); \
        FPRINTF(#asm_op "\n");                                 \
        break

#define CASE_UNARY_OP(node, op_name, asm_op, sub_info)         \
    case kOperation##op_name:                                  \
        PrintExpr(file, node->left, arr, asm_info, sub_info);  \
        FPRINTF(#asm_op "\n");                                 \
        break

static const char *ChooseCompareMode(LangNode_t *node);

static void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base, AsmInfo *asm_info, int indent);
static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PrintExprOperationCase(FILE *file, LangNode_t *expr, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void FindVarPosPopMN(FILE *file, VariableArr *arr, LangNode_t *node, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
static void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info);
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
static void CountValueWithParamCount(FILE *file, int shift, const char *add_sub, const char *reg, SubAsmInfo *sub_info);

void PrintProgram(FILE *file, LangNode_t *root, VariableArr *arr, int *ram_base, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(asm_info);
    if (!root) return;

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

    FPRINTF_LABEL(":%s", arr->var_array[func_node->left->value.pos].variable_name);
    param_count = arr->var_array[func_node->left->value.pos].variable_value;

    SubAsmInfo sub_info_val = {*ram_base, param_count, indent, "pushing parameters"};
    SubAsmInfo *sub_info = &sub_info_val;

    CountValueWithParamCount(file, param_count, "ADD", "RAX\n", sub_info);
    PushParamsToRam(file, args, arr, asm_info, sub_info);

    *ram_base += param_count;
    sub_info->ram_base = *ram_base;
    sub_info->comment = "printing statement";
    PrintStatement(file, func_node->right->right, arr, asm_info, sub_info);

    sub_info->comment = "pushing parameters";
    CountValueWithParamCount(file, param_count, "SUB", "RAX\n", sub_info);
    *ram_base -= param_count;

    if (strcmp(MAIN, arr->var_array[func_node->left->value.pos].variable_name) != 0) {
        FPRINTF("RET\n");
    } else {
        FPRINTF("HLT\n");
    }
}

static void FindVarPosPopMN(FILE *file, VariableArr *arr, LangNode_t *node, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(node);
    assert(asm_info);

    int var_idx = -1;
    LangNode_t *check_node = node;

    for (size_t i = 0; i < arr->size; i++) {
        if (IsThatOperation(node, kOperationGetAddr) || IsThatOperation(node, kOperationCallAddr)) {
            check_node = node->left;
        }

        if (arr->var_array[check_node->value.pos].variable_name && arr->var_array[i].variable_name
                && strcmp(arr->var_array[i].variable_name, arr->var_array[check_node->value.pos].variable_name) == 0) {

            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = asm_info->counter++;
            } else {
                var_idx = arr->var_array[i].pos_in_code;
            }

            sub_info->comment = "counting pos for popm";
            CountValueWithParamCount(file, (-1) * sub_info->param_count + var_idx, "ADD", "RCX", sub_info);
            FPRINTF("POPM [RCX]\n");
            break;
        }
    }

    if (var_idx == -1) {
        fprintf(stderr, "Unknown variable\n");
        return;
    }
}

static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        sub_info->comment = "counting expression in order to push params to stack";
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

static void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        FindVarPosPopMN(file, arr, args_node, asm_info, sub_info);
        return;
    }

    if (args_node->left) {
        PushParamsToRam(file, args_node->left, arr, asm_info, sub_info);
    }
    if (args_node->right) {
        PushParamsToRam(file, args_node->right, arr, asm_info, sub_info);
    }
}

static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            PrintStatementOperationCase(file, stmt, arr, asm_info, sub_info);
            break;

        case kVariable:
            FindVarPosPopMN(file, arr, stmt, asm_info, sub_info);
            break;

        case kNumber:
            FPRINTF("PUSH %.0f", stmt->value.number);
            break;

        default:
            fprintf(stderr, "No such switch case.\n");
            break;
    }
}

static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr,
        AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!expr) return;

    switch (expr->type) {
        case kNumber:
            FPRINTF("PUSH %.0f", expr->value.number);
            break;

        case kVariable:
            sub_info->comment = "finding pos for variable";
            CountValueWithParamCount(file, (-1) * sub_info->param_count + FindVarPos(arr, expr, asm_info), "ADD", "RCX", sub_info);
            FPRINTF("PUSHM [RCX]\n");
            break;

        case kOperation:
            PrintExprOperationCase(file, expr, arr, asm_info, sub_info);
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

static void PrintIfToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    LangNode_t *condition = stmt->left;

    sub_info->comment = "if condition left part";
    PrintExpr(file, condition->left, arr, asm_info, sub_info);
    sub_info->comment = "if condition right part";
    PrintExpr(file, condition->right, arr, asm_info, sub_info);

    int this_if = asm_info->label_if++;
    int this_else = asm_info->label_else++;

    FPRINTF("%s :else_%d", ChooseCompareMode(condition), this_else);

    sub_info->indent++;
    sub_info->comment = "if";
    if (IsThatOperation(stmt->right, kOperationElse)) {
        PrintStatement(file, stmt->right->left, arr, asm_info, sub_info);
        FPRINTF("JMP :end_if_%d", this_if);
    } else {
        PrintStatement(file, stmt->right, arr, asm_info, sub_info);
        FPRINTF("JMP :end_if_%d", this_if);
    }

    FPRINTF_LABEL("\n:else_%d", this_else);
    sub_info->indent--;
    sub_info->comment = "else";
    if (IsThatOperation(stmt->right, kOperationElse)) {
        PrintStatement(file, stmt->right->right, arr, asm_info, sub_info);
    }

    FPRINTF_LABEL(":end_if_%d", this_if);
}

static void PrintWhileToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    int start_label = asm_info->label_counter++;
    int end_label = asm_info->label_counter++;

    FPRINTF_LABEL("\n:while_start_%d", start_label);

    sub_info->comment = "condition";
    PrintExpr(file, stmt->left->left, arr, asm_info, sub_info);
    sub_info->comment = "while body";
    PrintExpr(file, stmt->left->right, arr, asm_info, sub_info);

    FPRINTF("%s :while_end_%d", ChooseCompareMode(stmt->left), end_label);

    sub_info->indent++;
    sub_info->comment = "while body 2.0";
    PrintStatement(file, stmt->right, arr, asm_info, sub_info);

    sub_info->indent--;
    FPRINTF("JMP :while_start_%d", start_label);
    FPRINTF_LABEL("\n:while_end_%d", end_label);
}

static void PrintReturn(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    PrintExpr(file, stmt->left, arr, asm_info, sub_info);
    sub_info->comment = "counting pos for return";
    CountValueWithParamCount(file, sub_info->param_count, "SUB", "RAX\n", sub_info);

    FPRINTF("RET\n");
}

static void PrintArrDeclare(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    arr->var_array[stmt->left->left->left->value.pos].pos_in_code = asm_info->counter;

    FPRINTF(";declaring array");
    for (size_t i = 0; i < (size_t)stmt->left->left->right->value.number; i++) {
        FPRINTF("PUSH 0");
        sub_info->comment = "counting pos for declaring array";
        CountValueWithParamCount(file, (-1) * sub_info->param_count + asm_info->counter + (int)i, "ADD", "RCX", sub_info);
        FPRINTF("POPM [RCX]\n");
    }

    asm_info->counter += (int)stmt->left->left->right->value.number;
}

static void PrintAddressOf(FILE *file, LangNode_t *var_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(var_node);
    assert(arr);
    assert(asm_info);
    assert(var_node->type == kVariable);

    FPRINTF("PUSHR RAX ;%s", sub_info->comment ? sub_info->comment : "");
    FPRINTF("PUSH %d", (-1) * sub_info->param_count + FindVarPos(arr, var_node, asm_info));
    FPRINTF("ADD");
}

static void PrintDereference(FILE *file, LangNode_t *ptr_node, VariableArr *arr,
    AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(ptr_node);
    assert(arr);
    assert(asm_info);

    PrintAddressOf(file, ptr_node, arr, asm_info, sub_info);
    FPRINTF("POPR RCX");
    FPRINTF("PUSHM [RCX]");
}

static void PrintAddressAssignment(FILE *file, LangNode_t *deref_node, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(deref_node);
    assert(arr);
    assert(asm_info);

    sub_info->comment = "address assignment";
    PrintExpr(file, deref_node->left, arr, asm_info, sub_info);
    FPRINTF("POPR RCX");
    FPRINTF("POPM [RCX]\n");
}

static void PrintIsForArray(FILE *file, LangNode_t *stmt, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    sub_info->comment = "expr for array is";
    PrintExpr(file, stmt->right, arr, asm_info, sub_info);
    FPRINTF("PUSHR RAX");
    FPRINTF("PUSH %d", (-1) * sub_info->param_count + FindVarPos(arr, stmt->left->left, asm_info));

    sub_info->comment = "arr pos for array is";
    PrintExpr(file, stmt->left->right, arr, asm_info, sub_info);
    FPRINTF("ADD");
    FPRINTF("ADD");
    FPRINTF("POPR RCX");
    FPRINTF("POPM [RCX]");
}

static void CountValueWithParamCount(FILE *file, int shift, const char *add_sub, const char *reg, SubAsmInfo *sub_info) {
    assert(add_sub);
    assert(reg);

    FPRINTF("PUSHR RAX ;%s", sub_info->comment ? sub_info->comment : "");
    FPRINTF("PUSH %d", shift);
    FPRINTF("%s", add_sub);
    FPRINTF("POPR %s", reg);
}

static void PrintStatementOperationCase(FILE *file, LangNode_t *stmt, VariableArr *arr,
        AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!stmt) return;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (stmt->value.operation) {
        case kOperationHLT:
            FPRINTF("HLT\n");
            break;

        case kOperationCallAddr:
            PrintAddressOf(file, stmt->left, arr, asm_info, sub_info);
            break;

        case kOperationGetAddr:
            PrintDereference(file, stmt->left, arr, asm_info, sub_info);
            break;

        case kOperationCall:
            PushParamsToStack(file, stmt->right, arr, asm_info, sub_info);
            FPRINTF("CALL :%s\n", arr->var_array[stmt->left->value.pos].variable_name);
            break;

        case kOperationIs:
            if (IsThatOperation(stmt->left, kOperationArrPos)) {
                PrintIsForArray(file, stmt, arr, asm_info, sub_info);
                break;
            }
            PrintExpr(file, stmt->right, arr, asm_info, sub_info);
            if (IsThatOperation(stmt->left, kOperationGetAddr)) {
                PrintAddressAssignment(file, stmt, arr, asm_info, sub_info);
                break;
            }
            sub_info->comment = "is left part";
            PrintStatement(file, stmt->left, arr, asm_info, sub_info);
            break;

        case kOperationReturn:
            sub_info->comment = "return";
            PrintReturn(file, stmt, arr, asm_info, sub_info);
            break;

        CASE_UNARY_OP(stmt, Write, OUT, sub_info);
        CASE_UNARY_OP(stmt, WriteChar, OUTC, sub_info);

        case kOperationRead:
            FPRINTF("IN\n");
            FindVarPosPopMN(file, arr, stmt->left, asm_info, sub_info);
            break;

        case kOperationThen:
            sub_info->comment = "then left";
            PrintStatement(file, stmt->left, arr, asm_info, sub_info);
            sub_info->comment = "then right";
            PrintStatement(file, stmt->right, arr, asm_info, sub_info);
            break;

        case kOperationIf:
            PrintIfToAsm(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationWhile:
            PrintWhileToAsm(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationTernary:
            sub_info->comment = "ternary";
            PrintStatement(file, stmt->left->right, arr, asm_info, sub_info);
            PrintStatement(file, stmt->left->left, arr, asm_info, sub_info);
            break;

        case kOperationArrDecl:
            PrintArrDeclare(file, stmt, arr, asm_info, sub_info);
            break;

        case kOperationDraw:
            FPRINTF("DRAW");
            break;

        default:
            PrintExpr(file, stmt, arr, asm_info, sub_info);
            break;
    }
    #pragma GCC diagnostic pop
}

static void PrintExprOperationCase(FILE *file, LangNode_t *expr, VariableArr *arr, AsmInfo *asm_info, SubAsmInfo *sub_info) {
    assert(file);
    assert(expr);
    assert(arr);
    assert(asm_info);

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (expr->value.operation) {
        case kOperationCallAddr:
            PrintAddressOf(file, expr->left, arr, asm_info, sub_info);
            break;

        case kOperationGetAddr:
            PrintDereference(file, expr->left, arr, asm_info, sub_info);
            break;

        CASE_UNARY_OP(expr, SQRT, SQRT, sub_info);
        CASE_BINARY_OP(expr, Add, ADD, sub_info);
        CASE_BINARY_OP(expr, Sub, SUB, sub_info);
        CASE_BINARY_OP(expr, Mul, MUL, sub_info);
        CASE_BINARY_OP(expr, Div, DIV, sub_info);

        case kOperationCall:
            PushParamsToStack(file, expr->right, arr, asm_info, sub_info);
            FPRINTF("CALL :%s\n", arr->var_array[expr->left->value.pos].variable_name);
            break;

        case kOperationArrPos:
            FPRINTF("PUSHR RAX ;%s", "finding array pos");
            FPRINTF("PUSH %d", (-1) * sub_info->param_count + FindVarPos(arr, expr->left, asm_info));
            PrintExpr(file, expr->right, arr, asm_info, sub_info);
            FPRINTF("ADD");
            FPRINTF("POPR RCX");
            FPRINTF("PUSHM [RCX]\n");

        default:
            break;
    }
    #pragma GCC diagnostic pop
}