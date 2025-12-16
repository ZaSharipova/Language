#include "TreeToCode.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Enums.h"
#include "Structs.h"
#include "Rules.h"
#include "LanguageFunctions.h"

static DifErrors CheckType(Dif_t title, DifNode_t *node, VariableArr *arr);
static DifErrors ParseTitle(const char *buffer, size_t *pos, char **out_title);
static DifErrors ParseMaybeNil(const char *buffer, size_t *pos, DifNode_t **out);
static DifErrors ExpectClosingParen(const char *buffer, size_t *pos);

static void SkipSpaces(const char *buf, size_t *pos);
static DifErrors SyntaxErrorNode(size_t pos, char c);

static void SkipSpaces(const char *buf, size_t *pos) {
    assert(buf);
    assert(pos);

    while (buf[*pos] == ' ' || buf[*pos] == '\t' || buf[*pos] == '\n' || buf[*pos] == '\r') {
        (*pos)++;
    }
}

static DifErrors SyntaxErrorNode(size_t pos, char c) {
    fprintf(stderr, "Syntax error at position %zu: unexpected character '%c'\n", pos, (c == '\0' ? 'EOF' : c));

    return kSyntaxError;
}

static const OpEntry OP_TABLE[] = {
    { "+",      kOperationAdd },
    { "-",      kOperationSub }, //
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
    { "func",   kOperationFunction },
    { "return", kOperationReturn },
    { "call",   kOperationCall },
    { "print",  kOperationWrite },
    { "scanf",  kOperationRead },

    { "<",       kOperationB },
    { "<=",      kOperationBE },
    { ">",       kOperationA },
    { ">=",      kOperationAE },
    { "==",      kOperationE },

    { ";",      kOperationThen },
    { ",",       kOperationComma },

    { "NULL",   kOperationNone },
};
static const size_t OP_TABLE_SIZE = sizeof(OP_TABLE) / sizeof(OP_TABLE[0]);

DifErrors ParseNodeFromString(const char *buffer, size_t *pos, DifNode_t *parent, DifNode_t **node_to_add, VariableArr *arr) {
    assert(buffer);
    assert(pos);
    assert(node_to_add);
    assert(arr);

    DifErrors err = ParseMaybeNil(buffer, pos, node_to_add);
    if (err == kSuccess) {
        return kSuccess;
    }

    char *title = NULL;
    err = ParseTitle(buffer, pos, &title);
    if (err != kSuccess)
        return err;

    DifNode_t *node = NULL;
    NodeCtor(&node, NULL);
    node->parent = parent;

    err = CheckType(title, node, arr);
    free(title);
    if (err != kSuccess)
        return err;

    DifNode_t *left = NULL;
    CHECK_ERROR_RETURN(ParseNodeFromString(buffer, pos, node, &left, arr));
    node->left = left;

    DifNode_t *right = NULL;
    CHECK_ERROR_RETURN(ParseNodeFromString(buffer, pos, node, &right, arr));
    node->right = right;

    CHECK_ERROR_RETURN(ExpectClosingParen(buffer, pos));

    *node_to_add = node;
    return kSuccess;
}

static DifErrors CheckType(Dif_t title, DifNode_t *node, VariableArr *arr) {
    assert(node);
    assert(arr);
    
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

    for (size_t j = 0; j < arr->size; j++) {
        if (strcmp(arr->var_array[j].variable_name, title) == 0) {
            node->value.pos = j;
            return kSuccess;
        }
    }

    if (arr->size >= arr->capacity)
        return kNoMemory;

    arr->var_array[arr->size].variable_name = strdup(title);
    arr->var_array[arr->size].variable_value = 0;

    node->value.pos = arr->size;
    arr->size++;

    return kSuccess;
}

static void PrintIndent(FILE *out, int indent) {
    for (int i = 0; i < indent; ++i)
        fputc('\t', out);
}

static void GenExpr(DifNode_t *node, FILE *out, VariableArr *arr);

static void GenThenChain(DifNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    DifNode_t *stmt = node;
    while (stmt && stmt->type == kOperation && stmt->value.operation == kOperationThen) {
        PrintIndent(out, indent);
        GenExpr(stmt->left, out, arr);
        fprintf(out, ";\n");
        stmt = stmt->right;
    }
    if (stmt) {
        PrintIndent(out, indent);
        GenExpr(stmt, out, arr);
        fprintf(out, ";\n");
    }
}

static void GenIf(DifNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);
    
    PrintIndent(out, indent);
    fprintf(out, "if (");
    GenExpr(node->left, out, arr);
    fprintf(out, ") {\n");
    if (node->right)
        GenThenChain(node->right, out, arr, indent + 1);
    PrintIndent(out, indent);
    fprintf(out, "}");
}

