#include "Rules.h"

#include "Enums.h"
#include "Structs.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "Enums.h"
#include "Structs.h"
#include "LanguageFunctions.h"
#include "StackFunctions.h"
#include "DoGraph.h"
#include "LexicalAnalysis.h"

static long long SizeOfFile(const char *filename);
static char *ReadToBuf(const char *filename, FILE *file, size_t filesize);
static void DoBufRead(FILE *file, const char *filename, FileInfo *Info);

#define CHECK_NULL_RETURN(name, cond) \
    DifNode_t *name = cond;           \
    if (name == NULL) {               \
        return NULL;                  \
    }

static OpEntry operations[] = {
    {"tg",    kOperationTg},
    {"sin",   kOperationSin},
    {"cos",   kOperationCos},
    {"ln",    kOperationLn},
    {"arctg", kOperationArctg},
    {"pow",   kOperationPow},
    {"sh",    kOperationSinh},
    {"ch",    kOperationCosh},
    {"th",    kOperationTgh},
};
size_t max_op_size = 5; //

DifNode_t *GetGoal(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetExpression(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetTerm(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetPrimary(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetPower(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetAssignment(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetOp(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);

static DifNode_t *GetNumber(DifRoot *root, Stack_Info *tokens, size_t *tokens_pos);
static DifNode_t *GetString(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
// static DifNode_t *GetOperation(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *position, char *buffer, size_t *tokens_pos);
// static OperationTypes ParseOperator(Stack_Info *tokens, size_t *tokens_pos);

DifErrors ReadInfix(DifRoot *root, DumpInfo *dump_info, VariableArr *Variable_Array, const char *filename, FILE *texfile) {
    assert(root);
    assert(dump_info);
    assert(Variable_Array);
    assert(filename);
    assert(texfile);

    FILE_OPEN_AND_CHECK(file, filename, "r");

    FileInfo Info = {};
    DoBufRead(file, filename, &Info);
    printf("%s", Info.buf_ptr);

    fclose(file);

    Stack_Info tokens = {};
    StackCtor(&tokens, 1, stderr);
    CheckAndReturn(root, (const char **)&Info.buf_ptr, &tokens, Variable_Array);

    size_t pos = 0;
    size_t tokens_pos = 0;
    // const char *new_string = Info.buf_ptr;
    root->root = GetGoal(root, &tokens, Variable_Array, &pos, &tokens_pos);
    if (!root->root) {
        return kFailure;
    }
    
    dump_info->tree = root;

    strcpy(dump_info->message, "Expression read with infix form");
    DoTreeInGraphviz(root->root, dump_info, root->root);
    StackDtor(&tokens, stderr);

    //PrintFirstExpression(texfile, root->root);
    //DoDump(dump_info);

    return kSuccess;
}

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL, arr)
#define NEWOP(op, left, right) NewNode(root, kOperation, (Value){ .operation = (op) }, left, right, arr) 

static bool StatementNeedsSemicolon(const DifNode_t *node) {
    if (!node) return false;

    if (node->type == kOperation) {
        switch (node->value.operation) {
            case kOperationIf:
            case kOperationWhile:
            case kOperationBraceOpen:
            case kOperationBraceClose:
                return false;
            default:
                return true;
        }
    }

    return true;
}


DifNode_t *GetGoal(DifRoot *root, Stack_Info *tokens, VariableArr *arr,
    size_t *pos, size_t *tokens_pos) {
    assert(root && tokens && arr && pos && tokens_pos);

    DifNode_t *first = GetOp(root, tokens, arr, pos, tokens_pos);
    if (!first) return NULL;

    DifNode_t *last = first;

    while (true) {
        size_t save_pos = *tokens_pos;
        DifNode_t *next = GetOp(root, tokens, arr, pos, tokens_pos);
        if (!next) break;

        if (last->type == kOperation && last->value.operation == kOperationThen) {
            last->right = next;
            next->parent = last;
        } else {
            DifNode_t *then_node = NEWOP(kOperationThen, last, next);
            last = then_node;
        }
    }

    return last;
}

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL, arr)
#define ADD_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationAdd}, left, right, arr)
#define SUB_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationSub}, left, right, arr)
#define MUL_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationMul}, left, right, arr)
#define DIV_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationDiv}, left, right, arr)
#define POW_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationPow}, left, right, arr)

static DifNode_t *GetExpression(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);

    DifNode_t *val = GetTerm(root, tokens, arr, pos, tokens_pos);
    if (!val) return NULL;

    root->size++;

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);

    while (node &&  node->type == kOperation &&
        (node->value.operation == kOperationAdd ||
        node->value.operation == kOperationSub)) {
        DifNode_t *op_node = node;

        (*tokens_pos)++;

        DifNode_t *val2 = GetTerm(root, tokens, arr, pos, tokens_pos);
        if (!val2) return val;

        root->size++;

        op_node->left  = val;
        op_node->right = val2;

        val->parent  = op_node;
        val2->parent = op_node;

        val = op_node;

        node = GetStackElem(tokens, *tokens_pos);
    }

    fprintf(stderr, "expression\n");
    return val;
}


static DifNode_t *GetTerm(DifRoot *root, Stack_Info *tokens, VariableArr *arr,
                          size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);

    DifNode_t *left = GetPower(root, tokens, arr, pos, tokens_pos);
    if (!left) return NULL;

    root->size++;

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);

    while (node && node->type == kOperation &&
          (node->value.operation == kOperationMul || node->value.operation == kOperationDiv)) {
        (*tokens_pos)++;

        DifNode_t *right = GetTerm(root, tokens, arr, pos, tokens_pos);
        if (!right) return left;

        root->size++;

        node->left  = left;
        node->right = right;
        left->parent  = node;
        right->parent = node;

        left = node;

        node = GetStackElem(tokens, *tokens_pos);
    }

    return left;
}


static DifNode_t *GetPrimary(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    if (!node) {
        return NULL;
    }

    if (node->type == kOperation && node->value.operation == kOperationParOpen) {
        (*tokens_pos)++;

        DifNode_t *val = GetExpression(root, tokens, arr, pos, tokens_pos);
        if (!val) {
            return NULL;
        }

        node = GetStackElem(tokens, *tokens_pos);
        if (node && node->type == kOperation && node->value.operation == kOperationParClose) {
            (*tokens_pos)++;
        } else {
            fprintf(stderr, "SYNTAX_ERROR_P: expected ')'\n");
            return NULL;
        }

        return val;
    }

    DifNode_t *value_number = GetNumber(root, tokens, tokens_pos);
    if (value_number) {
        return value_number;
    }

    return GetString(root, tokens, arr, pos, tokens_pos);
}


DifNode_t *GetAssignment(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);

    size_t save_pos = *tokens_pos;
    
    DifNode_t *maybe_var = GetString(root, tokens, arr, pos, tokens_pos);
    if (!maybe_var) {
        *tokens_pos = save_pos;
        return NULL;
    }

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    if (!node || node->type != kOperation || node->value.operation != kOperationIs) {
        *tokens_pos = save_pos;
        return NULL;
    }

    (*tokens_pos)++;

    DifNode_t *value = GetExpression(root, tokens, arr, pos, tokens_pos);
    if (!value) {
        *tokens_pos = save_pos;
        return NULL;
    }

    node->type = kOperation;
    node->value.operation = kOperationIs;
    node->left  = maybe_var;
    node->right = value;
    maybe_var->parent = node;
    value->parent = node;

    return node;
}

