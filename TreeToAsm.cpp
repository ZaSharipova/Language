#include "TreeToAsm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "Structs.h"
#include "Enums.h"

static int label_counter = 0;

#define PRINT(file, fmt, ...) fprintf(file, fmt "\n", ##__VA_ARGS__)

int NewLabel() {
    return label_counter++;
}

void PrintExpr(FILE *file, DifNode_t *expr, VariableArr *arr, int ram_base);

void PrintStatement(FILE *file, DifNode_t *stmt, VariableArr *arr, int ram_base) {
    assert(file);
    assert(arr);

    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            switch (stmt->value.operation) {
                case kOperationIs:
                    PrintExpr(file, stmt->right, arr, ram_base); {

                        int var_idx = -1;
                        for (size_t i = 0; i < arr->size; i++) {
                            if (strcmp(arr->var_array[i].variable_name, arr->var_array[stmt->left->value.pos].variable_name) == 0) {
                                var_idx = (int)i;
                                break;
                            }
                        }
                        if (var_idx == -1) { fprintf(stderr, "Unknown variable\n"); exit(1); }
                        PRINT(file, "POPMN [%d]", ram_base + var_idx);
                    }
                    break;

                case kOperationWrite:
                    PrintExpr(file, stmt->left, arr, ram_base);
                    PRINT(file, "OUT");
                    break;

                case kOperationThen:
                    PrintStatement(file, stmt->left, arr, ram_base);
                    PrintStatement(file, stmt->right, arr, ram_base);
                    break;

                case kOperationIf: {
                        int lbl_false = NewLabel();
                        PrintExpr(file, stmt->left, arr, ram_base);
                        PRINT(file, "PUSH 0");
                        PRINT(file, "SUB");
                        PRINT(file, "JE :F_%d", lbl_false);

                        PrintStatement(file, stmt->right, arr, ram_base);

                        PRINT(file, ":F_%d", lbl_false);
                    }
                    break;

                case kOperationAdd:
                case kOperationSub:
                case kOperationMul:
                case kOperationDiv:
                    PrintExpr(file, stmt, arr, ram_base);
                    break;

                default:
                    break;
            }
            break;

        case kVariable:
        case kNumber:
            break;
    }
}

void PrintExpr(FILE *file, DifNode_t *expr, VariableArr *arr, int ram_base) {
    assert(file);
    assert(arr);

    if (!expr) return;

    switch (expr->type) {
        case kNumber:
            PRINT(file, "PUSH %d", expr->value.number);
            break;
        case kVariable: {
                int var_idx = expr->value.pos;
                PRINT(file, "PUSHMN [%d]", ram_base + var_idx);
            }
            break;
        case kOperation:
            switch (expr->value.operation) {
                case kOperationAdd:
                    PrintExpr(file, expr->left, arr, ram_base);
                    PrintExpr(file, expr->right, arr, ram_base);
                    PRINT(file, "ADD");
                    break;
                case kOperationSub:
                    PrintExpr(file, expr->left, arr, ram_base);
                    PrintExpr(file, expr->right, arr, ram_base);
                    PRINT(file, "SUB");
                    break;
                case kOperationMul:
                    PrintExpr(file, expr->left, arr, ram_base);
                    PrintExpr(file, expr->right, arr, ram_base);
                    PRINT(file, "MUL");
                    break;
                case kOperationDiv:
                    PrintExpr(file, expr->left, arr, ram_base);
                    PrintExpr(file, expr->right, arr, ram_base);
                    PRINT(file, "DIV");
                    break;
                default:
                    break;
            }
            break;
    }
}

void PrintFunction(FILE *file, DifNode_t *func_node, VariableArr *arr) {
    assert(file);
    assert(arr);
    if (!func_node) return;

    int ram_base = 100; //
    int param_count = 0;

    PRINT(file, ":%s", arr->var_array[func_node->left->value.pos].variable_name);

    DifNode_t *args = func_node->right->left;
    for (DifNode_t *arg = args; arg != NULL; arg = arg->right) {
        if (arg && arg->type == kVariable) {
            PRINT(file, "PUSH %d", arr->var_array[arg->value.pos].variable_value);
            PRINT(file, "POPMN [%d]", ram_base + param_count);
        }
        param_count++;
    }
    for (DifNode_t *arg = args; arg != NULL; arg = arg->left) {
        if (arg && arg->type == kVariable) {
            PRINT(file, "PUSH %d", arr->var_array[arg->value.pos].variable_value);
            PRINT(file, "POPMN [%d]", ram_base + param_count);
        }
        param_count++;
    }

    DifNode_t *body = func_node->right->right;
    PrintStatement(file, body, arr, ram_base);

    PRINT(file, "RET");
}

void PrintProgram(FILE *file, DifNode_t *root, VariableArr *arr) {
    assert(file);
    assert(root);
    assert(arr);
    
    if (root->type == kOperation && root->value.operation == kOperationFunction) {
        PrintFunction(file, root, arr);
    }
    
    if (root->left) {
        PrintProgram(file, root->left, arr);
    }
    
    if (root->right) {
        PrintProgram(file, root->right, arr);
    }
}