static void GenWhile(DifNode_t *node, FILE *out, VariableArr *arr, int indent) {
    PrintIndent(out, indent);
    fprintf(out, "while (");
    GenExpr(node->left, out, arr);
    fprintf(out, ") {\n");
    if (node->right)
        GenThenChain(node->right, out, arr, indent + 1);
    PrintIndent(out, indent);
    fprintf(out, "}");
}

static void GenFunction(DifNode_t *node, FILE *out, VariableArr *arr, int indent) {
    DifNode_t *name = node->left;
    DifNode_t *pair = node->right;
    DifNode_t *args = NULL;
    DifNode_t *body = NULL;

    if (pair &&
        pair->type == kOperation &&
        pair->value.operation == kOperationThen) {
        args = pair->left;
        body = pair->right;
    } else {
        body = pair;
    }

    PrintIndent(out, indent);
    fprintf(out, "func ");
    if (name)
        GenExpr(name, out, arr);
    fprintf(out, "(");
    if (args)
        GenExpr(args, out, arr);
    fprintf(out, ") {\n");
    if (body)
        GenThenChain(body, out, arr, indent + 1);
    PrintIndent(out, indent);
    fprintf(out, "}");
}

static void GenExpr(DifNode_t *node, FILE *out, VariableArr *arr) {
    if (!node) return;

    switch (node->type) {
        case kNumber:
            fprintf(out, "%g", node->value.number);
            break;

        case kVariable:
            fprintf(out, "%s", arr->var_array[node->value.pos].variable_name);
            break;

        case kOperation: {
            switch (node->value.operation) {
                case kOperationAdd:
                case kOperationSub:
                case kOperationMul:
                case kOperationDiv:
                case kOperationPow: {
                    const char *op_str = "?";
                    if (node->value.operation == kOperationAdd) op_str = "+";
                    else if (node->value.operation == kOperationSub) op_str = "-";
                    else if (node->value.operation == kOperationMul) op_str = "*";
                    else if (node->value.operation == kOperationDiv) op_str = "/";
                    else if (node->value.operation == kOperationPow) op_str = "^";

                    fprintf(out, "(");
                    GenExpr(node->left, out, arr);
                    fprintf(out, " %s ", op_str);
                    GenExpr(node->right, out, arr);
                    fprintf(out, ")");
                    break;
                }

                case kOperationComma:
                    GenExpr(node->left, out, arr);
                    fprintf(out, ", ");
                    GenExpr(node->right, out, arr);
                    break;

                case kOperationIs:
                    GenExpr(node->left, out, arr);
                    fprintf(out, " = ");
                    GenExpr(node->right, out, arr);
                    break;

                case kOperationCall:
                    GenExpr(node->left, out, arr);
                    fprintf(out, "(");
                    if (node->right)
                        GenExpr(node->right, out, arr);
                    fprintf(out, ")");
                    break;

                default:
                    if (node->left)
                        GenExpr(node->left, out, arr);
                    if (node->right)
                        GenExpr(node->right, out, arr);
                    break;
            }
            break;
        }

        default:
            fprintf(out, "UNKNOWN_NODE");
            break;
    }
}

void GenerateCodeFromAST(DifNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(out);
    assert(arr);
    if (!node) return;

    if (node->type == kOperation) {
        switch (node->value.operation) {
            case kOperationIf:
                GenIf(node, out, arr, indent);
                return;
            case kOperationWhile:
                GenWhile(node, out, arr, indent);
                return;
            case kOperationFunction:
                GenFunction(node, out, arr, indent);
                return;
            case kOperationThen:
                GenThenChain(node, out, arr, indent);
                return;
            default:
                break;
        }
    }

    PrintIndent(out, indent);
    GenExpr(node, out, arr);
    fprintf(out, ";\n");
}

static DifErrors ParseMaybeNil(const char *buffer, size_t *pos, DifNode_t **out) {
    assert(buffer);
    assert(pos);
    assert(out);

    SkipSpaces(buffer, pos);

    if (strncmp(buffer + *pos, "nil", 3) == 0) {
        *pos += 3;
        *out = NULL;
        return kSuccess;
    }

    return kFailure;
}

static DifErrors ParseTitle(const char *buffer, size_t *pos, char **out_title) {
    assert(buffer);
    assert(pos);
    assert(out_title);

    if (buffer[*pos] != '(')
        return SyntaxErrorNode(*pos, buffer[*pos]);
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

    char *title = (char *) calloc(len + 1, 1);
    if (!title) return kNoMemory;

    memcpy(title, buffer + start, len);
    title[len] = '\0';

    (*pos)++;
    SkipSpaces(buffer, pos);

    *out_title = title;
    return kSuccess;
}

static DifErrors ExpectClosingParen(const char *buffer, size_t *pos) {
    SkipSpaces(buffer, pos);

    if (buffer[*pos] != ')')
        return SyntaxErrorNode(*pos, buffer[*pos]);
    (*pos)++;

    return kSuccess;
}
