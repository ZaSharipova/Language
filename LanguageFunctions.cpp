#include "LanguageFunctions.h"

#include "Enums.h"
#include "Structs.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Rules.h"

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

    return kSuccess;
}

DifErrors DtorVariableArray(VariableArr *arr) {
    assert(arr);

    arr->capacity = 0;
    arr->size = 0;

    free(arr->var_array);

    return kSuccess;
}

DifNode_t *NewNode(DifRoot *root, DifTypes type, Value value, DifNode_t *left, DifNode_t *right,
    VariableArr *Variable_Array) {
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

    DifNode_t *new_node = NULL;
    NodeCtor(&new_node, NULL);

    root->size ++;
    new_node->type = kVariable;
    VariableInfo *addr = NULL;

    for (size_t i = 0; i < VariableArr->size; i++) {
        if (strncmp(variable, VariableArr->var_array[i].variable_name, strlen(variable)) == 0) {
           addr = &VariableArr->var_array[i];
        }
    }

    if (!addr) {
        ResizeArray(VariableArr);
        VariableArr->var_array[VariableArr->size].variable_name = variable;
        addr = &VariableArr->var_array[VariableArr->size];
        VariableArr->size ++;
    }

        
    new_node->value.variable = addr;

    return new_node;
}

const char *ConvertEnumToOperation(OperationTypes type) {
    switch (type) {
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
        case kOperationNone:      return "none";
    }
    
    return NULL;
}

DifErrors PrintAST(DifNode_t *node, FILE *file) {
    if (!node) {
        fprintf(file, "nil");
        return kSuccess;
    }
    assert(file);

    fprintf(file, "( ");
    
    switch (node->type) {
        case kNumber:
            fprintf(file, "\"%d\"", (int)node->value.number);
            break;
        case kVariable:
            fprintf(file, "\"%s\"", node->value.variable->variable_name);
            break;
        case kOperation:
            fprintf(file, "\"%s\"", ConvertEnumToOperation(node->value.operation));
            break;
        default:
            fprintf(file, "\"UNKNOWN\"");
            break;
    }
    
    fprintf(file, " ");
    PrintAST(node->left, file);
    fprintf(file, " ");
    PrintAST(node->right, file);
    fprintf(file, " )");
    
    return kSuccess;
}

DifErrors ReadAST(FILE *file, DifNode_t **node_to_add) {
    assert(file);
    assert(node_to_add);


}

static void SkipSpaces(const char *buf, size_t *pos) {
    while (buf[*pos] == ' ' || buf[*pos] == '\t' || buf[*pos] == '\n' || buf[*pos] == '\r') {
        (*pos)++;
    }
}

static DifErrors SyntaxErrorNode(size_t pos, char c) {
    fprintf(stderr, "Syntax error at position %zu: unexpected character '%c'\n",
            pos,
            (c == '\0' ? 'EOF' : c));

    return kSyntaxError;
}

static const OpEntry OP_TABLE[] = {
    { "+",      kOperationAdd },
    { "-",      kOperationSub },
    { "*",      kOperationMul },
    { "/",      kOperationDiv },
    { "^",      kOperationPow },

    { "sin",    kOperationSin },
    { "cos",    kOperationCos },
    { "tg",     kOperationTg },
    { "ln",     kOperationLn },
    { "arctg",  kOperationArctg },
    { "sinh",   kOperationSinh },
    { "cosh",   kOperationCosh },
    { "tgh",    kOperationTgh },

    { "=",      kOperationIs },
    { "if",     kOperationIf },
    { "else",   kOperationElse },
    { "while",  kOperationWhile },
    { ";",   kOperationThen },

    { "NULL",      kOperationNone },
};
static const size_t OP_TABLE_SIZE = sizeof(OP_TABLE) / sizeof(OP_TABLE[0]);


static DifErrors CheckType(Dif_t title, DifNode_t *node, VariableArr *vars) {
    for (size_t i = 0; i < OP_TABLE_SIZE; i++) {
        if (strcmp(OP_TABLE[i].name, title) == 0) {
            node->type = kOperation;
            node->value.operation = OP_TABLE[i].type;
            return kSuccess;
        }
    }

    bool is_num = true;
    for (size_t k = 0; title[k]; k++) {
        if (!isdigit(title[k]) && title[k] != '.' && title[k] != '-') {
            is_num = false;
            break;
        }
    }

    if (is_num) {
        node->type = kNumber;
        node->value.number = atof(title);
        return kSuccess;
    }

    node->type = kVariable;

    for (size_t j = 0; j < vars->size; j++) {
        if (strcmp(vars->var_array[j].variable_name, title) == 0) {
            node->value.variable = &vars->var_array[j];
            return kSuccess;
        }
    }

    if (vars->size >= vars->capacity)
        return kNoMemory;

    vars->var_array[vars->size].variable_name = strdup(title);
    vars->var_array[vars->size].variable_value = 0;

    node->value.variable = &vars->var_array[vars->size];
    vars->size++;

    return kSuccess;
}