static DifNode_t *GetIf(DifRoot *root, Stack_Info *tokens,
                        VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);
    assert(tokens_pos);

    DifNode_t *tok = GetStackElem(tokens, *tokens_pos);
    if (!tok || tok->type != kOperation || tok->value.operation != kOperationIf)
        return NULL;

    (*tokens_pos)++;

    tok = GetStackElem(tokens, *tokens_pos);
    if (!tok || tok->type != kOperation || tok->value.operation != kOperationParOpen) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected '('\n");
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *cond = GetExpression(root, tokens, arr, pos, tokens_pos);
    if (!cond) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected condition\n");
        return NULL;
    }

    tok = GetStackElem(tokens, *tokens_pos);
    if (!tok || tok->type != kOperation || tok->value.operation != kOperationParClose) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected ')'\n");
        return NULL;
    }
    (*tokens_pos)++;

    tok = GetStackElem(tokens, *tokens_pos);
    if (!tok || tok->type != kOperation || tok->value.operation != kOperationBraceOpen) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected '{'\n");
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *first = GetOp(root, tokens, arr, pos, tokens_pos);
    if (!first) {
        fprintf(stderr, "SYNTAX_ERROR_IF: empty if-body\n");
        return NULL;
    }

    //DifNode_t *last = first;

    // tok = GetStackElem(tokens, *tokens_pos);
    // if (!(first->value.operation == kOperationIf || first->value.operation == kOperationWhile)){
    //     if (!tok || tok->type != kOperation || tok->value.operation != kOperationThen) {
    //         fprintf(stderr, "SYNTAX_ERROR_IF: expected ';' after statement\n");
    //         return NULL;
    //     }
    //     (*tokens_pos)++;
        
    // }

    DifNode_t *last = first;

    while (true) {
        size_t save_pos = *tokens_pos;
        DifNode_t *stmt = GetOp(root, tokens, arr, pos, tokens_pos);
        if (!stmt) break;

        // if (StatementNeedsSemicolon(stmt)) {
        //     tok = GetStackElem(tokens, *tokens_pos);
        //     if (!tok || tok->type != kOperation || tok->value.operation != kOperationThen) {
        //         fprintf(stderr, "SYNTAX_ERROR_IF: expected ';' after statement\n");
        //         return NULL;
        //     }
        //     (*tokens_pos)++;
        //     fprintf(stderr, "!");
        // }
        
        last = NEWOP(kOperationThen, last, stmt);
        fprintf(stderr, "%p", last);
    }

    tok = GetStackElem(tokens, *tokens_pos);
    if (!tok || tok->type != kOperation || tok->value.operation != kOperationBraceClose) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected '}'\n");
        return NULL;
    }
    (*tokens_pos)++;
    fprintf(stderr, "AAA%p\n", tokens[*tokens_pos]);

    DifNode_t *if_node = NEWOP(kOperationIf, cond, last);


    return if_node;
}

