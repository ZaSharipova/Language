#include "Common/LanguageFunctions.h"

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Front-End/Rules.h"

#include <stdio.h>
#include <assert.h>
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

    node->parent = NULL;

    free(node);

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

LangNode_t *NewNode(LangRoot *root, DifTypes type, Value value, LangNode_t *left, LangNode_t *right) {
    assert(root);

    LangNode_t *new_node = NULL;
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

LangNode_t *NewVariable(LangRoot *root, const char *variable, VariableArr *VariableArr) {
    assert(root);
    assert(variable);
    assert(VariableArr);

    LangNode_t *new_node = NULL;
    NodeCtor(&new_node, NULL);

    root->size ++;
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
        VariableArr->var_array[VariableArr->size].variable_name = strdup(variable);
        pos = VariableArr->size;
        VariableArr->var_array[VariableArr->size].func_made = NULL;
        VariableArr->size ++;
    }
    
    new_node->value.pos = pos;

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

    switch (node->value.operation) {
        case kOperationAdd:       return TreeNameFromTable(kOperationAdd);
        case kOperationSub:       return TreeNameFromTable(kOperationSub);
        case kOperationMul:       return TreeNameFromTable(kOperationMul);
        case kOperationDiv:       return TreeNameFromTable(kOperationDiv);
        case kOperationPow:       return TreeNameFromTable(kOperationPow);
        case kOperationSin:       return TreeNameFromTable(kOperationSin);
        case kOperationSQRT:      return TreeNameFromTable(kOperationSQRT);
        case kOperationCos:       return TreeNameFromTable(kOperationCos);
        case kOperationTg:        return TreeNameFromTable(kOperationTg);
        case kOperationLn:        return TreeNameFromTable(kOperationLn);
        case kOperationArctg:     return TreeNameFromTable(kOperationArctg);
        case kOperationSinh:      return TreeNameFromTable(kOperationSinh);
        case kOperationCosh:      return TreeNameFromTable(kOperationCosh);
        case kOperationTgh:       return TreeNameFromTable(kOperationTgh);

        case kOperationIs:        return TreeNameFromTable(kOperationIs);
        case kOperationIf:        return TreeNameFromTable(kOperationIf);
        case kOperationElse:      return TreeNameFromTable(kOperationElse);
        case kOperationWhile:     return TreeNameFromTable(kOperationWhile);
        case kOperationThen:      return TreeNameFromTable(kOperationThen);
        case kOperationParOpen:   return TreeNameFromTable(kOperationParOpen);
        case kOperationParClose:  return TreeNameFromTable(kOperationParClose);
        case kOperationBraceOpen: return TreeNameFromTable(kOperationBraceOpen);
        case kOperationBraceClose:return TreeNameFromTable(kOperationBraceClose);
        case kOperationWrite:     return TreeNameFromTable(kOperationWrite);
        case kOperationRead:      return TreeNameFromTable(kOperationRead);
        case kOperationComma:     return TreeNameFromTable(kOperationComma);
        case kOperationCall:      return TreeNameFromTable(kOperationCall);
        case kOperationFunction:  return TreeNameFromTable(kOperationFunction);
        case kOperationReturn:    return TreeNameFromTable(kOperationReturn);
        case kOperationHLT:       return TreeNameFromTable(kOperationHLT);
        case kOperationWriteChar: return TreeNameFromTable(kOperationWriteChar);

        case kOperationB:         return TreeNameFromTable(kOperationB);
        case kOperationBE:        return TreeNameFromTable(kOperationBE);
        case kOperationA:         return TreeNameFromTable(kOperationA);
        case kOperationAE:        return TreeNameFromTable(kOperationAE);
        case kOperationE:         return TreeNameFromTable(kOperationE);
        case kOperationNE:        return TreeNameFromTable(kOperationNE);

        case kOperationNone:      return "NULL";
    }

    return NULL;
}