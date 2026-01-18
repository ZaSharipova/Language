#include "Front-End/TreeToAsm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/StackFunctions.h"

#define FPRINTF(fmt, ...) fprintf(file, fmt "\n", ##__VA_ARGS__)
static void CleanPositions(VariableArr *arr);
static const char *ChooseCompareMode(LangNode_t *node);

static void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base, AsmInfo *asm_info);
static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info);
static void FindVarPosPopMN(FILE *file, VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info);
static int  FindVarPos(VariableArr *arr, LangNode_t *node, AsmInfo *asm_info);
static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info);
static void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info);
static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info);

void PrintProgram(FILE *file, LangNode_t *root, VariableArr *arr, int *ram_base, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(asm_info);
    if (!root) return;

    asm_info->counter = 0;

    if (root->type == kOperation && root->value.operation == kOperationFunction) {
        PrintFunction(file, root, arr, ram_base, asm_info);
    }

    if (root->left) PrintProgram(file, root->left, arr, ram_base, asm_info);
    if (root->right) PrintProgram(file, root->right, arr, ram_base, asm_info);
}

static void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(asm_info);
    if (!func_node) return;

    int param_count = 0;
    CleanPositions(arr);
    asm_info->counter = 0;

    LangNode_t *args = func_node->right->left;

    if (strcmp(MAIN, arr->var_array[func_node->left->value.pos].variable_name) != 0)
        FPRINTF("\n:%s", arr->var_array[func_node->left->value.pos].variable_name);
    param_count = arr->var_array[func_node->left->value.pos].variable_value;

    FPRINTF("PUSHR RAX");
    FPRINTF("PUSH %d", param_count);
    FPRINTF("ADD");
    FPRINTF("POPR RAX\n");

    PushParamsToRam(file, args, arr, *ram_base, param_count, asm_info);

    (*ram_base) += param_count;
    PrintStatement(file, func_node->right->right, arr, *ram_base, param_count, asm_info);

    FPRINTF("PUSHR RAX");
    FPRINTF("PUSH %d", param_count);
    FPRINTF("SUB");
    FPRINTF("POPR RAX\n");
    (*ram_base) -= param_count;

    // FPRINTFf("\n\n");
    // for (size_t i = 0; i < arr->size; i++) {
    //     FPRINTFf("%s %d %d\n", arr->var_array[i].variable_name, arr->var_array[i].variable_value, arr->var_array[i].pos_in_code);
    // }
    if (strcmp(MAIN, arr->var_array[func_node->left->value.pos].variable_name) == 0) {
        FPRINTF( "HLT\n");
    } else {
        FPRINTF("RET\n");
    }
}

static void FindVarPosPopMN(FILE *file, VariableArr *arr, LangNode_t *node, int param_count, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(node);
    assert(asm_info);

    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (arr->var_array[node->value.pos].variable_name && strcmp(arr->var_array[i].variable_name, arr->var_array[node->value.pos].variable_name) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = asm_info->counter++;
                //FPRINTFf("AAAAA%d", arr->var_array[i].pos_in_code);
                FPRINTF("PUSHR RAX");
                FPRINTF("PUSH %d", (-1) * param_count + arr->var_array[node->value.pos].pos_in_code);
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
        exit(1);
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

static void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!args_node) return;

    if (!(args_node->type == kOperation && args_node->value.operation == kOperationComma)) {
        PrintExpr(file, args_node, arr, ram_base, param_count, asm_info);
        return;
    }

    if (args_node->left) PushParamsToStack(file, args_node->right, arr, ram_base, param_count, asm_info);
    if (args_node->right) PushParamsToStack(file, args_node->left, arr, ram_base, param_count, asm_info);
    
}

void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info) {
    assert(file);
    assert(arr);
    assert(asm_info);
    if (!args_node) return;

    if (!(args_node->type == kOperation && args_node->value.operation == kOperationComma)) {
        FindVarPosPopMN(file, arr, args_node, param_count, asm_info);
        return;
    }

    if (args_node->left) PushParamsToRam(file, args_node->left, arr, ram_base, param_count, asm_info);
    if (args_node->right) PushParamsToRam(file, args_node->right, arr, ram_base, param_count, asm_info);
}

