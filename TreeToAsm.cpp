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

static const char *ChooseCompareMode(DifNode_t *node);
void PrintExpr(FILE *file, DifNode_t *expr, VariableArr *arr, int ram_base);

static void FindVarPos(FILE *file, VariableArr *arr, DifNode_t *node, int ram_base, int *param_count) {
    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (strncmp(arr->var_array[i].variable_name, arr->var_array[node->value.pos].variable_name, strlen(arr->var_array[i].variable_name)) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = ram_base + *param_count;
                (*param_count)++;
                //PRINT(file, "PUSH %d", arr->var_array[node->value.pos].variable_value);
                //printf("%d %d\n", node->value.pos, arr->var_array[node->value.pos].variable_value);
                PRINT(file, "POPMN [%d]", arr->var_array[node->value.pos].pos_in_code);
                PRINT(file, "PUSHR RAX");
                PRINT(file, "PUSH 1");
                PRINT(file, "ADD");
                PRINT(file, "POPR RAX");
            } else {
                var_idx = arr->var_array[i].pos_in_code;
                printf("aaa\n");
                PRINT(file, "POPMN [%d]", arr->var_array[node->value.pos].pos_in_code);
                break;
            }
        }
    }
    if (var_idx == -1) { fprintf(stderr, "Unknown variable\n"); exit(1); }
}

void PrintStatement(FILE *file, DifNode_t *stmt, VariableArr *arr, int *ram_base, int *param_count) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(param_count);

    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            switch (stmt->value.operation) {
                case kOperationIs:
                    PrintExpr(file, stmt->right, arr, *ram_base); {

                        int var_idx = -1;
                        FindVarPos(file, arr, stmt->left, *ram_base, param_count);
                    }
                    break;
                case kOperationReturn:
                    PrintExpr(file, stmt->left, arr, *ram_base);
                    PRINT(file, "POPR RBX"); // переделать
                
                case kOperationWrite:
                    PrintExpr(file, stmt->left, arr, *ram_base);
                    PRINT(file, "OUT");
                    break;
                case kOperationRead:
                    PRINT(file, "IN");
                    PRINT(file, "POPMN [%d]", arr->var_array[stmt->left->value.pos].pos_in_code);
                    break;
                case kOperationThen:
                    PrintStatement(file, stmt->left, arr, ram_base, param_count);
                    PrintStatement(file, stmt->right, arr, ram_base, param_count);
                    break;

                case kOperationIf: {
                        int lbl_false = NewLabel();
                        PrintExpr(file, stmt->left->left, arr, *ram_base);
                        PrintExpr(file, stmt->left->right, arr, *ram_base);
                        PRINT(file, "SUB");

                        PRINT(file, "%s :F_%d", ChooseCompareMode(stmt->left), lbl_false); //

                        PrintStatement(file, stmt->right, arr, ram_base, param_count);

                        PRINT(file, "\n:F_%d", lbl_false);
                    }
                    break;

                case kOperationAdd:
                case kOperationSub:
                case kOperationMul:
                case kOperationDiv:
                    PrintExpr(file, stmt, arr, *ram_base);
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
            PRINT(file, "PUSH %.0f", expr->value.number);
            break;
        case kVariable: {
                int var_idx = expr->value.pos;

                PRINT(file, "PUSHMN [%d]", arr->var_array[expr->value.pos].pos_in_code);
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

    int ram_base = 0; //
    int param_count = 0;

    DifNode_t *args = func_node->right->left;
    for (DifNode_t *arg = args; arg != NULL; arg = arg->right) {
        if (arg && arg->type == kVariable) {
            PRINT(file, "PUSH %d", arr->var_array[arg->value.pos].variable_value);
        }
        param_count++;
    }
    for (DifNode_t *arg = args; arg != NULL; arg = arg->left) {
        if (arg && arg->type == kVariable) {
            PRINT(file, "PUSH %d", arr->var_array[arg->value.pos].variable_value);
        }
        param_count++;
    }

    if (strcmp("main", arr->var_array[func_node->left->value.pos].variable_name) != 0) PRINT(file, "\n:%s", arr->var_array[func_node->left->value.pos].variable_name);

    for (DifNode_t *arg = args; arg != NULL; arg = arg->right) {
        if (arg && arg->type == kVariable) {
            PRINT(file, "POPMN [RAX]");
            PRINT(file, "PUSHR RAX");
            PRINT(file, "PUSH 1");
            PRINT(file, "ADD");
            PRINT(file, "POPR RAX");
        }
        param_count++;
    }
    for (DifNode_t *arg = args; arg != NULL; arg = arg->left) {
        if (arg && arg->type == kVariable) {
            PRINT(file, "POPMN [RAX]");
            PRINT(file, "PUSHR RAX");
            PRINT(file, "PUSH 1");
            PRINT(file, "ADD");
            PRINT(file, "POPR RAX");
        }
        param_count++;
    }

    ram_base += param_count;
    DifNode_t *body = func_node->right->right;
    PrintStatement(file, body, arr, &ram_base, &param_count);

    PRINT(file, "PUSHR RAX");
    PRINT(file, "PUSH %d", param_count);
    PRINT(file, "SUB");
    PRINT(file, "POPR RAX");
    if (strcmp("main", arr->var_array[func_node->left->value.pos].variable_name) == 0) PRINT(file, "HLT");
    else PRINT(file, "RET");
}

static void CleanPositions(VariableArr *arr) {
    assert(arr);

    for (size_t i = 0; i < arr->size; i++) {
        arr->var_array[i].pos_in_code = -1;
    }
}

void PrintProgram(FILE *file, DifNode_t *root, VariableArr *arr) {
    assert(file);
    assert(root);
    assert(arr);
    
    CleanPositions(arr);

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


static const char *ChooseCompareMode(DifNode_t *node) {
    assert(node);

    if (node && node->type == kOperation) {
        switch (node->value.operation) {
            case (kOperationA):  return "JA";
            case (kOperationAE): return "JAE";
            case (kOperationB):  return "JB";
            case (kOperationBE): return "JBE";
            case (kOperationE):  return "JE";
            default: return "NULL";
        }
    }
}