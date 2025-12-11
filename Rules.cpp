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
//static void DoBufRead(FILE *file, const char *filename, FileInfo *Info);

#define CHECK_NULL_RETURN(name, cond) \
    DifNode_t *name = cond;           \
    if (name == NULL) {               \
        return NULL;                  \
    }

DifNode_t *GetGoal(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetExpression(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetTerm(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetPrimary(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetPower(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetAssignment(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetOp(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetWhile(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetIf(DifRoot *root, Stack_Info *tokens,
                        VariableArr *arr, size_t *pos, size_t *tokens_pos);
static DifNode_t *GetElse(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos, DifNode_t *if_node);
static DifNode_t *GetFunction(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos);

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
    DoTreeInGraphviz(root->root, dump_info, Variable_Array);
    StackDtor(&tokens, stderr);

    //PrintFirstExpression(texfile, root->root);
    //DoDump(dump_info);

    return kSuccess;
}

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define NEWOP(op, left, right) NewNode(root, kOperation, (Value){ .operation = (op) }, left, right) 


/* G :: = OP+
   OP :: = WHILE | IF | ASSIGNMENT; | FUNCTION
   WHILE :: = 'while' ( E ) { OP+ }
   IF :: = 'if' ( E ) { OP+ }
   ASSIGNMENT :: = V '=' E
   FUNCTION :: = V (T {, T}+) (; | { OP+})
   E :: = T ([+-] T)*
   T :: = POWER ([*:] T)*
   POWER :: = PRIMARY ([^] POWER)*
   PRIMARY :: = ( E ) | N | S
*/

DifNode_t *GetGoal(DifRoot *root, Stack_Info *tokens, VariableArr *arr,
    size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);
    assert(tokens_pos);

    DifNode_t *first = NULL;
    do {
        DifNode_t *next = GetOp(root, tokens, arr, pos, tokens_pos);
        if (!next) break;
        else if (first && !first->right) {
            first->right = next;
        } else {
            first = NEWOP(kOperationThen, next, first);
            printf("BB%p\n", first);
        }
    } while (true);

    return first;
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
    stmt = GetFunction(root, tokens, arr, pos, tokens_pos);
    if (stmt) {
        return stmt;
    }

    DifNode_t *last = NULL;
    *tokens_pos = save_pos;

    do {
        save_pos = *tokens_pos;
        stmt = GetAssignment(root, tokens, arr, pos, tokens_pos);
        if (!stmt) {
            *tokens_pos = save_pos;
            break;
        }

        DifNode_t *token = GetStackElem(tokens, *tokens_pos);
        if (token && token->type == kOperation && token->value.operation == kOperationThen) {
            (*tokens_pos)++;
            if (!last) {
                last = stmt;
            } else if (last && !last->right) {
                last->right = stmt;
                //last = token;
            } else {
                printf("AA%p\n", last);
                token->left = last;
                token->right = stmt;
                stmt->parent = token;
                last = token;
            }
        }
        //printf("STMT%p\n", stmt);

    } while (true);
    //printf("hm%zu\n", *tokens_pos);
    return last;
}

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define ADD_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationAdd}, left, right)
#define SUB_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationSub}, left, right)
#define MUL_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationMul}, left, right)
#define DIV_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationDiv}, left, right)
#define POW_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationPow}, left, right)

static DifNode_t *GetFunction(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);
    assert(tokens_pos);

    size_t save_pos = *tokens_pos;

    DifNode_t *name_node = GetStackElem(tokens, *tokens_pos);
    if (!name_node || name_node->type != kVariable) {
        *tokens_pos = save_pos;
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *token = GetStackElem(tokens, *tokens_pos);
    if (!token || token->type != kOperation || token->value.operation != kOperationParOpen) {
        *tokens_pos = save_pos;
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *args_root = NULL;
    DifNode_t *rightmost = NULL; 
    while (true) {
        token = GetStackElem(tokens, *tokens_pos);
        if (!token) {
            fprintf(stderr, "SYNTAX_ERROR: unexpected end of tokens in function args\n");
            *tokens_pos = save_pos;
            return NULL;
        }

        if (token->type == kOperation && token->value.operation == kOperationParClose) {
            (*tokens_pos)++;
            break;
        }

        if (token->type == kOperation && token->value.operation == kOperationComma) {
            (*tokens_pos)++;
            continue;
        }

        DifNode_t *arg_node = NULL;

        if (token->type == kVariable || token->type == kNumber) {
            arg_node = token;
            (*tokens_pos)++;
        } //

        if (!args_root) {
            args_root = arg_node;
        }
        else if (!rightmost) {
            args_root = NEWOP(kOperationComma, args_root, arg_node);
            rightmost = args_root;
        }   else {
            DifNode_t *comma = NEWOP(kOperationComma, rightmost->right, arg_node);
            rightmost->right = comma;
            rightmost = comma;
        }
    }

    token = GetStackElem(tokens, *tokens_pos);
    if (token && token->type == kOperation && token->value.operation == kOperationBraceOpen) {
        (*tokens_pos)++;

        DifNode_t *body_root = NULL;
        DifNode_t *last_stmt = NULL;

        while (true) {
            DifNode_t *stmt = GetOp(root, tokens, arr, pos, tokens_pos);
            if (!stmt) break;

            if (!body_root) {
                body_root = stmt;
            } else {
                last_stmt->right = stmt;
                stmt->parent = last_stmt;
            }
            last_stmt = stmt;
        }

        token = GetStackElem(tokens, *tokens_pos);
        if (!token || token->type != kOperation || token->value.operation != kOperationBraceClose) {
            fprintf(stderr, "SYNTAX_ERROR: expected '}' at end of function body\n");
            *tokens_pos = save_pos;
            return NULL;
        }
        (*tokens_pos)++;

        name_node->type = kOperation;
        name_node->value.operation = kFunction;
        name_node->left = args_root;
        name_node->right = body_root;
    } else {
        name_node->type = kOperation;
        name_node->value.operation = kOperationCall;
        name_node->left = args_root;
        name_node->right = NULL;
    }

    return name_node;
}

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

    return NEWOP(kOperationThen, node, NULL);
}

static DifNode_t *GetIf(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos) {
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

    DifNode_t *last = first;

    while (true) {
        DifNode_t *stmt = GetOp(root, tokens, arr, pos, tokens_pos);
        if (!stmt) break;
        
        last = NEWOP(kOperationThen, last, stmt);
        fprintf(stderr, "%p", last);
    }

    tok = GetStackElem(tokens, *tokens_pos);
    if (!tok || tok->type != kOperation || tok->value.operation != kOperationBraceClose) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected '}'\n");
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *if_node = NEWOP(kOperationIf, cond, last);

    DifNode_t *else_node = GetElse(root, tokens, arr, pos, tokens_pos, last);
    if (else_node) {
        if_node->right = else_node;
        else_node->parent = if_node;
    }
    return if_node;
}

static DifNode_t *GetElse(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *pos, size_t *tokens_pos, DifNode_t *if_node) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(pos);
    assert(if_node);

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    if (!(node && node->type == kOperation && node->value.operation == kOperationElse)) {
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *tok = GetStackElem(tokens, *tokens_pos);
    if (!tok || tok->type != kOperation || tok->value.operation != kOperationBraceOpen) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected '{'\n");
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *last = NULL;

    while (true) {
        DifNode_t *stmt = GetOp(root, tokens, arr, pos, tokens_pos);
        if (!stmt) break;
        
        if (last) last = NEWOP(kOperationThen, last, stmt);
        else {
            last = stmt;
        }
        //fprintf(stderr, "else%p\n", last);
    }

    tok = GetStackElem(tokens, *tokens_pos);
    if (!tok || tok->type != kOperation || tok->value.operation != kOperationBraceClose) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected '}'\n");
        return NULL;
    }
    (*tokens_pos)++;

    return NEWOP(kOperationElse, if_node, last);

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


#undef NEWN
#undef ADD_
#undef SUB_
#undef MUL_
#undef DIV_
#undef POW_

// static OperationTypes ParseOperator(const char *string) {
//     assert(string);

//     size_t op_size = sizeof(operations)/sizeof(operations[0]);

//     for (size_t i = 0; i < op_size; i++) {
//         if (strncmp(string, operations[i].name, strlen(operations[i].name)) == 0) { //
//             return operations[i].type;
//         }
//     }

//     return kOperationNone;
// }

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

void DoBufRead(FILE *file, const char *filename, FileInfo *Info) {
    assert(file);
    assert(filename);
    assert(Info);

    Info->filesize = (size_t)SizeOfFile(filename) * 4;

    Info->buf_ptr = ReadToBuf(filename, file, Info->filesize);
    assert(Info->buf_ptr != NULL);
}