static DifNode_t *GetWhile(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    if (!node || node->type != kOperation || node->value.operation != kOperationWhile) {
        return NULL;
    }
    (*tokens_pos)++;

    node = GetStackElem(tokens, *tokens_pos);
    if (!node || node->type != kOperation || node->value.operation != kOperationParOpen) {
        fprintf(stderr, "SYNTAX_ERROR_WHILE: expected '(' after while\n");
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *cond = GetExpression(root, tokens, arr, pos, tokens_pos);
    if (!cond) {
        return NULL;
    }

    node = GetStackElem(tokens, *tokens_pos);
    if (!node || node->type != kOperation || node->value.operation != kOperationParClose) {
        fprintf(stderr, "SYNTAX_ERROR_WHILE: expected ')'\n");
        return NULL;
    }
    (*tokens_pos)++;

    node = GetStackElem(tokens, *tokens_pos);
    if (!node || node->type != kOperation || node->value.operation != kOperationBraceOpen) {
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *first = GetOp(root, tokens, arr, pos, tokens_pos);
    if (!first) {
        fprintf(stderr, "SYNTAX_ERROR_WHILE: expected statements in body\n");
        return NULL;
    }
    DifNode_t *last = first;

    node = GetStackElem(tokens, *tokens_pos);
    while (node && node->type == kOperation && node->value.operation == kOperationThen) {
        (*tokens_pos)++;
        DifNode_t *next = GetOp(root, tokens, arr, pos, tokens_pos);
        if (!next) {
            fprintf(stderr, "SYNTAX_ERROR_WHILE: expected statement after ';'\n");
            return NULL;
        }

        DifNode_t *then_node = GetStackElem(tokens, *tokens_pos - 1);
        then_node->type = kOperation;
        then_node->value.operation = kOperationThen;
        then_node->left  = last;
        then_node->right = next;
        last->parent = then_node;
        next->parent = then_node;

        last = then_node;

        node = GetStackElem(tokens, *tokens_pos);
    }

    node = GetStackElem(tokens, *tokens_pos);
    if (!node || node->type != kOperation || node->value.operation != kOperationBraceClose) {
        fprintf(stderr, "SYNTAX_ERROR_WHILE: expected '}'\n");
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *while_node = GetStackElem(tokens, *tokens_pos - 1);
    while_node->type = kOperation;
    while_node->value.operation = kOperationWhile;
    while_node->left = cond;
    while_node->right = last;
    cond->parent = while_node;
    last->parent = while_node;

    return while_node;
}

DifNode_t *GetOp(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);

    size_t save_pos = *tokens_pos;

    DifNode_t *stmt = GetWhile(root, tokens, arr, pos, tokens_pos);
    if (stmt) {
        return stmt;
    }

    *tokens_pos = save_pos;
    stmt = GetIf(root, tokens, arr, pos, tokens_pos);
    if (stmt) {
        return stmt;
    }

    *tokens_pos = save_pos;
    stmt = GetAssignment(root, tokens, arr, pos, tokens_pos);
    if (stmt) {
        DifNode_t *semicolon = GetStackElem(tokens, *tokens_pos);
        if (!semicolon || semicolon->type != kOperation || 
            semicolon->value.operation != kOperationThen) {
            fprintf(stderr, "SYNTAX_ERROR: expected ';' after assignment\n");
            return NULL;
        }
        fprintf(stderr, "OP%p\n", tokens[*tokens_pos]);
        (*tokens_pos)++;
        
        semicolon->left = stmt;
        semicolon->right = NULL;
        stmt->parent = semicolon;
        
        return semicolon;
    }


    *tokens_pos = save_pos;
    return NULL;
}

DifNode_t *GetPower(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);

    DifNode_t *val = GetPrimary(root, tokens, arr, pos, tokens_pos);
    if (!val) {
        return NULL;
    }

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    while (node && node->type == kOperation && node->value.operation == kOperationPow) {
        (*tokens_pos)++;
        DifNode_t *val2 = GetPower(root, tokens, arr, pos, tokens_pos);
        if (!val2) {
            return NULL;
        }

        node->type = kOperation;
        node->value.operation = kOperationPow;
        node->left  = val;
        node->right = val2;
        val->parent = node;
        val2->parent = node;

        val = node;

        node = GetStackElem(tokens, *tokens_pos);
    }

    return val;
}


static DifNode_t *GetNumber(DifRoot *root, Stack_Info *tokens, size_t *tokens_pos) {
    assert(root);
    assert(tokens);

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);

    if (node && node->type == kNumber) {
        (*tokens_pos)++;
        return node;
    }

    return NULL;
}

