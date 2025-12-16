#include "LanguageFunctions.h"

#include "Enums.h"
#include "Structs.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Rules.h"

static const char *ConvertEnumToOperation(DifNode_t *node, VariableArr *arr);

size_t DEFAULT_SIZE = 60;

DifErrors DifRootCtor(DifRoot *root) {
    assert(root);

    root->root = NULL;
    root->size = 0;

    return kSuccess;
}

DifErrors NodeCtor(DifNode_t **node, Value *value) {
    assert(node);

    *node = (DifNode_t *) calloc (DEFAULT_SIZE, sizeof(DifNode_t));
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

DifErrors DeleteNode(DifRoot *root, DifNode_t *node) {
    assert(root);
    if (!node)
        return kSuccess;
    root->size--;

    if (node->left) {
        DeleteNode(root, node->left);
    }

    if (node->right) {
        DeleteNode(root, node->right);
    }

    node->parent = NULL;

    free(node);

    return kSuccess;
}

DifErrors TreeDtor(DifRoot *tree) {
    assert(tree);

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

    for (size_t i = 0; i < arr->capacity; i++) {
        arr->var_array[i].variable_value = -666;
    }
    
    return kSuccess;
}

DifErrors ResizeArray(VariableArr *arr)  {
    assert(arr);

    if (arr->size + 2 > arr->capacity) {
        VariableInfo *ptr = (VariableInfo *) realloc (arr->var_array, (arr->capacity += 2) * sizeof(VariableInfo));
        if (!ptr) {
            fprintf(stderr, "Memory error.\n");
            return kNoMemory;
        }
        arr->var_array = ptr;
    }

    for (size_t i = arr->size; i < arr->capacity; i++) {
        arr->var_array[i].variable_value = -666;
    }

    return kSuccess;
}

DifErrors DtorVariableArray(VariableArr *arr) {
    assert(arr);

    arr->capacity = 0;
    arr->size = 0;

    free(arr->var_array);

    return kSuccess;
}

DifNode_t *NewNode(DifRoot *root, DifTypes type, Value value, DifNode_t *left, DifNode_t *right) {
    assert(root);

    DifNode_t *new_node = NULL;
    NodeCtor(&new_node, NULL);

    root->size++;
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

    return new_node;
}

DifNode_t *NewVariable(DifRoot *root, const char *variable, VariableArr *VariableArr) {
    assert(root);
    assert(variable);
    assert(VariableArr);

    DifNode_t *new_node = NULL;
    NodeCtor(&new_node, NULL);

    root->size ++;
    new_node->type = kVariable;
    size_t pos = 0;
    bool found_flag = false;

    for (size_t i = 0; i < VariableArr->size; i++) {
        if (strcmp(variable, VariableArr->var_array[i].variable_name) == 0) {
            pos = i;
            found_flag = true;
        }
    }

    if (!found_flag) {
        ResizeArray(VariableArr);
        VariableArr->var_array[VariableArr->size].variable_name = variable;
        pos = VariableArr->size;
        VariableArr->size ++;
    }
    
    new_node->value.pos = pos;

    return new_node;
}

DifErrors PrintAST(DifNode_t *node, FILE *file, VariableArr *arr) {
    assert(file);
    assert(arr);

    if (!node) {
        fprintf(file, "nil");
        return kSuccess;
    }

    fprintf(file, "( ");
    
    switch (node->type) {
        case kNumber:
            fprintf(file, "\"%d\"", (int)node->value.number);
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
    
    fprintf(file, " ");
    PrintAST(node->left, file, arr);
    fprintf(file, " ");
    PrintAST(node->right, file, arr);
    fprintf(file, " )");
    
    return kSuccess;
}

static const char *ConvertEnumToOperation(DifNode_t *node, VariableArr *arr) {
    assert(node);
    assert(arr);

    switch (node->value.operation) {
        case kOperationAdd:       return "+";
        case kOperationSub:       return "-";
        case kOperationMul:       return "*";
        case kOperationDiv:       return "/";
        case kOperationPow:       return "^";
        case kOperationSin:       return "sin";
        case kOperationCos:       return "cos";
        case kOperationTg:        return "tg";
        case kOperationLn:        return "ln";
        case kOperationArctg:     return "arctg";
        case kOperationSinh:      return "sh";
        case kOperationCosh:      return "ch";
        case kOperationTgh:       return "th";
        case kOperationIs:        return "=";
        case kOperationIf:        return "if";
        case kOperationElse:      return "else";
        case kOperationWhile:     return "while";
        case kOperationThen:      return ";";
        case kOperationParOpen:   return "(";
        case kOperationParClose:  return ")";
        case kOperationBraceOpen: return "{";
        case kOperationBraceClose:return "}";
        case kOperationWrite:     return "print";
        case kOperationRead:      return "scanf";
        case kOperationComma:     return ",";
        case kOperationCall:      return "call";
        case kOperationFunction:  return "func";

        case kOperationNone:      return "none";
    }
    return NULL;
}