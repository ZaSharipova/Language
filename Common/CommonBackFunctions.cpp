#include "Common/CommonBackFunctions.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "Common/Structs.h"
#include "Common/CommonFunctions.h"

int FindVarPos(VariableArr *VariableArr, LangNode_t *node, AsmInfo *info) {
    assert(VariableArr);
    assert(node);
    assert(info);

    int var_idx = -1;
    for (size_t i = 0; i < VariableArr->size; i++) {
        if (strcmp(VariableArr->var_array[i].variable_name, VariableArr->var_array[node->value.pos].variable_name) == 0) {
            if (VariableArr->var_array[i].pos_in_code == -1) {
                var_idx = VariableArr->var_array[i].pos_in_code = info->counter++;
            } else {
                var_idx = VariableArr->var_array[i].pos_in_code;
            }
        }
    }

    return var_idx;
}

void CleanPositions(VariableArr *arr) {
    assert(arr);

    for (size_t i = 0; i < arr->size; i++) {
        arr->var_array[i].pos_in_code = -1;
    }
}

int CountArgs(LangNode_t *args_node) {
    if (!args_node) return 0;
    if (!IsThatOperation(args_node, kOperationComma)) return 1;

    return CountArgs(args_node->left) + CountArgs(args_node->right);
}