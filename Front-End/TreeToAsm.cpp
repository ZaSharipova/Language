#include "Front-End/TreeToAsm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/StackFunctions.h"
#include "Common/CommonFunctions.h"

#define FPRINTF(fmt, ...)                                       \
    do {                                                        \
        for (int k = 0; k < indent; k++) fprintf(file, "\t"); \
        fprintf(file, fmt "\n", ##__VA_ARGS__);                 \
    } while(0)

#define FPRINTF_LABEL(fmt, ...)                 \
    do {                                        \
        fprintf(file, fmt "\n", ##__VA_ARGS__); \
    } while(0)


#define CASE_BINARY_OP(node, op_name, asm_op)                                       \
    case kOperation##op_name:                                                       \
        PrintExpr(file, node->left, arr, ram_base, param_count, asm_info, indent);  \
        PrintExpr(file, node->right, arr, ram_base, param_count, asm_info, indent); \
        FPRINTF(#asm_op "\n");                                                      \
        break

#define CASE_UNARY_OP(node, op_name, asm_op)                                        \
    case kOperation##op_name:                                                       \
        PrintExpr(file, node->left, arr, ram_base, param_count, asm_info, indent);  \
        FPRINTF(#asm_op "\n");                                                      \
        break

static void CleanPositions(VariableArr *arr);
static const char *ChooseCompareMode(LangNode_t *node);

static void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base, AsmInfo *asm_info, int indent);
static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void FindVarPosPopMN(FILE *file, VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info, int indent);
static int  FindVarPos(VariableArr *arr, LangNode_t *node, AsmInfo *asm_info);
static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);

static void PrintIfToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void PrintWhileToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);
static void PrintReturn(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);

static void PrintArrDeclare(FILE *file, LangNode_t *stmt, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent);
static void PrintAddressOf(FILE *file, LangNode_t *var_node, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent);
static void PrintDereference(FILE *file, LangNode_t *ptr_node, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent);
static void PrintAddressAssignment(FILE *file, LangNode_t *deref_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent);

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

    FPRINTF("PUSHR RAX");
    FPRINTF("PUSH %d", param_count);
    FPRINTF("ADD");
    FPRINTF("POPR RAX\n");

    PushParamsToRam(file, args, arr, *ram_base, param_count, asm_info, indent);

    *ram_base += param_count;
    PrintStatement(file, func_node->right->right, arr, *ram_base, param_count, asm_info, indent);

    FPRINTF("PUSHR RAX");
    FPRINTF("PUSH %d", param_count);
    FPRINTF("SUB");
    FPRINTF("POPR RAX\n");
    *ram_base -= param_count;

    if (strcmp(MAIN, arr->var_array[func_node->left->value.pos].variable_name) != 0) {
        FPRINTF( "RET\n");
    } else {
        FPRINTF("HLT\n");
    }
}

static void FindVarPosPopMN(FILE *file, VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info, int indent) {
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

        if (arr->var_array[check_node->value.pos].variable_name 
                && strcmp(arr->var_array[i].variable_name, arr->var_array[check_node->value.pos].variable_name) == 0) {

            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = asm_info->counter++;
                FPRINTF("PUSHR RAX");
                FPRINTF("PUSH %d", (-1) * param_count + arr->var_array[check_node->value.pos].pos_in_code);
                FPRINTF("ADD");
                FPRINTF("POPR RCX");
                FPRINTF("POPM [RCX]\n");
            } else {
                var_idx = arr->var_array[i].pos_in_code;
                FPRINTF("PUSHR RAX");
                FPRINTF("PUSH %d", (-1) * param_count + var_idx);
                FPRINTF( "ADD");
                FPRINTF("POPR RCX");
                FPRINTF("POPM [RCX]\n");
            }
            break;
        }
    }

    if (var_idx == -1) {
        fprintf(stderr, "Unknown variable\n");
        return; //
    }
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

static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        PrintExpr(file, args_node, arr, ram_base, param_count, asm_info, indent);
        return;
    }

    if (args_node->left) {
        PushParamsToStack(file, args_node->right, arr, ram_base, param_count, asm_info, indent);
    }
    if (args_node->right) {
        PushParamsToStack(file, args_node->left, arr, ram_base, param_count, asm_info, indent);
    }
}

void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!args_node) return;

    if (!IsThatOperation(args_node, kOperationComma)) {
        FindVarPosPopMN(file, arr, args_node, param_count, asm_info, indent);
        return;
    }

    if (args_node->left) {
        PushParamsToRam(file, args_node->left, arr, ram_base, param_count, asm_info, indent);
    }
    if (args_node->right) {
        PushParamsToRam(file, args_node->right, arr, ram_base, param_count, asm_info, indent);
    }
}

