#include "Front-End/TreeToAsm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "Structs.h"
#include "Enums.h"
#include "StackFunctions.h"

static int label_counter = 0;
int counter = 0;

int label_if = 0;
int label_else = 0;

#define PRINT(file, fmt, ...) fprintf(file, fmt "\n", ##__VA_ARGS__)
static void CleanPositions(VariableArr *arr);

int NewLabel() {
    return label_counter++;
}

static const char *ChooseCompareMode(LangNode_t *node);
void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count);

static void FindVarPosPopMN(FILE *file, VariableArr *arr, LangNode_t *node, int ram_base, int param_count) {
    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (strncmp(arr->var_array[i].variable_name, arr->var_array[node->value.pos].variable_name, strlen(arr->var_array[i].variable_name)) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = counter++;
                //printf("AAAAA%d", arr->var_array[i].pos_in_code);
                PRINT(file, "PUSHR RAX");
                PRINT(file, "PUSH %d", (-1) * param_count + arr->var_array[node->value.pos].pos_in_code);
                PRINT(file, "ADD");
                PRINT(file, "POPR RCX");
                PRINT(file, "POPM [RCX]\n");
            } else {
                var_idx = arr->var_array[i].pos_in_code;
                PRINT(file, "PUSHR RAX");
                PRINT(file, "PUSH %d", (-1) * param_count + var_idx);
                PRINT(file, "ADD");
                PRINT(file, "POPR RCX");
                PRINT(file, "POPM [RCX]\n");
            }
            break;
        }
    }
    if (var_idx == -1) {
        fprintf(stderr, "Unknown variable\n");
        exit(1);
    }
}

int FindVarPos(VariableArr *arr, LangNode_t *node, int ram_base, int param_count) {
    assert(arr);
    assert(node);

    //printf("%d", counter);
    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (strncmp(arr->var_array[i].variable_name, arr->var_array[node->value.pos].variable_name,
                    strlen(arr->var_array[i].variable_name)) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = counter++;
            } else {
                var_idx = arr->var_array[i].pos_in_code;
            }
        }
    }

    return var_idx;
}

void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count) {
    if (!args_node) return;

    if (!(args_node->type == kOperation && args_node->value.operation == kOperationComma)) {
        PrintExpr(file, args_node, arr, ram_base, param_count);
        return;
    }

    if (args_node->left) PushParamsToStack(file, args_node->right, arr, ram_base, param_count);
    if (args_node->right) PushParamsToStack(file, args_node->left, arr, ram_base, param_count);
    
}

void PushParamsToRam(FILE *file, LangNode_t *args_node, VariableArr *arr, int ram_base, int param_count) {
    assert(file);
    assert(arr);
    if (!args_node) return;

    if (!(args_node->type == kOperation && args_node->value.operation == kOperationComma)) {
        FindVarPosPopMN(file, arr, args_node, ram_base, param_count);
        return;
    }

    if (args_node->left) PushParamsToRam(file, args_node->left, arr, ram_base, param_count);
    if (args_node->right) PushParamsToRam(file, args_node->right, arr, ram_base, param_count);
}

void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, int ram_base, int param_count) {
    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            switch (stmt->value.operation) {
                case kOperationHLT:
                    PRINT(file, "HLT\n");
                    break;

                case kOperationCall:
                    PushParamsToStack(file, stmt->right, arr, ram_base, param_count);
                    PRINT(file, "CALL :%s\n", arr->var_array[stmt->left->value.pos].variable_name);
                    
                    break;

                case kOperationIs:
                    PrintExpr(file, stmt->right, arr, ram_base, param_count);
                    PrintStatement(file, stmt->left, arr, ram_base, param_count);
                    break;

                case kOperationReturn:
                    PrintExpr(file, stmt->left, arr, ram_base, param_count);
                    PRINT(file, "PUSHR RAX");
                    PRINT(file, "PUSH %d", param_count);
                    PRINT(file, "SUB");
                    PRINT(file, "POPR RAX");
                    PRINT(file, "RET\n");
                    break;

                case kOperationWrite:
                    PrintExpr(file, stmt->left, arr, ram_base, param_count);
                    PRINT(file, "OUT\n");
                    break;

                case kOperationRead:
                    PRINT(file, "IN\n");
                    FindVarPosPopMN(file, arr, stmt->left, ram_base, param_count);
                    break;

                case kOperationThen:
                    PrintStatement(file, stmt->left, arr, ram_base, param_count);
                    PrintStatement(file, stmt->right, arr, ram_base, param_count);
                    break;

                case kOperationIf: {
                    LangNode_t *condition = stmt->left;

                    PrintExpr(file, condition->left, arr, ram_base, param_count);
                    PrintExpr(file, condition->right, arr, ram_base, param_count);

                    int this_if = label_if++;
                    int this_else = label_else++;

                    PRINT(file, "%s :else_%d", ChooseCompareMode(condition), this_else);

                    if (stmt->right && stmt->right->type == kOperation &&
                        stmt->right->value.operation == kOperationElse) {
                        PrintStatement(file, stmt->right->left, arr, ram_base, param_count);
                        PRINT(file, "JMP :end_if_%d", this_if);
                    } else {
                        PrintStatement(file, stmt->right, arr, ram_base, param_count);
                        PRINT(file, "JMP :end_if_%d", this_if);
                    }

                    PRINT(file, ":else_%d", this_else);
                    if (stmt->right && stmt->right->type == kOperation &&
                        stmt->right->value.operation == kOperationElse) {
                        PrintStatement(file, stmt->right->right, arr, ram_base, param_count);
                    }

                    PRINT(file, ":end_if_%d", this_if);
                } break;

                case kOperationWhile: {
                    int start_label = NewLabel();
                    int end_label = NewLabel();

                    PRINT(file, ":while_start_%d", start_label);

                    PrintExpr(file, stmt->left->left, arr, ram_base, param_count);
                    PrintExpr(file, stmt->left->right, arr, ram_base, param_count);

                    PRINT(file, "%s :while_end_%d", ChooseCompareMode(stmt->left), end_label);

                    PrintStatement(file, stmt->right, arr, ram_base, param_count);

                    PRINT(file, "JMP :while_start_%d", start_label);
                    PRINT(file, ":while_end_%d", end_label);
                } break;

                default:
                    PrintExpr(file, stmt, arr, ram_base, param_count);
                    break;
            }
            break;

        case kVariable:
            FindVarPosPopMN(file, arr, stmt, ram_base, param_count);
            break;

        case kNumber:
            PRINT(file, "PUSH %.0f", stmt->value.number);
            break;
    }
}

