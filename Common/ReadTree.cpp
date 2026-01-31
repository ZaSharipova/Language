#include "Common/ReadTree.h"

#include "Common/Enums.h"
#include "Common/Structs.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "Front-End/Rules.h"
#include "Common/LanguageFunctions.h"
#include "Common/CommonFunctions.h"
#include "Common/StackFunctions.h"

static DifErrors CheckType(Lang_t title, LangNode_t *node, VariableArr *Variable_Array);
static DifErrors ParseTitle(const char *buffer, size_t *pos, char **out_title);
static DifErrors ParseMaybeNil(const char *buffer, size_t *pos, LangNode_t **out);
static DifErrors ExpectClosingParen(const char *buffer, size_t *pos);

static int CountArgs(LangNode_t *args_root);
static void RegisterInit(LangNode_t *func_name_node, LangNode_t *var_node, VariableArr *Variable_Array);
static void ScanInitsInSubtree(LangNode_t *func_name_node, LangNode_t *root, VariableArr *Variable_Array);
static void ComputeFuncSizes(LangNode_t *node, VariableArr *Variable_Array);

static void SkipSpaces(const char *buf, size_t *pos);
static DifErrors PrintSyntaxErrorNode(size_t pos, char c);

DifErrors ParseNodeFromString(const char *buffer, size_t *pos, LangNode_t *parent, LangNode_t **node_to_add, VariableArr *Variable_Array) {
    assert(buffer);
    assert(pos);
    assert(node_to_add);
    assert(Variable_Array);

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

    err = CheckType(title, node, Variable_Array);
    free(title);
    if (err != kSuccess) {
        return err;
    }

    LangNode_t *left = NULL;
    CHECK_ERROR_RETURN(ParseNodeFromString(buffer, pos, node, &left, Variable_Array), NULL, NULL, NULL);
    node->left = left;

    LangNode_t *right = NULL;
    CHECK_ERROR_RETURN(ParseNodeFromString(buffer, pos, node, &right, Variable_Array), NULL, NULL, NULL);
    node->right = right;

    if (node->type == kOperation && node->value.operation == kOperationFunction) {
        ComputeFuncSizes(node, Variable_Array);
    }

    CHECK_ERROR_RETURN(ExpectClosingParen(buffer, pos), NULL, NULL, NULL);

    *node_to_add = node;
    return kSuccess;
}

static void ComputeFuncSizes(LangNode_t *node, VariableArr *Variable_Array) {
    assert(Variable_Array);
    if (!node) return;

    ComputeFuncSizes(node->left, Variable_Array);
    ComputeFuncSizes(node->right, Variable_Array);

    if (IsThatOperation(node, kOperationFunction)) {
        LangNode_t *func_name = node->left;
        LangNode_t *args_root = node->right ? node->right->left  : NULL;
        LangNode_t *body_root = node->right ? node->right->right : NULL;

        int argc = CountArgs(args_root);
        Variable_Array->var_array[func_name->value.pos].variable_value = argc;
        ScanInitsInSubtree(func_name, body_root, Variable_Array);
    }
}

static DifErrors CheckType(Lang_t title, LangNode_t *node, VariableArr *Variable_Array) {
    assert(title);
    assert(node);
    assert(Variable_Array);
    
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

    for (size_t j = 0; j < Variable_Array->size; j++) {
        if (strcmp(Variable_Array->var_array[j].variable_name, title) == 0) {
            node->value.pos = j;
            return kSuccess;
        }
    }

    if (Variable_Array->size >= Variable_Array->capacity) {
        return kNoMemory;
    }

    ResizeArray(Variable_Array);
    Variable_Array->var_array[Variable_Array->size].variable_name = strdup(title);
    Variable_Array->var_array[Variable_Array->size].variable_value = 0;
    Variable_Array->var_array[Variable_Array->size].func_made = NULL;

    node->value.pos = Variable_Array->size;
    Variable_Array->size++;

    return kSuccess;
}

static int CountArgs(LangNode_t *args_root) {
    if (!args_root) {
        return 0;
    }

    if (IsThatOperation(args_root, kOperationComma)) {
        return CountArgs(args_root->left) + CountArgs(args_root->right);
    }

    return 1;
}

static void RegisterInit(LangNode_t *func_name_node, LangNode_t *var_node, VariableArr *Variable_Array) {
    assert(func_name_node);
    assert(var_node);
    assert(Variable_Array);

    VariableInfo *func_vi = &Variable_Array->var_array[func_name_node->value.pos];
    VariableInfo *var_vi  = &Variable_Array->var_array[var_node->value.pos];

    if (!var_vi->func_made || strcmp(var_vi->func_made, func_vi->variable_name) != 0) {
        var_vi->func_made = strdup(func_vi->variable_name);
        func_vi->variable_value++;
    }
}

static void ScanInitsInSubtree(LangNode_t *func_name_node, LangNode_t *root, VariableArr *Variable_Array) {
    assert(func_name_node);
    assert(Variable_Array);
    if (!root) return;

    if (IsThatOperation(root, kOperationIs) && root->left && root->left->type == kVariable) {
        RegisterInit(func_name_node, root->left, Variable_Array);
    }

    ScanInitsInSubtree(func_name_node, root->left, Variable_Array);
    ScanInitsInSubtree(func_name_node, root->right, Variable_Array);
}

static void SkipSpaces(const char *buf, size_t *pos) {
    assert(buf);
    assert(pos);

    while (buf[*pos] == ' ' || buf[*pos] == '\t' || buf[*pos] == '\n' || buf[*pos] == '\r') {
        (*pos)++;
    }
}

static DifErrors PrintSyntaxErrorNode(size_t pos, char c) {
    fprintf(stderr, "Syntax error at position %zu: unexpected character ", pos);

    if (c == '\0') {
        fprintf(stderr, "EOF");
    } else {
        fprintf(stderr, "'%c'", c);
    }
    fprintf(stderr, "\n");


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

    if (buffer[*pos] != '(') {
        return PrintSyntaxErrorNode(*pos, buffer[*pos]);
    }
    (*pos)++;
    SkipSpaces(buffer, pos);

    if (buffer[*pos] != '"') {
        return PrintSyntaxErrorNode(*pos, buffer[*pos]);
    }

    (*pos)++;
    size_t start = *pos;

    while (buffer[*pos] != '"' && buffer[*pos] != '\0') {
        (*pos)++;
    }

    if (buffer[*pos] == '\0') {
        return PrintSyntaxErrorNode(*pos, buffer[*pos]);
    }

    size_t len = *pos - start;

    char *title = (char *) calloc (len + 1, 1);
    if (!title) {
        return kNoMemory;
    }

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