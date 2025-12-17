#include "Front-End/TreeToAsm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "Structs.h"
#include "Enums.h"

static int label_counter = 0;
int last_pos = 0;
#define PRINT(file, fmt, ...) fprintf(file, fmt "\n", ##__VA_ARGS__)

int NewLabel() {
    return label_counter++;
}

static const char *ChooseCompareMode(LangNode_t *node);
void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int *ram_base, int *param_counter);

static void FindVarPosPopMN(FILE *file, VariableArr *arr, LangNode_t *node, int *ram_base, int *param_count) {
    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (strncmp(arr->var_array[i].variable_name, arr->var_array[node->value.pos].variable_name, strlen(arr->var_array[i].variable_name)) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = *ram_base + last_pos - *param_count;
                last_pos++;
                (*param_count)++;
                (*ram_base)++;
                //PRINT(file, "PUSH %d", arr->var_array[node->value.pos].variable_value);
                //printf("%d %d\n", node->value.pos, arr->var_array[node->value.pos].variable_value);
                PRINT(file, "PUSH %d", var_idx);
                PRINT(file, "POPR RCX");
                PRINT(file, "POPM [RCX]");
                PRINT(file, "PUSHR RAX");
                PRINT(file, "PUSH 1");
                PRINT(file, "ADD");
                PRINT(file, "POPR RAX\n");
            } else {
                var_idx = arr->var_array[i].pos_in_code;
                printf("aaa\n");
                PRINT(file, "PUSH %d", arr->var_array[node->value.pos].pos_in_code);
                PRINT(file, "POPR RCX");
                PRINT(file, "POPM [RCX]\n");
                break;
            }
        }
    }
    if (var_idx == -1) { fprintf(stderr, "Unknown variable\n"); exit(1); }
}

int FindVarPos(VariableArr *arr, LangNode_t *node, int *ram_base, int *param_count) {
    assert(arr);
    assert(node);
    assert(param_count);


    int var_idx = -1;
    for (size_t i = 0; i < arr->size; i++) {
        if (strncmp(arr->var_array[i].variable_name, arr->var_array[node->value.pos].variable_name, strlen(arr->var_array[i].variable_name)) == 0) {
            if (arr->var_array[i].pos_in_code == -1) {
                var_idx = arr->var_array[i].pos_in_code = *ram_base + last_pos - *param_count;
                last_pos++;
                (*param_count)++;
                (*ram_base)++;

            } else {
                var_idx = arr->var_array[i].pos_in_code;
            }
        }
    }

    return var_idx;
}

void PushParamsToStack(FILE *file, LangNode_t *args_node, VariableArr *arr, int *ram_base, int *param_count) {
    assert(file);
    assert(arr);
    assert(param_count);
    if (!args_node) return;

    if (!(args_node->type == kOperation && args_node->value.operation == kOperationThen)) {
        PrintExpr(file, args_node, arr, ram_base, param_count);
        return;
    }

    if (args_node->left) {
        PushParamsToStack(file, args_node->left, arr, ram_base, param_count);
    }

    if (args_node->right) {
        PushParamsToStack(file, args_node->right, arr, ram_base, param_count);
    }
}
void PrintParamsToRam(FILE *file, LangNode_t *stmt, VariableArr *arr, int *ram_base, int *param_count) {
    assert(file);
    assert(stmt);
    assert(arr);
    assert(ram_base);
    assert(param_count);

    LangNode_t *args = stmt->right->left;
    for (LangNode_t *arg = args; arg != NULL; arg = arg->right) {
        if (arg && arg->type == kVariable) {
            PRINT(file, "POPM [RAX]");
            PRINT(file, "PUSHR RAX");
            PRINT(file, "PUSH 1");
            PRINT(file, "ADD");
            PRINT(file, "POPR RAX\n");

            (*param_count)++;
            (*ram_base)++;
        }
    }
    for (LangNode_t *arg = args; arg != NULL; arg = arg->left) {
        if (arg && arg->type == kVariable) {
            PRINT(file, "POPM [RAX]");
            PRINT(file, "PUSHR RAX");
            PRINT(file, "PUSH 1");
            PRINT(file, "ADD");
            PRINT(file, "POPR RAX\n");

            (*param_count)++;
            (*ram_base)++;
        }
    }
}
void PrintStatement(FILE *file, LangNode_t *stmt, VariableArr *arr, int *ram_base, int *param_count) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(param_count);

    if (!stmt) return;

    switch (stmt->type) {
        case kOperation:
            printf("-%d-\n", stmt->value.operation);
            switch (stmt->value.operation) {
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
                    PRINT(file, "PUSH %d", *param_count);
                    PRINT(file, "SUB");
                    PRINT(file, "POPR RAX");
                    PRINT(file, "RET\n");
                    break;
                
                case kOperationWrite:
                    PrintExpr(file, stmt->left, arr, ram_base, param_count);
                    PRINT(file, "OUT\n");
                    break;

                case kOperationRead:
                    PRINT(file, "IN");
                    PrintStatement(file, stmt->left, arr, ram_base, param_count);
                    //PRINT(file, "POPMN [%d]\n", arr->var_array[stmt->left->value.pos].pos_in_code);
                    break;

                case kOperationThen:
                    PrintStatement(file, stmt->left, arr, ram_base, param_count);
                    PrintStatement(file, stmt->right, arr, ram_base, param_count);
                    break;

                case kOperationIf: {
                        int lbl_false = NewLabel();
                        PrintExpr(file, stmt->left->left, arr, ram_base, param_count);
                        PrintExpr(file, stmt->left->right, arr, ram_base, param_count);

                        PRINT(file, "%s :F_%d\n", ChooseCompareMode(stmt->left), lbl_false); //

                        PrintStatement(file, stmt->right, arr, ram_base, param_count); // переделать для else
                        PRINT(file, "\n:F_%d", lbl_false);
                    }
                    break;

                case kOperationAdd:
                case kOperationSub:
                case kOperationMul:
                case kOperationDiv:
                    PrintExpr(file, stmt, arr, ram_base, param_count);
                    break;

                default:
                    break;
            }
            break;

        case kVariable:
            FindVarPosPopMN(file, arr, stmt, ram_base, param_count);
            break;
        case kNumber:
            //PRINT(file, "%d", stmt->value.number);
            break;
    }
}

