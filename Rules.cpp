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

static bool IsThatOperation(DifNode_t *node, OperationTypes type);
static DifNode_t *ParseFunctionArgs(DifRoot *root, Stack_Info *tokens, size_t *tokens_pos);
static DifNode_t *ParseFunctionBody(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *ParseStatementSequence(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);

#define CHECK_NULL_RETURN(name, cond) \
    DifNode_t *name = cond;           \
    if (name == NULL) {               \
        return NULL;                  \
    }

#define TRY_PARSE_RETURN(node_var, func_call)   \
    do {                                        \
        (node_var) = (func_call);               \
        if (node_var) {                         \
            return (node_var);                  \
        }                                       \
        *tokens_pos = save_pos;                 \
    } while (0)

#define CHECK_EXPECTED_TOKEN(out_tok, TRUE_COND, error_handler) \
    do {                                                        \
        (out_tok) = GetStackElem((tokens), *(tokens_pos));      \
        if (!(TRUE_COND)) {                                     \
            error_handler;                                      \
            *(tokens_pos) = (save_pos);                         \
            return NULL;                                        \
        }                                                       \
        (*(tokens_pos))++;                                      \
    } while (0)

// TODO: 1. перевод из дерева в ассеблер
// TODO: 2. придумать хранение всего в коде процессора
// TODO: 3. решить до конца с функциями 
// TODO: 4. сделать тернарные операторы
// TODO: 5. делать ли для функций новый тип данных
// TODO: 6. убрать указатель на таблицу меток в структуру узла
// TODO: 8. убрать op+ и сделать просто op
// TODO: 9. разобраться с makefile

static DifNode_t *GetGoal(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);

static DifNode_t *GetAssignment(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetOp(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetWhile(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetIf(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetElse(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos, DifNode_t *if_node);
static DifNode_t *GetFunctionDeclare(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetFunctionCall(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetPrintf(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);

static DifNode_t *GetExpression(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetTerm(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetPrimary(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetPower(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);

static DifNode_t *GetNumber(DifRoot *root, Stack_Info *tokens, size_t *tokens_pos);
static DifNode_t *GetString(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);

DifErrors ReadInfix(DifRoot *root, DumpInfo *dump_info, VariableArr *Variable_Array, const char *filename) {
    assert(root);
    assert(dump_info);
    assert(Variable_Array);
    assert(filename);

    FILE_OPEN_AND_CHECK(file, filename, "r");

    FileInfo Info = {};
    DoBufRead(file, filename, &Info);
    fclose(file);

    Stack_Info tokens = {};
    StackCtor(&tokens, 1, stderr);
    CheckAndReturn(root, (const char **)&Info.buf_ptr, &tokens, Variable_Array);

    size_t tokens_pos = 0;

    root->root = GetGoal(root, &tokens, Variable_Array, &tokens_pos);
    if (!root->root) {
        return kFailure;
    }
    
    dump_info->tree = root;

    strcpy(dump_info->message, "Code");
    DoTreeInGraphviz(root->root, dump_info, Variable_Array);
    StackDtor(&tokens, stderr);

    return kSuccess;
}

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define NEWOP(op, left, right) NewNode(root, kOperation, (Value){ .operation = (op) }, left, right) 

/* G :: = FUNCTION_D+
   OP :: = WHILE | IF | ASSIGNMENT+; | FUNCTION_C
   WHILE :: = 'while' ( E ) { OP+ }
   IF :: = 'if' ( E ) { OP+ }
   ASSIGNMENT :: = V '=' E
   FUNCTION_D :: = "declare" V (T {, T}+) { OP+}
   FUNCTION_C :: = V (T {, T}+);
   E :: = T ([+-] T)*
   T :: = POWER ([*:] T)*
   POWER :: = PRIMARY ([^] POWER)*
   PRIMARY :: = ( E ) | N | S
*/

static DifNode_t *GetGoal(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    int cnt = 0;
    DifNode_t *first = NULL;
    do {
        DifNode_t *next = GetFunctionDeclare(root, tokens, arr, tokens_pos);
        if (!next) break;
        else if (first && !first->right) {
            first->right = next;
        } else {
            first = (cnt % 2 == 0) ? NEWOP(kOperationThen, next, first) : NEWOP(kOperationThen, first, next);
            cnt++;
        }
    } while (true);

    return first;
}

DifNode_t *GetOp(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *stmt = NULL;
    size_t save_pos = *tokens_pos;

    TRY_PARSE_RETURN(stmt, GetPrintf(root, tokens, arr, tokens_pos));
    TRY_PARSE_RETURN(stmt, GetWhile(root, tokens, arr, tokens_pos));
    TRY_PARSE_RETURN(stmt, GetIf(root, tokens, arr, tokens_pos));
    TRY_PARSE_RETURN(stmt, GetFunctionCall(root, tokens, arr, tokens_pos));

    DifNode_t *last = NULL;

    do { // сделать по-другому
        save_pos = *tokens_pos;
        stmt = GetAssignment(root, tokens, arr, tokens_pos);
        if (!stmt) {
            *tokens_pos = save_pos;
            break;
        }

        DifNode_t *token = GetStackElem(tokens, *tokens_pos);
        if (IsThatOperation(token, kOperationThen)) {
            (*tokens_pos)++;
            if (!last) {
                last = stmt;
            } else if (last && !last->right) {
                last->right = stmt;
            } else {
                token->left = last;
                token->right = stmt;
                stmt->parent = token;
                last = token;
            }
        }

    } while (true);

    return last;
}

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define ADD_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationAdd}, left, right)
#define SUB_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationSub}, left, right)
#define MUL_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationMul}, left, right)
#define DIV_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationDiv}, left, right)
#define POW_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationPow}, left, right)

static DifNode_t *GetPrintf(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) { //
    assert(root);
    assert(tokens);
    assert(arr);

    DifNode_t *print_f = GetStackElem(tokens, *tokens_pos);
    if (IsThatOperation(print_f, kOperationWrite)) {
        (*tokens_pos)++;

        size_t save_pos = *tokens_pos;
        DifNode_t *par = NULL;
        CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParOpen),);

        DifNode_t *printf_arg = GetStackElem(tokens, *tokens_pos);
        if (!(printf_arg && (printf_arg->type == kVariable || printf_arg->type == kNumber))) { // сделать и для функций
            fprintf(stderr, "NO AVAILABLE ARGUMENT FOR PRINTF WRITTEN.\n");
            return NULL; //
        }
        (*tokens_pos)++;

        CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParClose), );

        print_f->left = printf_arg;
        printf_arg->parent = print_f;
        (*tokens_pos)++;
        return print_f;
    } else {
        return NULL;
    }
}

static DifNode_t *GetFunctionDeclare(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);
    
    size_t save_pos = *tokens_pos;
    DifNode_t *token = NULL, *func_name = NULL;
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationFunction), );
    CHECK_EXPECTED_TOKEN(func_name, func_name && func_name->type == kVariable, );
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationParOpen), );
    
    DifNode_t *args_root = ParseFunctionArgs(root, tokens, tokens_pos);
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR: expected '{' after function declaration\n"));
    
    DifNode_t *body_root = ParseFunctionBody(root, tokens, arr, tokens_pos);
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceClose),
        fprintf(stderr, "%s", "SYNTAX_ERROR: expected '}' at end of function body.\n"));
    
    return NEWOP(kOperationFunction, func_name, NEWOP(kOperationThen, args_root, body_root));
}

