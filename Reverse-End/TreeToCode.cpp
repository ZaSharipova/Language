#include "Reverse-End/TreeToCode.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Enums.h"
#include "Structs.h"
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
    { "+",      kOperationAdd },          // "+" → "augeo"
    { "-",      kOperationSub },          // "-" → "minuo"
    { "*",      kOperationMul },          // "*" → "multiplico"
    { "/",      kOperationDiv },          // "/" → "divido"
    { "^",      kOperationPow },

    { "sin",    kOperationSin },
    { "sqrt",   kOperationSQRT },
    { "cos",    kOperationCos },
    { "tg",     kOperationTg },
    { "ln",     kOperationLn },
    { "arctg",  kOperationArctg },
    { "sinh",   kOperationSinh },
    { "cosh",   kOperationCosh },
    { "tgh",    kOperationTgh },

    { "=",      kOperationIs },
    { "if",     kOperationIf },           // "if" → "si"
    { "else",   kOperationElse },         // "else" → "altius"
    { "while",  kOperationWhile },        // "while" → "perpetuum"
    { "func",   kOperationFunction },     // "func" → "incantatio"
    { "return", kOperationReturn },       // "return" → "reporto"
    { "call",   kOperationCall },
    { "print",  kOperationWrite },        // "print" → "revelatio"
    { "scanf",  kOperationRead },         // "scanf" → "augurio"
    { "exit",  kOperationHLT},

    { "<",      kOperationB },
    { "<=",     kOperationBE },
    { ">",      kOperationA },
    { ">=",     kOperationAE },
    { "==",     kOperationE },            // "==" → "aequalis"
    { "!=",     kOperationNE },

    { ";",      kOperationThen },
    { ",",      kOperationComma },

    { "NULL",   kOperationNone },
};

static const size_t OP_TABLE_SIZE = sizeof(OP_TABLE) / sizeof(OP_TABLE[0]);

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

    if (node->type == kOperation &&
        node->value.operation == kOperationFunction) {

        LangNode_t *func_name = node->left;
        LangNode_t *args_root = node->right ? node->right->left  : NULL;
        LangNode_t *body_root = node->right ? node->right->right : NULL;

        int argc = CountArgs(args_root);

        arr->var_array[func_name->value.pos].variable_value = argc;

        ScanInitsInSubtree(func_name, body_root, arr);
    }
}


static DifErrors CheckType(Lang_t title, LangNode_t *node, VariableArr *arr) {
    assert(node);
    assert(arr);
    
    for (size_t i = 0; i < OP_TABLE_SIZE; i++) {
        if (strcmp(OP_TABLE[i].name, title) == 0) {
            node->type = kOperation;
            node->value.operation = OP_TABLE[i].type;
            return kSuccess;
        }
    }
    //printf("%s\n\n", title);

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
    arr->var_array[arr->size].func_made = NULL;

    node->value.pos = arr->size;
    arr->size++;

    return kSuccess;
}

