#include "ReadTree.h"

#include "Enums.h"
#include "Structs.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Front-End/Rules.h"
#include "Front-End/LanguageFunctions.h"

static DifErrors CheckType(Lang_t title, LangNode_t *node, VariableArr *arr);
static DifErrors ParseTitle(const char *buffer, size_t *pos, char **out_title);
static DifErrors ParseMaybeNil(const char *buffer, size_t *pos, LangNode_t **out);
static DifErrors ExpectClosingParen(const char *buffer, size_t *pos);

static int CountArgs(LangNode_t *args_root);
static void RegisterInit(LangNode_t *func_name_node, LangNode_t *var_node, VariableArr *arr);
static void ScanInitsInSubtree(LangNode_t *func_name_node, LangNode_t *root, VariableArr *arr);
static void ComputeFuncSizes(LangNode_t *node, VariableArr *arr);

static void SkipSpaces(const char *buf, size_t *pos);
static bool IsThatOperation(LangNode_t *node, OperationTypes type);
static DifErrors PrintSyntaxErrorNode(size_t pos, char c);

DifErrors ParseNodeFromString(const char *buffer, size_t *pos, LangNode_t *parent, LangNode_t **node_to_add, VariableArr *arr) {
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
    if (err != kSuccess) {
        return err;
    }

    LangNode_t *node = NULL;
    NodeCtor(&node, NULL);
    node->parent = parent;

    err = CheckType(title, node, arr);
    free(title);
    if (err != kSuccess) {
        return err;
    }

    LangNode_t *left = NULL;
    CHECK_ERROR_RETURN(ParseNodeFromString(buffer, pos, node, &left, arr));
    node->left = left;

    LangNode_t *right = NULL;
    CHECK_ERROR_RETURN(ParseNodeFromString(buffer, pos, node, &right, arr));
    node->right = right;

    if (node->type == kOperation && node->value.operation == kOperationFunction) {
        ComputeFuncSizes(node, arr);
    }

    CHECK_ERROR_RETURN(ExpectClosingParen(buffer, pos));

    *node_to_add = node;
    return kSuccess;
}

static void ComputeFuncSizes(LangNode_t *node, VariableArr *arr) {
    assert(arr);
    if (!node) return;

    ComputeFuncSizes(node->left, arr);
    ComputeFuncSizes(node->right, arr);

    if (IsThatOperation(node, kOperationFunction)) {

        LangNode_t *func_name = node->left;
        LangNode_t *args_root = node->right ? node->right->left  : NULL;
        LangNode_t *body_root = node->right ? node->right->right : NULL;

        int argc = CountArgs(args_root);
        arr->var_array[func_name->value.pos].variable_value = argc;
        ScanInitsInSubtree(func_name, body_root, arr);
    }
}

static DifErrors CheckType(Lang_t title, LangNode_t *node, VariableArr *arr) {
    assert(title);
    assert(node);
    assert(arr);
    
    for (size_t i = 0; i < OP_TABLE_SIZE; i++) {
        if (strcmp(NAME_TYPES_TABLE[i].name_in_tree, title) == 0) {
            node->type = kOperation;
            node->value.operation = NAME_TYPES_TABLE[i].type;
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

    if (arr->size >= arr->capacity) {
        return kNoMemory;
    }

    ResizeArray(arr);
    arr->var_array[arr->size].variable_name = strdup(title);
    arr->var_array[arr->size].variable_value = 0;
    arr->var_array[arr->size].func_made = NULL;

    node->value.pos = arr->size;
    arr->size++;

    return kSuccess;
}

static int CountArgs(LangNode_t *args_root) {
    if (!args_root) return 0;

    if (IsThatOperation(args_root, kOperationComma)) {
        return CountArgs(args_root->left) + CountArgs(args_root->right);
    }

    return 1;
}

static void RegisterInit(LangNode_t *func_name_node, LangNode_t *var_node, VariableArr *arr) {
    assert(func_name_node);
    assert(var_node);
    assert(arr);

    VariableInfo *func_vi = &arr->var_array[func_name_node->value.pos];
    VariableInfo *var_vi  = &arr->var_array[var_node->value.pos];

    if (!var_vi->func_made || strcmp(var_vi->func_made, func_vi->variable_name) != 0) {
        var_vi->func_made = strdup(func_vi->variable_name);
        func_vi->variable_value++;
    }
}

static void ScanInitsInSubtree(LangNode_t *func_name_node, LangNode_t *root, VariableArr *arr) {
    assert(func_name_node);
    assert(arr);
    if (!root) return;

    if (IsThatOperation(root, kOperationIs) &&
        root->left && root->left->type == kVariable) {
        
        RegisterInit(func_name_node, root->left, arr);
    }

    ScanInitsInSubtree(func_name_node, root->left, arr);
    ScanInitsInSubtree(func_name_node, root->right, arr);
}

static void SkipSpaces(const char *buf, size_t *pos) {
    assert(buf);
    assert(pos);

    while (buf[*pos] == ' ' || buf[*pos] == '\t' || buf[*pos] == '\n' || buf[*pos] == '\r') {
        (*pos)++;
    }
}

static bool IsThatOperation(LangNode_t *node, OperationTypes type) {
    if (node && node->type == kOperation && node->value.operation == type) {
        return true;
    }
    return false;
}

static DifErrors PrintSyntaxErrorNode(size_t pos, char c) {
    fprintf(stderr, "Syntax error at position %zu: unexpected character '%c'\n", pos, (c == '\0' ? 'EOF' : c));

    return kSyntaxError;
}

static DifErrors ParseMaybeNil(const char *buffer, size_t *pos, LangNode_t **out) {
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
        return PrintSyntaxErrorNode(*pos, buffer[*pos]);
    (*pos)++;
    SkipSpaces(buffer, pos);

    if (buffer[*pos] != '"')
        return PrintSyntaxErrorNode(*pos, buffer[*pos]);

    (*pos)++;
    size_t start = *pos;

    while (buffer[*pos] != '"' && buffer[*pos] != '\0') {
        (*pos)++;
    }

    if (buffer[*pos] == '\0') {
        return PrintSyntaxErrorNode(*pos, buffer[*pos]);
    }

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
    assert(buffer);
    assert(pos);

    SkipSpaces(buffer, pos);

    if (buffer[*pos] != ')') {
        return PrintSyntaxErrorNode(*pos, buffer[*pos]);
    }
    (*pos)++;

    return kSuccess;
}