void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int ram_base, int param_count) {
    if (!expr) return;

    switch (expr->type) {
        case kNumber:
        PRINT(file, "PUSH %.0f", expr->value.number);
        break;
        case kVariable: {
            int var_idx = FindVarPos(arr, expr, ram_base, param_count);
            PRINT(file, "PUSHR RAX");
            PRINT(file, "PUSH %d", (-1) * param_count + var_idx);
            PRINT(file, "ADD");
            PRINT(file, "POPR RCX");
            PRINT(file, "PUSHM [RCX]\n");
        } break;
        case kOperation:
            switch (expr->value.operation) {
                case kOperationSQRT:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PRINT(file, "SQRT\n");
                    break;
                case kOperationAdd:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PrintExpr(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "ADD\n");
                    break;
                case kOperationSub:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PrintExpr(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "SUB\n");
                    break;
                case kOperationMul:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PrintExpr(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "MUL\n");
                    break;
                case kOperationDiv:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PrintExpr(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "DIV\n");
                    break;
                case kOperationCall:
                    PushParamsToStack(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "CALL :%s\n", arr->var_array[expr->left->value.pos].variable_name);
                    break;
            }
            break;
    }
}


void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base) {
    if (!func_node) return;

    int param_count = 0;
    CleanPositions(arr);
    counter = 0;

    LangNode_t *args = func_node->right->left;

    if (strcmp(MAIN, arr->var_array[func_node->left->value.pos].variable_name) != 0)
        PRINT(file, "\n:%s", arr->var_array[func_node->left->value.pos].variable_name);
    param_count = arr->var_array[func_node->left->value.pos].variable_value;

    PRINT(file, "PUSHR RAX");
    PRINT(file, "PUSH %d", param_count);
    PRINT(file, "ADD");
    PRINT(file, "POPR RAX\n");

    PushParamsToRam(file, args, arr, *ram_base, param_count);

    (*ram_base) += param_count;
    PrintStatement(file, func_node->right->right, arr, *ram_base, param_count);

    PRINT(file, "PUSHR RAX");
    PRINT(file, "PUSH %d", param_count);
    PRINT(file, "SUB");
    PRINT(file, "POPR RAX\n");
    (*ram_base) -= param_count;

    // printf("\n\n");
    // for (size_t i = 0; i < arr->size; i++) {
    //     printf("%s %d %d\n", arr->var_array[i].variable_name, arr->var_array[i].variable_value, arr->var_array[i].pos_in_code);
    // }
    if (strcmp(MAIN, arr->var_array[func_node->left->value.pos].variable_name) == 0)
        PRINT(file, "HLT\n");
    else
        PRINT(file, "RET\n");
}

static void CleanPositions(VariableArr *arr) {
    for (size_t i = 0; i < arr->size; i++)
        arr->var_array[i].pos_in_code = -1;
}

void PrintProgram(FILE *file, LangNode_t *root, VariableArr *arr, int *ram_base) {
    if (!root) return;

    counter = 0;

    if (root->type == kOperation && root->value.operation == kOperationFunction)
        PrintFunction(file, root, arr, ram_base);

    if (root->left)
        PrintProgram(file, root->left, arr, ram_base);
    if (root->right)
        PrintProgram(file, root->right, arr, ram_base);
}

static const char *ChooseCompareMode(LangNode_t *node) {
    if (!node || node->type != kOperation) return "NULL";

    switch (node->value.operation) {
        case kOperationA:  return "JBE";
        case kOperationAE: return "JB";
        case kOperationB:  return "JAE";
        case kOperationBE: return "JA";
        case kOperationE:  return "JNE";
        case kOperationNE: return "JE";
        default: return "NULL";
    }
}