static int CountArgs(LangNode_t *args_root) {
    if (!args_root) return 0;

    if (args_root->type == kOperation &&
        args_root->value.operation == kOperationComma) {
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

    if (root->type == kOperation && root->value.operation == kOperationIs &&
        root->left && root->left->type == kVariable) {
        
        RegisterInit(func_name_node, root->left, arr);
    }

    ScanInitsInSubtree(func_name_node, root->left, arr);
    ScanInitsInSubtree(func_name_node, root->right, arr);
}

static void GenExpr(LangNode_t *node, FILE *out, VariableArr *arr);
static void GenThenChain(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenIf(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenWhile(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenFunction(LangNode_t *node, FILE *out, VariableArr *arr, int indent);

static void PrintIndent(FILE *out, int indent) {
    for (int i = 0; i < indent; ++i)
        fputc('\t', out);
}

void GenerateCodeFromAST(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
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

            case kOperationReturn:
                PrintIndent(out, indent);
                fprintf(out, "%s ", RETURN);
                if (node->left)
                    GenExpr(node->left, out, arr);

                fprintf(out, "%s\n", THEN);
                return;

            case kOperationWrite:
                PrintIndent(out, indent);
                fprintf(out, "%s(", PRINT);
                GenExpr(node->left, out, arr);
                fprintf(out, ")");
                fprintf(out, "%s\n", THEN);
                return;

            case kOperationRead:
                PrintIndent(out, indent);
                fprintf(out, "%s(", SCANF);
                GenExpr(node->left, out, arr);
                fprintf(out, ")");
                fprintf(out, "%s\n", THEN);
                return;

            default:
                break;
        }
    }

    PrintIndent(out, indent);
    GenExpr(node, out, arr);
    fprintf(out, "%s\n", THEN);
}

static void GenThenChain(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(out);
    assert(arr);

    LangNode_t *stmt = node;

    while (stmt &&
           stmt->type == kOperation &&
           stmt->value.operation == kOperationThen) {

        if (stmt->left) {
            GenerateCodeFromAST(stmt->left, out, arr, indent);
        }

        stmt = stmt->right;
    }

    if (stmt) {
        GenerateCodeFromAST(stmt, out, arr, indent);
    }
}

static void GenIf(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(out);
    assert(arr);
    assert(node);

    LangNode_t *condition = node->left;
    LangNode_t *body      = node->right;

    LangNode_t *then_branch = NULL;
    LangNode_t *else_branch = NULL;

    if (body && body->type == kOperation && body->value.operation == kOperationElse) {
        then_branch = body->left;
        else_branch = body->right;
    } else {
        then_branch = body;
    }

    PrintIndent(out, indent);
    fprintf(out, "%s (", IF);
    GenExpr(condition, out, arr);
    fprintf(out, ") %s\n", BRACEOP);

    if (then_branch)
        GenThenChain(then_branch, out, arr, indent + 1);

    PrintIndent(out, indent);
    fprintf(out, "%s", BRACECL);

    if (else_branch) {
        fprintf(out, " %s %s\n", ELSE, BRACEOP);
        GenThenChain(else_branch, out, arr, indent + 1);
        PrintIndent(out, indent);
        fprintf(out, "%s", BRACECL);
    }

    fprintf(out, "\n");
}

static void GenWhile(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s (", WHILE);
    GenExpr(node->left, out, arr);
    fprintf(out, ") %s\n", BRACEOP);

    if (node->right)
        GenThenChain(node->right, out, arr, indent + 1);

    PrintIndent(out, indent);
    fprintf(out, "%s\n", BRACECL);
}

static void GenFunction(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    LangNode_t *name = node->left;
    LangNode_t *pair = node->right;

    LangNode_t *args = NULL;
    LangNode_t *body = NULL;
    fprintf(stderr, "A\n");

    if (pair &&
        pair->type == kOperation &&
        pair->value.operation == kOperationThen) {
        args = pair->left;
        body = pair->right;
    } else {
        body = pair;
    }

    PrintIndent(out, indent);
    fprintf(out, "%s ", DECLARE);
    if (name)
        GenExpr(name, out, arr);

    fprintf(out, "(");
    if (args)
        GenExpr(args, out, arr);
    fprintf(out, ") %s\n", BRACEOP);

    if (body)
        GenThenChain(body, out, arr, indent + 1);

    PrintIndent(out, indent);
    fprintf(out, "%s\n", BRACECL);
}

static int GetOpPrecedence(OperationTypes op) {
    switch (op) {
        case kOperationPow:     return 4;
        case kOperationMul:     return 3;
        case kOperationDiv:     return 3;
        case kOperationAdd:     return 2;
        case kOperationSub:     return 2;
        case kOperationB:       return 1;
        case kOperationBE:      return 1;
        case kOperationA:       return 1;
        case kOperationAE:      return 1;
        case kOperationE:       return 1;
        case kOperationNE:      return 1;
        default:                return 0;
    }
}

static void GenExpr(LangNode_t *node, FILE *out, VariableArr *arr) {
    if (!node) return;
    assert(out);
    assert(arr);

    switch (node->type) {
        case kNumber:
            fprintf(out, "%g", node->value.number);
            return;

        case kVariable:
            fprintf(out, "%s", arr->var_array[node->value.pos].variable_name);
            return;

        case kOperation:
            break;

        default:
            fprintf(out, "UNKNOWN");
            return;
    }

    switch (node->value.operation) {

        case kOperationAdd:
        case kOperationSub:
        case kOperationMul:
        case kOperationDiv:
        case kOperationPow: {
            const char *op = "?";
            if (node->value.operation == kOperationAdd) op = ADD;
            else if (node->value.operation == kOperationSub) op = SUB;
            else if (node->value.operation == kOperationMul) op = MUL;
            else if (node->value.operation == kOperationDiv) op = DIV;
            else if (node->value.operation == kOperationPow) op = "^";

            if (node->left && node->left->type == kOperation &&
                GetOpPrecedence(node->left->value.operation) < GetOpPrecedence(node->value.operation)) {
                fprintf(out, "(");
                GenExpr(node->left, out, arr);
                fprintf(out, ")");
                
            } else {
                GenExpr(node->left, out, arr);
            }
            fprintf(out, " %s ", op);
            GenExpr(node->right, out, arr);
            return;
        }

        case kOperationB:
        case kOperationBE:
        case kOperationA:
        case kOperationAE:
        case kOperationE:
        case kOperationNE: {
            const char *op = "?";
            if      (node->value.operation == kOperationB)  op = B;
            else if (node->value.operation == kOperationBE) op = BEQ;
            else if (node->value.operation == kOperationA)  op = A;
            else if (node->value.operation == kOperationAE) op = AE;
            else if (node->value.operation == kOperationE)  op = EQUAL;
            else if (node->value.operation == kOperationNE) op = "!=";

            //fprintf(out, "(");
            GenExpr(node->left, out, arr);
            fprintf(out, " %s ", op);
            GenExpr(node->right, out, arr);
            //fprintf(out, ")");
            return;
        }


        case kOperationIs:
            GenExpr(node->left, out, arr);
            fprintf(out, " %s ", IS);
            GenExpr(node->right, out, arr);
            return;


        case kOperationCall:
            GenExpr(node->left, out, arr);
            GenExpr(node->left, stdout, arr);
            fprintf(out, "(");
            if (node->right)
                GenExpr(node->right, out, arr);
            fprintf(out, ")");
            return;

        case kOperationSin:
        case kOperationCos:
        case kOperationTg:
        case kOperationLn:
        case kOperationArctg:
        case kOperationSinh:
        case kOperationCosh:
        case kOperationTgh:
        case kOperationSQRT: {
            const char *name = "func";
            if      (node->value.operation == kOperationSin)   name = "sin";
            else if (node->value.operation == kOperationCos)   name = "cos";
            else if (node->value.operation == kOperationTg)    name = "tg";
            else if (node->value.operation == kOperationLn)    name = "ln";
            else if (node->value.operation == kOperationArctg) name = "arctg";
            else if (node->value.operation == kOperationSinh)  name = "sinh";
            else if (node->value.operation == kOperationCosh)  name = "cosh";
            else if (node->value.operation == kOperationTgh)   name = "tgh";
            else if (node->value.operation == kOperationSQRT)  name = "sqrt";

            fprintf(out, "%s(", name);
            GenExpr(node->left, out, arr);
            fprintf(out, ")");
            return;
        }

        default:
            fprintf(out, "UNSUPPORTED_OP");
            return;
    }
}


static DifErrors ParseMaybeNil(const char *buffer, size_t *pos, LangNode_t **out) {
    assert(buffer);
    assert(pos);
    assert(out);

    SkipSpaces(buffer, pos);
    //printf("DEBUG: nil node\n");

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