static void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info) {
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

                case kOperationCall:
                    PushParamsToStack(file, stmt->right, arr, ram_base, param_count, asm_info);
                    FPRINTF("CALL :%s\n", arr->var_array[stmt->left->value.pos].variable_name);
                    
                    break;

                case kOperationIs:
                    PrintExpr(file, stmt->right, arr, ram_base, param_count, asm_info);
                    PrintStatement(file, stmt->left, arr, ram_base, param_count, asm_info);
                    break;

                case kOperationReturn:
                    PrintExpr(file, stmt->left, arr, ram_base, param_count, asm_info);
                    FPRINTF("PUSHR RAX");
                    FPRINTF("PUSH %d", param_count);
                    FPRINTF("SUB");
                    FPRINTF("POPR RAX");
                    FPRINTF("RET\n");
                    break;

                case kOperationWrite:
                    PrintExpr(file, stmt->left, arr, ram_base, param_count, asm_info);
                    FPRINTF("OUT\n");
                    break;

                case kOperationWriteChar:
                    PrintExpr(file, stmt->left, arr, ram_base, param_count, asm_info);
                    FPRINTF("OUTC\n");
                    break;

                case kOperationRead:
                    FPRINTF( "IN\n");
                    FindVarPosPopMN(file, arr, stmt->left, param_count, asm_info);
                    break;

                case kOperationThen:
                    PrintStatement(file, stmt->left, arr, ram_base, param_count, asm_info);
                    PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info);
                    break;

                case kOperationIf: {
                    LangNode_t *condition = stmt->left;

                    PrintExpr(file, condition->left, arr, ram_base, param_count, asm_info);
                    PrintExpr(file, condition->right, arr, ram_base, param_count, asm_info);

                    int this_if = asm_info->label_if++;
                    int this_else = asm_info->label_else++;

                    FPRINTF("%s :else_%d", ChooseCompareMode(condition), this_else);

                    if (stmt->right && stmt->right->type == kOperation &&
                        stmt->right->value.operation == kOperationElse) {
                        PrintStatement(file, stmt->right->left, arr, ram_base, param_count, asm_info);
                        FPRINTF( "JMP :end_if_%d", this_if);
                    } else {
                        PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info);
                        FPRINTF("JMP :end_if_%d", this_if);
                    }

                    FPRINTF(":else_%d", this_else);
                    if (stmt->right && stmt->right->type == kOperation &&
                        stmt->right->value.operation == kOperationElse) {
                        PrintStatement(file, stmt->right->right, arr, ram_base, param_count, asm_info);
                    }

                    FPRINTF(":end_if_%d", this_if);
                } break;

                case kOperationWhile: {
                    int start_label = asm_info->label_counter++;
                    int end_label = asm_info->label_counter++;

                    FPRINTF(":while_start_%d", start_label);

                    PrintExpr(file, stmt->left->left, arr, ram_base, param_count, asm_info);
                    PrintExpr(file, stmt->left->right, arr, ram_base, param_count, asm_info);

                    FPRINTF("%s :while_end_%d", ChooseCompareMode(stmt->left), end_label);

                    PrintStatement(file, stmt->right, arr, ram_base, param_count, asm_info);

                    FPRINTF("JMP :while_start_%d", start_label);
                    FPRINTF(":while_end_%d", end_label);
                } break;

                case (kOperationTernary): {
                    PrintStatement(file, stmt->left->right, arr, ram_base, param_count, asm_info);
                    PrintStatement(file, stmt->left->left, arr, ram_base, param_count, asm_info);
                    
                }  break;


                default:
                    PrintExpr(file, stmt, arr, ram_base, param_count, asm_info);
                    break;
            }
            #pragma clang diagnostic pop
            break;

        case kVariable:
            FindVarPosPopMN(file, arr, stmt, param_count, asm_info);
            break;

        case kNumber:
            FPRINTF("PUSH %.0f", stmt->value.number);
            break;

        default:
            fprintf(stderr, "No such switch case.\n");
            break;
    }
}

static void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count, AsmInfo *asm_info) {
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
                case kOperationSQRT:
                    PrintExpr(file, expr->left, arr, ram_base, param_count, asm_info);
                    FPRINTF( "SQRT\n");
                    break;
                case kOperationAdd:
                    PrintExpr(file, expr->left, arr, ram_base, param_count, asm_info);
                    PrintExpr(file, expr->right, arr, ram_base, param_count, asm_info);
                    FPRINTF("ADD\n");
                    break;
                case kOperationSub:
                    PrintExpr(file, expr->left, arr, ram_base, param_count, asm_info);
                    PrintExpr(file, expr->right, arr, ram_base, param_count, asm_info);
                    FPRINTF("SUB\n");
                    break;
                case kOperationMul:
                    PrintExpr(file, expr->left, arr, ram_base, param_count, asm_info);
                    PrintExpr(file, expr->right, arr, ram_base, param_count, asm_info);
                    FPRINTF("MUL\n");
                    break;
                case kOperationDiv:
                    PrintExpr(file, expr->left, arr, ram_base, param_count, asm_info);
                    PrintExpr(file, expr->right, arr, ram_base, param_count, asm_info);
                    FPRINTF("DIV\n");
                    break;
                case kOperationCall:
                    PushParamsToStack(file, expr->right, arr, ram_base, param_count, asm_info);
                    FPRINTF("CALL :%s\n", arr->var_array[expr->left->value.pos].variable_name);
                    break;

                default:
                    break;
            }
            break;
            #pragma clang diagnostic pop
    }
}

static void CleanPositions(VariableArr *arr) {
    assert(arr);

    for (size_t i = 0; i < arr->size; i++)
        arr->var_array[i].pos_in_code = -1;
}

static const char *ChooseCompareMode(LangNode_t *node) {
    if (!node || node->type != kOperation) return "NULL";

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