static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wswitch-enum"
            switch (stmt->value.operation) {
                case kOperationHLT:
                    FPRINTF("HLT\n");
                    break;

                case kOperationCallAddr:
                    PrintAddressOf(file, stmt->left, arr, param_count, asm_info, indent);
                    break;
                    
                case kOperationGetAddr:
                    PrintDereference(file, stmt->left, arr, param_count, asm_info, indent);
                    break;

                case kOperationCall:
                    PushParamsToStack(file, stmt->right, arr, ram_base, param_count, asm_info, indent);
                    FPRINTF("CALL :%s\n", arr->var_array[stmt->left->value.pos].variable_name);
                    break;

                case kOperationIs:
                    if (IsThatOperation(stmt->left, kOperationArrPos)) {

                        PrintExpr(file, stmt->right, arr, ram_base, param_count, asm_info, indent);

                        FPRINTF("PUSHR RAX");
                        FPRINTF("PUSH %d", (-1) * param_count + arr->var_array[stmt->left->left->value.pos].pos_in_code);
                        PrintExpr(file, stmt->left->right, arr, ram_base, param_count, asm_info, indent);
                        FPRINTF("ADD");
                        FPRINTF("ADD");
                        FPRINTF("POPR RCX");


                        FPRINTF("POPM [RCX]");
                        break;
                    }
                    PrintExpr(file, stmt->right, arr, ram_base, param_count, asm_info, indent);
                    if (IsThatOperation(stmt->left, kOperationGetAddr)) {
                        PrintAddressAssignment(file, stmt, arr, ram_base, param_count, asm_info, indent);
                        break;
                    }
                    PrintStatement(file, stmt->left, arr, ram_base, param_count, asm_info, indent);
                    break;

                case kOperationReturn:
                    PrintReturn(file, stmt, arr, ram_base, param_count, asm_info, indent);
                    break;

                CASE_UNARY_OP(stmt, Write, OUT);
                CASE_UNARY_OP(stmt, WriteChar, OUTC);

                case kOperationRead:
                    FPRINTF("IN\n");
                    FindVarPosPopMN(file, arr, stmt->left, param_count, asm_info, indent);
                    break;

                case kOperationThen:
                    PrintStatement(file, stmt->left, arr, ram_base, param_count, asm_info, indent);
                    PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info, indent);
                    break;

                case kOperationIf: 
                    PrintIfToAsm(file, stmt, arr, ram_base, param_count, asm_info, indent);
                    break;

                case kOperationWhile:
                    PrintWhileToAsm(file, stmt, arr, ram_base, param_count, asm_info, indent);
                    break;

                case kOperationTernary: {
                    PrintStatement(file, stmt->left->right, arr, ram_base, param_count, asm_info, indent);
                    PrintStatement(file, stmt->left->left, arr, ram_base, param_count, asm_info, indent);
                }  break;

                case kOperationArrDecl: { // TODO: check 
                    PrintArrDeclare(file, stmt, arr, param_count, asm_info, indent);
                    break;
                }

                case kOperationDraw:
                    FPRINTF("DRAW");
                    break;

                default:
                    PrintExpr(file, stmt, arr, ram_base, param_count, asm_info, indent);
                    break;
            }
            #pragma clang diagnostic pop
            break;

        case kVariable:
            FindVarPosPopMN(file, arr, stmt, param_count, asm_info, indent);
            break;

        case kNumber:
            FPRINTF("PUSH %.0f", stmt->value.number);
            break;

        default:
            fprintf(stderr, "No such switch case.\n");
            break;
    }
}

static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!expr) return;

    switch (expr->type) {
        case kNumber:
        FPRINTF("PUSH %.0f", expr->value.number);
        break;
        case kVariable: {
            int var_idx = FindVarPos(arr, expr, asm_info);
            FPRINTF("PUSHR RAX");
            FPRINTF("PUSH %d", (-1) * param_count + var_idx);
            FPRINTF("ADD");
            FPRINTF("POPR RCX");
            FPRINTF("PUSHM [RCX]\n");
        } break;
        case kOperation:
            #pragma clang diagnostic push
            #pragma clang diagnostic ignored "-Wswitch-enum"
            switch (expr->value.operation) {
                case kOperationCallAddr:
                    PrintAddressOf(file, expr->left, arr, param_count, asm_info, indent);
                    break;

                case kOperationGetAddr:
                    PrintDereference(file, expr->left, arr, param_count, asm_info, indent);
                    break;

                CASE_UNARY_OP(expr, SQRT, SQRT);
                CASE_BINARY_OP(expr, Add, ADD);
                CASE_BINARY_OP(expr, Sub, SUB);
                CASE_BINARY_OP(expr, Mul, MUL);
                CASE_BINARY_OP(expr, Div, DIV);

                case kOperationCall:
                    PushParamsToStack(file, expr->right, arr, ram_base, param_count, asm_info, indent);
                    FPRINTF("CALL :%s\n", arr->var_array[expr->left->value.pos].variable_name);
                    break;

                case kOperationArrPos:
                    FPRINTF("PUSHR RAX");
                    FPRINTF("PUSH %d", (-1) * param_count + arr->var_array[expr->left->value.pos].pos_in_code);
                    //FPRINTF("PUSH %d", (-1) * param_count + FindVarPos(arr, expr->right, asm_info));
                    PrintExpr(file, expr->right, arr, ram_base, param_count, asm_info, indent);
                    FPRINTF("ADD");
                    FPRINTF("POPR RCX");
                    FPRINTF("PUSHM [RCX]\n"); 

                default:
                    break;
            }
            break;
            #pragma clang diagnostic pop
    }
}