static DifNode_t *GetFunctionCall(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    size_t save_pos = *tokens_pos;
    DifNode_t *token = NULL;
    DifNode_t *name_token = NULL;

    CHECK_EXPECTED_TOKEN(name_token, 
        name_token && name_token->type == kVariable,);

    CHECK_EXPECTED_TOKEN(token, 
        token && token->type == kOperation && token->value.operation == kOperationParOpen,);

    DifNode_t *args_root = NULL;
    DifNode_t *rightmost = NULL;

    while (true) {
        CHECK_EXPECTED_TOKEN(token, token, fprintf(stderr, "%s", "SYNTAX_ERROR: unexpected end of tokens in call args.\n"));
        (*tokens_pos)--;

        if (token->type == kOperation && token->value.operation == kOperationParClose) {
            (*tokens_pos)++;
            break;
        }

        if (token->type == kOperation && token->value.operation == kOperationComma) {
            (*tokens_pos)++;
            continue;
        }

        DifNode_t *arg = NULL;
        if (token->type == kVariable || token->type == kNumber) {
            arg = token;
            (*tokens_pos)++;
        }

        if (!args_root) {
            args_root = arg;
        } else if (!rightmost) {
            args_root = NEWOP(kOperationComma, args_root, arg);
            rightmost = args_root;
        } else {
            DifNode_t *comma = NEWOP(kOperationComma, rightmost->right, arg);
            rightmost->right = comma;
            rightmost = comma;
        }
    }
    (*tokens_pos)++;
    return NEWOP(kOperationCall, name_token, args_root);
}