DifErrors ParseNodeFromString(const char *buffer, size_t *pos, DifNode_t *parent, DifNode_t **node_to_add, VariableArr *arr) {
    assert(buffer);
    assert(pos);
    assert(node_to_add);
    assert(arr);

    SkipSpaces(buffer, pos);

    if (strncmp(buffer + *pos, "nil", 3) == 0) {
        *pos += 3;
        *node_to_add = NULL;
        return kSuccess;
    }

    if (buffer[*pos] != '(') {
        return SyntaxErrorNode(*pos, buffer[*pos]);
    }
    (*pos)++;
    SkipSpaces(buffer, pos);

    if (buffer[*pos] != '"')
        return SyntaxErrorNode(*pos, buffer[*pos]);

    (*pos)++;
    size_t start = *pos;

    while (buffer[*pos] != '"' && buffer[*pos] != '\0')
        (*pos)++;

    if (buffer[*pos] == '\0')
        return SyntaxErrorNode(*pos, buffer[*pos]);

    size_t len = *pos - start;

    char *title = (char *) calloc (len + 1, sizeof(char));
    if (!title) return kNoMemory;

    memcpy(title, buffer + start, len);
    title[len] = '\0';

    (*pos)++;
    SkipSpaces(buffer, pos);

    DifNode_t *node = NULL;
    NodeCtor(&node, NULL);
    node->parent = parent;

    DifErrors err = CheckType(title, node, arr);
    if (err != kSuccess) {
        free(title);
        return err;
    }

    free(title);

    DifNode_t *left = NULL;
    CHECK_ERROR_RETURN(ParseNodeFromString(buffer, pos, node, &left, arr));
    node->left = left;

    DifNode_t *right = NULL;
    CHECK_ERROR_RETURN(ParseNodeFromString(buffer, pos, node, &right, arr));
    node->right = right;

    SkipSpaces(buffer, pos);

    if (buffer[*pos] != ')')
        return SyntaxErrorNode(*pos, buffer[*pos]);

    (*pos)++;

    *node_to_add = node;
    return kSuccess;
}

void GenerateCodeFromAST(DifNode_t *node, FILE *out) {
    assert(out);
    if (!node)
        return;

    switch (node->type) {
        case kNumber:
            fprintf(out, "%g", node->value.number);
            break;

        case kVariable:
            fprintf(out, "%s", node->value.variable->variable_name);
            break;

        case kOperation: {
            const char *op_str = NULL;
            for (size_t i = 0; i < OP_TABLE_SIZE; i++) {
                if (OP_TABLE[i].type == node->value.operation) {
                    op_str = OP_TABLE[i].name;
                    break;
                }
            }

            if (!op_str)
                op_str = "UNKNOWN";

            if (node->value.operation == kOperationIf) {
                fprintf(out, "if (");
                GenerateCodeFromAST(node->left, out);
                fprintf(out, ") {\n");
                GenerateCodeFromAST(node->right, out); 
                fprintf(out, "}");
            }
            else if (node->value.operation == kOperationThen) {
                GenerateCodeFromAST(node->left, out);
                fprintf(out, ";\n");
                GenerateCodeFromAST(node->right, out);
            }
            else if (node->value.operation == kOperationIs) {
                GenerateCodeFromAST(node->left, out);
                fprintf(out, " = ");
                GenerateCodeFromAST(node->right, out);
            }
            else if (node->value.operation == kOperationAdd  ||
                     node->value.operation == kOperationSub  ||
                     node->value.operation == kOperationMul  ||
                     node->value.operation == kOperationDiv  ||
                     node->value.operation == kOperationPow) {
                fprintf(out, "(");
                GenerateCodeFromAST(node->left, out);
                fprintf(out, " %s ", op_str);
                GenerateCodeFromAST(node->right, out);
                fprintf(out, ")");
            }
            else {
                GenerateCodeFromAST(node->left, out);
                GenerateCodeFromAST(node->right, out);
            }
            break;
        }

        default:
            fprintf(out, "UNKNOWN_NODE");
            break;
    }
}