static void CleanPositions(VariableArr *arr) {
    assert(arr);

    for (size_t i = 0; i < arr->size; i++) {
        arr->var_array[i].pos_in_code = -1;
    }
}

static const char *ChooseCompareMode(LangNode_t *node) {
    if (!node || node->type != kOperation) {
        return "NULL";
    }

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    switch (node->value.operation) {
        case kOperationA:  return "JBE";
        case kOperationAE: return "JB";
        case kOperationB:  return "JAE";
        case kOperationBE: return "JA";
        case kOperationE:  return "JNE";
        case kOperationNE: return "JE";
        default: return "NULL";
    }
    #pragma clang diagnostic pop
}

static void PrintIfToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    LangNode_t *condition = stmt->left;

    PrintExpr(file, condition->left, arr, ram_base, param_count, asm_info, indent);
    PrintExpr(file, condition->right, arr, ram_base, param_count, asm_info, indent);

    int this_if = asm_info->label_if++;
    int this_else = asm_info->label_else++;

    FPRINTF("%s :else_%d", ChooseCompareMode(condition), this_else);

    if (IsThatOperation(stmt->right, kOperationElse)) {
        PrintStatement(file, stmt->right->left, arr, ram_base, param_count, asm_info, indent + 1);
        FPRINTF( "JMP :end_if_%d", this_if);
    } else {
        PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info, indent + 1);
        FPRINTF("JMP :end_if_%d", this_if);
    }

    FPRINTF_LABEL("\n:else_%d", this_else);
    if (IsThatOperation(stmt->right, kOperationElse)) {
        PrintStatement(file, stmt->right->right, arr, ram_base, param_count, asm_info, indent);
    }

    FPRINTF_LABEL(":end_if_%d", this_if);
}

static void PrintWhileToAsm(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    int start_label = asm_info->label_counter++;
    int end_label = asm_info->label_counter++;

    FPRINTF_LABEL("\n:while_start_%d", start_label);

    PrintExpr(file, stmt->left->left, arr, ram_base, param_count, asm_info, indent);
    PrintExpr(file, stmt->left->right, arr, ram_base, param_count, asm_info, indent);

    FPRINTF("%s :while_end_%d", ChooseCompareMode(stmt->left), end_label);

    PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info, indent + 1);

    FPRINTF("JMP :while_start_%d", start_label);
    FPRINTF_LABEL("\n:while_end_%d", end_label);
}

static void PrintReturn(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    PrintExpr(file, stmt->left, arr, ram_base, param_count, asm_info, indent);
    FPRINTF("PUSHR RAX");
    FPRINTF("PUSH %d", param_count);
    FPRINTF("SUB");
    FPRINTF("POPR RAX");
    FPRINTF("RET\n");
}

static void PrintArrDeclare(FILE *file, LangNode_t *stmt, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(asm_info);

    arr->var_array[stmt->left->left->left->value.pos].pos_in_code = asm_info->counter;

    for (size_t i = 0; i < stmt->left->left->right->value.number; i++) {
        FPRINTF("PUSH 0");
        FPRINTF("PUSHR RAX");
        FPRINTF("PUSH %d", (-1) * param_count + asm_info->counter + (int)i);

        FPRINTF("ADD");
        FPRINTF("POPR RCX");
        FPRINTF("POPM [RCX]\n");
    }

    asm_info->counter += (int)stmt->left->left->right->value.number;
}

static void PrintAddressOf(FILE *file, LangNode_t *var_node, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(var_node);
    assert(arr);
    assert(asm_info);

    assert(var_node->type == kVariable);
    
    int var_idx = FindVarPos(arr, var_node, asm_info);
    FPRINTF("PUSHR RAX");
    FPRINTF("PUSH %d", (-1) * param_count + var_idx);
    FPRINTF("ADD");
}

static void PrintDereference(FILE *file, LangNode_t *ptr_node, VariableArr *arr, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(ptr_node);
    assert(arr);
    assert(asm_info);

    PrintAddressOf(file, ptr_node, arr, param_count, asm_info, indent);

    FPRINTF("POPR RCX");
    FPRINTF("PUSHM [RCX]");
}

static void PrintAddressAssignment(FILE *file, LangNode_t *deref_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info, int indent) {
    assert(file);
    assert(deref_node);
    assert(arr);
    assert(asm_info);

    PrintExpr(file, deref_node->left, arr, ram_base, param_count, asm_info, indent);
    
    FPRINTF("POPR RCX");
    FPRINTF("POPM [RCX]\n");
}