static DifNode_t *GetExpression(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *val = GetTerm(root, tokens, arr, tokens_pos);
    if (!val) return NULL;

    root->size++;
    DifNode_t *node = GetStackElem(tokens, *tokens_pos);

    while (IsThatOperation(node, kOperationAdd) || IsThatOperation(node, kOperationSub)) {
        (*tokens_pos)++;

        DifNode_t *val2 = GetTerm(root, tokens, arr, tokens_pos);
        if (!val2) return val;

        root->size++;
        node->left  = val;
        node->right = val2;
        val->parent  = node;
        val2->parent = node;
        val = node;

        node = GetStackElem(tokens, *tokens_pos);
    }

    return val;
}

static DifNode_t *GetTerm(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *left = GetPower(root, tokens, arr, tokens_pos);
    if (!left) return NULL;

    root->size++;
    DifNode_t *node = GetStackElem(tokens, *tokens_pos);

    while (IsThatOperation(node, kOperationMul) || IsThatOperation(node, kOperationDiv)) {
        (*tokens_pos)++;

        DifNode_t *right = GetTerm(root, tokens, arr, tokens_pos);
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

static DifNode_t *GetPrimary(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    if (!node) {
        return NULL;
    }

    if (IsThatOperation(node, kOperationParOpen)) {
        (*tokens_pos)++;

        DifNode_t *val = GetExpression(root, tokens, arr, tokens_pos);
        if (!val) {
            return NULL;
        }

        node = GetStackElem(tokens, *tokens_pos);
        if (IsThatOperation(node, kOperationParClose)) {
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

    return GetString(root, tokens, arr, tokens_pos);
}


DifNode_t *GetAssignment(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    size_t save_pos = *tokens_pos;
    
    DifNode_t *maybe_var = GetString(root, tokens, arr, tokens_pos);
    if (!maybe_var) {
        *tokens_pos = save_pos;
        return NULL;
    }

    DifNode_t *node = NULL;
    CHECK_EXPECTED_TOKEN(node, 
        node && node->type == kOperation && node->value.operation == kOperationIs, );

    DifNode_t *value = GetExpression(root, tokens, arr, tokens_pos);
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

    printf("%d\n", *tokens_pos);
    return NEWOP(kOperationThen, node, NULL); //
}

static DifNode_t *GetIf(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    size_t save_pos = *tokens_pos;
    DifNode_t *tok = NULL;

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationIf),);
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected '('\n"));
    
    DifNode_t *cond = GetExpression(root, tokens, arr, tokens_pos);
    if (!cond) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected condition\n");
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParClose),
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected ')'\n"));
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceOpen), 
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected '{'\n"));

    DifNode_t *first = GetOp(root, tokens, arr, tokens_pos);
    if (!first) {
        fprintf(stderr, "SYNTAX_ERROR_IF: empty if-body\n");
        return NULL;
    }

    DifNode_t *last = first;

    while (true) {
        DifNode_t *stmt = GetOp(root, tokens, arr, tokens_pos);
        if (!stmt) break;
        
        last = NEWOP(kOperationThen, last, stmt);
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceClose), 
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected '}'\n"));

    DifNode_t *if_node = NEWOP(kOperationIf, cond, last);

    DifNode_t *else_node = GetElse(root, tokens, arr, tokens_pos, last);
    if (else_node) {
        if_node->right = else_node;
        else_node->parent = if_node;
    }
    printf("if %d\n", *tokens_pos);
    return if_node;
}

static DifNode_t *GetElse(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos, DifNode_t *if_node) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);
    assert(if_node);

    size_t save_pos = *tokens_pos;
    DifNode_t *node = NULL, *tok = NULL, *last = NULL;

    CHECK_EXPECTED_TOKEN(node, IsThatOperation(node, kOperationElse), );
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceOpen), );

    while (true) {
        DifNode_t *stmt = GetOp(root, tokens, arr, tokens_pos);
        if (!stmt) break;
        
        if (last) last = NEWOP(kOperationThen, last, stmt);
        else {
            last = stmt;
        }
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceClose), );
    node->left = if_node;
    node->right = last;
    if_node->parent = node;
    last->parent = node;
    root->size++;

    return node;
}

