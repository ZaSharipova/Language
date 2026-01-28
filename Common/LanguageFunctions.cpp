#include "Common/LanguageFunctions.h"

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Front-End/Rules.h"
#include "Common/StackFunctions.h"

#include <stdio.h>
#include <assert.h>
#include <cstdlib>
#include <string.h>
#include <ctype.h>

#define TreeNameFromTable(type) NAME_TYPES_TABLE[type].name_in_tree

static const char *ConvertEnumToOperation(LangNode_t *node, VariableArr *arr);

size_t DEFAULT_SIZE = 60;

DifErrors LangRootCtor(LangRoot *root) {
    assert(root);

    root->root = NULL;
    root->size = 0;

    return kSuccess;
}

DifErrors NodeCtor(LangNode_t **node, Value *value) {
    assert(node);

    *node = (LangNode_t *) calloc (1, sizeof(LangNode_t));
    if (!*node) {
        fprintf(stderr, "No memory to calloc NODE.\n");
        return kNoMemory;
    }

    if (value) {
        (*node)->value = *value; 
    } else {
        (*node)->type = kNumber;
        (*node)->value.number = 0;
    }
    
    (*node)->left =  NULL;
    (*node)->right =  NULL;
    (*node)->parent = NULL;

    return kSuccess;
}

DifErrors DeleteNode(LangRoot *root, LangNode_t *node) {
    assert(root);
    if (!node) return kSuccess;

    root->size--;

    if (node->left) {
        DeleteNode(root, node->left);
    }

    if (node->right) {
        DeleteNode(root, node->right);
    }

    //node->parent = NULL;
    free(node);
    node = NULL;

    return kSuccess;
}

DifErrors TreeDtor(LangRoot *tree) {
    if (!tree) {
        return kSuccess;
    }

    DeleteNode(tree, tree->root);

    tree->root =  NULL;
    tree->size = 0;

    return kSuccess;
}

DifErrors InitArrOfVariable(VariableArr *arr, size_t capacity) {
    assert(arr);

    arr->capacity = capacity;
    arr->size = 0;

    arr->var_array = (VariableInfo *) calloc (capacity, sizeof(VariableInfo));
    if (!arr->var_array) {
        fprintf(stderr, "Memory error.\n");
        return kNoMemory;
    }

    for (size_t i = 0; i < capacity; ++i) {
        arr->var_array[i].variable_name  = NULL;
        arr->var_array[i].func_made      = NULL;
        arr->var_array[i].variable_value = POISON;
        arr->var_array[i].params_number  = POISON;
        arr->var_array[i].type           = kUnknown;
    }
    
    return kSuccess;
}

DifErrors ResizeArray(VariableArr *arr)  {
    assert(arr);

    if (arr->size + 2 > arr->capacity) {
        arr->capacity += 2;
        
        VariableInfo *new_array = (VariableInfo *) calloc (arr->capacity, sizeof(VariableInfo));
        if (!new_array) {
            fprintf(stderr, "Memory error.\n");
            return kNoMemory;
        }
        
        for (size_t i = 0; i < arr->size; i++) {
            new_array[i] = arr->var_array[i];
        }
        
        for (size_t i = arr->size; i < arr->capacity; i++) {
            new_array[i].variable_name  = NULL;
            new_array[i].func_made      = NULL;
            new_array[i].variable_value = POISON;
            new_array[i].params_number  = POISON;
            new_array[i].type           = kUnknown;
        }
        
        free(arr->var_array);
        arr->var_array = new_array;
    }

    return kSuccess;
}

DifErrors DtorVariableArray(VariableArr *arr) {
    if (!arr || !arr->var_array) {
        return kSuccess;
    }

    for (size_t i = 0; i < arr->size; i++) {
        free(arr->var_array[i].variable_name);
        free(arr->var_array[i].func_made);
    }

    free(arr->var_array);
    arr->var_array = NULL;
    arr->capacity  = 0;
    arr->size      = 0;

    return kSuccess;
}

LangNode_t *NewNode(Language *lang_info, DifTypes type, Value value, LangNode_t *left, LangNode_t *right) {
    assert(lang_info);

    LangNode_t *new_node = NULL;
    NodeCtor(&new_node, NULL);

    lang_info->root->size++;
    new_node->type = type;

    switch (type) {
    case kNumber:
        new_node->value.number = value.number;
        break;
    case kVariable: {
        new_node->value = value;
    
        break;
    }
    case kOperation:
        new_node->value.operation = value.operation;
        new_node->left = left;
        new_node->right = right;

        if (left)  left->parent  = new_node;
        if (right) right->parent = new_node;

        break;
    }

    if (StackPush(lang_info->tokens, new_node, stderr) != kSuccess) {
        fprintf(stderr, "Error making new node.\n");
        return NULL;
    }
    return new_node;
}

LangNode_t *NewVariable(Language *lang_info, char *variable, VariableArr *VariableArr) {
    assert(lang_info);
    assert(variable);
    assert(VariableArr);

    LangNode_t *new_node = NULL;
    NodeCtor(&new_node, NULL);

    lang_info->root->size ++;
    new_node->type = kVariable;
    size_t pos = 0;
    bool found_flag = false;

    for (size_t i = 0; i < VariableArr->size; i++) {
        const char *name = VariableArr->var_array[i].variable_name;
        if (name && strcmp(variable, name) == 0) {
            pos = i;
            found_flag = true;
        }
    }

    if (!found_flag) {
        ResizeArray(VariableArr);
        VariableArr->var_array[VariableArr->size].variable_name = variable;
        
        pos = VariableArr->size;
        VariableArr->var_array[VariableArr->size].func_made = NULL;
        VariableArr->size ++;
    } else {
        free(variable);
    }
    
    new_node->value.pos = pos;

    if (StackPush(lang_info->tokens, new_node, stderr) != kSuccess) {
        fprintf(stderr, "Error making new node.\n");
        return NULL;
    }
    return new_node;
}

static void PrintIndent(FILE *file, int indent) {
    for (int i = 0; i < indent; i++) {
        fputc('\t', file);
    }
}

DifErrors PrintAST(LangNode_t *node, FILE *file, VariableArr *arr, int indent) {
    assert(file);
    assert(arr);

    if (!node) {
        PrintIndent(file, indent);
        fprintf(file, "nil\n");
        return kSuccess;
    }

    PrintIndent(file, indent);
    fprintf(file, "( ");
    
    switch (node->type) {
        case kNumber:
            fprintf(file, "\"%.0f\"", node->value.number);
            break;
        case kVariable:
            fprintf(file, "\"%s\"", arr->var_array[node->value.pos].variable_name);
            break;
        case kOperation:
            fprintf(file, "\"%s\"", ConvertEnumToOperation(node, arr));
            break;
        default:
            fprintf(file, "\"UNKNOWN\"");
            break;
    }
    fprintf(file, "\n");
    
    PrintAST(node->left, file, arr, indent + 1);
    
    // if (node->right) {
        fprintf(file, "\n");
        PrintAST(node->right, file, arr, indent + 1);
    // }
    
    PrintIndent(file, indent);
    fprintf(file, ")\n");
    
    return kSuccess;
}


static const char *ConvertEnumToOperation(LangNode_t *node, VariableArr *arr) {
    assert(node);
    assert(arr);

    if ((size_t)node->value.operation < OP_TABLE_SIZE) {
        return NAME_TYPES_TABLE[node->value.operation].name_in_tree;
    }

    fprintf(stderr, "Error while trying to convert enum to operation, because the number of operation is more than nametable size.\n");
    return NULL;
}