void PrintExpr(FILE *file, LangNode_t *expr, VariableArr *arr, int *ram_base, int *param_count) {
    assert(file);
    assert(arr);
    assert(ram_base);
    assert(param_count);

    if (!expr) return;

    switch (expr->type) {
        case kNumber:
            PRINT(file, "PUSH %.0f", expr->value.number);
            break;
        case kVariable: {
                int var_idx = FindVarPos(arr, expr, ram_base, param_count);

                PRINT(file, "PUSH %d", var_idx);
                PRINT(file, "POPR RCX");
                PRINT(file, "PUSHM [RCX]\n");
            }
            break;
        case kOperation:
            switch (expr->value.operation) {
                case kOperationCall:
                    PushParamsToStack(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "CALL :%s\n", arr->var_array[expr->left->value.pos].variable_name);
                    break;
                case kOperationAdd:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PrintExpr(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "ADD");
                    break;
                case kOperationSub:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PrintExpr(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "SUB");
                    break;
                case kOperationMul:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PrintExpr(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "MUL");
                    break;
                case kOperationDiv:
                    PrintExpr(file, expr->left, arr, ram_base, param_count);
                    PrintExpr(file, expr->right, arr, ram_base, param_count);
                    PRINT(file, "DIV");
                    break;
                default:
                    break;
            }
            break;
    }
}

void PrintFunction(FILE *file, LangNode_t *func_node, VariableArr *arr, int *ram_base) {
    assert(file);
    assert(arr);
    assert(ram_base);
    if (!func_node) return;

    int param_count = 0;
    last_pos = 0;

    LangNode_t *args = func_node->right->left;

    if (strcmp("main", arr->var_array[func_node->left->value.pos].variable_name) != 0) PRINT(file, "\n:%s", arr->var_array[func_node->left->value.pos].variable_name);

    for (LangNode_t *arg = args; arg != NULL; arg = arg->right) {
        if (arg && arg->type == kVariable) {
            //PRINT(file, "PUSH %d", arr->var_array[arg->value.pos].variable_value);
            PRINT(file, "POPM [RAX]");
            PRINT(file, "PUSHR RAX");
            PRINT(file, "PUSH 1");
            PRINT(file, "ADD");
            PRINT(file, "POPR RAX\n");
        }
        param_count++;
    }
    if (args && args->left) {
        for (LangNode_t *arg = args->left; arg != NULL; arg = arg->left) {
            if (arg && arg->type == kVariable) {
                PRINT(file, "POPM [RAX]");
                PRINT(file, "PUSHR RAX");
                PRINT(file, "PUSH 1");
                PRINT(file, "ADD");
                PRINT(file, "POPR RAX\n");
            }
            param_count++;
        }
    }

   (*ram_base) += param_count;
    LangNode_t *body = func_node->right->right;
    PrintStatement(file, body, arr, ram_base, &param_count);
       printf("!!%d\n", *ram_base);

    PRINT(file, "PUSHR RAX");
    PRINT(file, "PUSH %d", param_count);
    PRINT(file, "SUB");
    PRINT(file, "POPR RAX");
    (*ram_base) -= param_count;
    if (strcmp("main", arr->var_array[func_node->left->value.pos].variable_name) == 0) PRINT(file, "HLT");
    else PRINT(file, "RET");
}

static void CleanPositions(VariableArr *arr) {
    assert(arr);

    for (size_t i = 0; i < arr->size; i++) {
        arr->var_array[i].pos_in_code = -1;
    }
}

void PrintProgram(FILE *file, LangNode_t *root, VariableArr *arr, int *ram_base) {
    assert(file);
    assert(root);
    assert(arr);
    assert(ram_base);
    
    CleanPositions(arr);

    if (root->type == kOperation && root->value.operation == kOperationFunction) {
        PrintFunction(file, root, arr, ram_base);
    }
    
    if (root->left) {
        PrintProgram(file, root->left, arr, ram_base);
    }
    
    if (root->right) {
        PrintProgram(file, root->right, arr, ram_base);
    }
}


static const char *ChooseCompareMode(LangNode_t *node) {
    assert(node);

    if (node && node->type == kOperation) {
        switch (node->value.operation) {
            case (kOperationA):  return "JBE";
            case (kOperationAE): return "JB";
            case (kOperationB):  return "JAE";
            case (kOperationBE): return "JA";
            case (kOperationE):  return "JNE";
            case (kOperationNE): return "JE";
            default:             return "NULL";
        }
    }
    return "NULL";
}