static DifNode_t *GetString(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    if (node && node->type == kVariable) {
        (*tokens_pos)++;
    } else {
        return NULL;
    }

    fprintf(stderr, "string\n");

    return node;
}


// static DifNode_t *GetOperation(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
//     assert(root);
//     assert(tokens);
//     assert(arr);

//     DifNode_t *node = GetStackElem(tokens, *tokens_pos);
//     if (node->type == kOperation) {
//         (*tokens_pos)++;
//     } else {
//         return NULL;
//     }

//     return NULL;
// }

#undef NEWN
#undef ADD_
#undef SUB_
#undef MUL_
#undef DIV_
#undef POW_

static OperationTypes ParseOperator(const char *string) {
    assert(string);

    size_t op_size = sizeof(operations)/sizeof(operations[0]);

    for (size_t i = 0; i < op_size; i++) {
        if (strncmp(string, operations[i].name, strlen(operations[i].name)) == 0) { //
            return operations[i].type;
        }
    }

    return kOperationNone;
}

static long long SizeOfFile(const char *filename) {
    assert(filename);

    struct stat stbuf = {};

    int err = stat(filename, &stbuf);
    if (err != kSuccess) {
        perror("stat() failed");
        return kErrorStat;
    }

    return stbuf.st_size;
}

static char *ReadToBuf(const char *filename, FILE *file, size_t filesize) {
    assert(filename);
    assert(file);

    char *buf_in = (char *) calloc (filesize + 2, sizeof(char));
    if (!buf_in) {
        fprintf(stderr, "ERROR while calloc.\n");
        return NULL;
    }

    size_t bytes_read = fread(buf_in, 1, filesize, file);

    char *buf_out = (char *) calloc (bytes_read + 1, 1);
    if (!buf_out) {
        fprintf(stderr, "ERROR while calloc buf_out.\n");
        free(buf_in);
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < bytes_read; i++) {
        if (buf_in[i] != '\n' && buf_in[i] != ' ') {
            buf_out[j++] = buf_in[i];
        }
    }

    buf_out[j] = '\0';

    free(buf_in);

    return buf_out;
}

static void DoBufRead(FILE *file, const char *filename, FileInfo *Info) {
    assert(file);
    assert(filename);
    assert(Info);

    Info->filesize = (size_t)SizeOfFile(filename) * 4;

    Info->buf_ptr = ReadToBuf(filename, file, Info->filesize);
    assert(Info->buf_ptr != NULL);
}