static DifNode_t *GetWhile(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    size_t save_pos = *tokens_pos;
    DifNode_t *tok = NULL;

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationWhile), );
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen), );

    DifNode_t *condition_node = GetExpression(root, tokens, arr, tokens_pos);
    if (!condition_node) {
        *tokens_pos = save_pos;
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParClose),
        fprintf(stderr, "%s", "SYNTAX_ERROR_WHILE: expected ')'\n"));
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_WHILE: expected '{'\n"));

    DifNode_t *body_node = ParseStatementSequence(root, tokens, arr, tokens_pos);
    if (!body_node) {
        *tokens_pos = save_pos;
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceClose),
        fprintf(stderr, "%s", "SYNTAX_ERROR_WHILE: expected '}'\n"));

    DifNode_t *while_node = GetStackElem(tokens, *tokens_pos - 1);
    while_node->type = kOperation;
    while_node->value.operation = kOperationWhile;
    while_node->left  = condition_node;
    while_node->right = body_node;
    condition_node->parent = while_node;
    body_node->parent = while_node;

    return while_node;
}

DifNode_t *GetPower(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *val = GetPrimary(root, tokens, arr, tokens_pos);
    if (!val) {
        return NULL;
    }

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    while (IsThatOperation(node, kOperationPow)) {
        (*tokens_pos)++;
        DifNode_t *val2 = GetPower(root, tokens, arr, tokens_pos);
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
    assert(tokens_pos);

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);

    if (node && node->type == kNumber) {
        (*tokens_pos)++;
        return node;
    }

    return NULL;
}

static DifNode_t *GetString(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *node = GetStackElem(tokens, *tokens_pos);
    if (node && node->type == kVariable) {
        (*tokens_pos)++;
    } else {
        return NULL;
    }

    return node;
}

#undef NEWN
#undef ADD_
#undef SUB_
#undef MUL_
#undef DIV_
#undef POW_

static DifNode_t *ParseFunctionArgs(DifRoot *root, Stack_Info *tokens, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(tokens_pos);

    DifNode_t *args_root = NULL, *rightmost = NULL;
    
    while (true) {
        DifNode_t *token = GetStackElem(tokens, *tokens_pos);
        if (!token || IsThatOperation(token, kOperationParClose)) {
            if (IsThatOperation(token, kOperationParClose)) (*tokens_pos)++;
            break;
        }
        
        if (IsThatOperation(token, kOperationComma)) {
            (*tokens_pos)++;
            continue;
        }
        
        if (token->type == kVariable || token->type == kNumber) {
            DifNode_t *arg = token;
            (*tokens_pos)++;
            
            if (!args_root) {
                args_root = arg;
            } else {
                DifNode_t *comma = NEWOP(kOperationComma, NULL, NULL);
                if (!rightmost) {
                    comma->left = args_root;
                    comma->right = arg;
                    args_root = comma;
                } else {
                    comma->left = rightmost->right;
                    comma->right = arg;
                    rightmost->right = comma;
                }
                rightmost = comma;
            }
        }
    }
    
    return args_root;
}

static DifNode_t *ParseFunctionBody(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *body_root = NULL;
    
    while (true) {
        DifNode_t *stmt = GetOp(root, tokens, arr, tokens_pos);
        if (!stmt) break;
        
        body_root = !body_root ? stmt : NEWOP(kOperationThen, body_root, stmt);
    }
    
    return body_root;
}

static DifNode_t *ParseStatementSequence(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *first_stmt = GetOp(root, tokens, arr, tokens_pos);
    if (!first_stmt) {
        fprintf(stderr, "SYNTAX_ERROR_WHILE: expected statements in body\n");
        return NULL;
    }

    DifNode_t *last_stmt = first_stmt;
    DifNode_t *tok = GetStackElem(tokens, *tokens_pos);

    while (IsThatOperation(tok, kOperationThen)) {
        (*tokens_pos)++;

        DifNode_t *next_stmt = GetOp(root, tokens, arr, tokens_pos);
        if (!next_stmt) {
            fprintf(stderr, "SYNTAX_ERROR_WHILE: expected statement after ';'\n");
            return NULL;
        }

        DifNode_t *then_node = GetStackElem(tokens, *tokens_pos - 1);
        then_node->left  = last_stmt;
        then_node->right = next_stmt;
        last_stmt->parent = then_node;
        next_stmt->parent = then_node;

        last_stmt = then_node;
        tok = GetStackElem(tokens, *tokens_pos);
    }

    return last_stmt;
}

static bool IsThatOperation(DifNode_t *node, OperationTypes type) {
    if (node && node->type == kOperation && node->value.operation == type) {
        return true;
    }
    return false;
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
        buf_out[j++] = buf_in[i];
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