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
static DifNode_t *ParseFunctionArgs(DifRoot *root, Stack_Info *tokens, size_t *tokens_pos, size_t *cnt);
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
// TODO: 4. сделать тернарные операторы
// TODO: 6. убрать указатель на таблицу меток в структуру узла
// TODO: 9. разобраться с makefile

// TODO: проверка на инициализированность

static DifNode_t *GetGoal(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);

static DifNode_t *GetAssignment(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetOp(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetWhile(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetIf(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetElse(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos, DifNode_t *if_node);
static DifNode_t *GetFunctionDeclare(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
static DifNode_t *GetFunctionCall(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
DifNode_t *GetReturn(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos);
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

DifNode_t *GetReturn(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *return_node = GetStackElem(tokens, *tokens_pos);
    if (!IsThatOperation(return_node, kOperationReturn)) {
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *node = GetExpression(root, tokens, arr, tokens_pos);
    if (!node) {
        return NULL;
    }
    (*tokens_pos)++;

    return NEWOP(kOperationReturn, node, NULL);
}

DifNode_t *GetScanf(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    size_t save_pos = *tokens_pos;

    DifNode_t *return_node = GetStackElem(tokens, *tokens_pos);
    if (!IsThatOperation(return_node, kOperationRead)) {
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *tok = GetStackElem(tokens, *tokens_pos);
    if (!IsThatOperation(tok, kOperationParOpen)) {
        *tokens_pos = save_pos;
        return NULL;
    }
    (*tokens_pos)++;
    
    DifNode_t *node = GetString(root, tokens, arr, tokens_pos);
    if (!node) {
        *tokens_pos = save_pos;
        return NULL;
    }

    tok = GetStackElem(tokens, *tokens_pos);
    if (!IsThatOperation(tok, kOperationParClose)) {
        *tokens_pos = save_pos;
        return NULL;
    }
    (*tokens_pos)++;

    tok = GetStackElem(tokens, *tokens_pos);
    if (!IsThatOperation(tok, kOperationThen)) {
        *tokens_pos = save_pos;
        return NULL;
    }
    (*tokens_pos)++;

    return NEWOP(kOperationRead, node, NULL);
}

DifNode_t *GetOp(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    DifNode_t *stmt = NULL;
    size_t save_pos = *tokens_pos;

    TRY_PARSE_RETURN(stmt, GetReturn(root, tokens, arr, tokens_pos));
    TRY_PARSE_RETURN(stmt, GetPrintf(root, tokens, arr, tokens_pos));
    TRY_PARSE_RETURN(stmt, GetScanf(root, tokens, arr, tokens_pos));
    TRY_PARSE_RETURN(stmt, GetWhile (root, tokens, arr, tokens_pos));
    TRY_PARSE_RETURN(stmt, GetIf    (root, tokens, arr, tokens_pos));
    TRY_PARSE_RETURN(stmt, GetFunctionCall(root, tokens, arr, tokens_pos));

    DifNode_t *seq = NULL;

    while (1) {
        save_pos = *tokens_pos;
        stmt = GetAssignment(root, tokens, arr, tokens_pos);
        if (!stmt) {
            *tokens_pos = save_pos;
            break;
        }

        if (!seq) {
            seq = stmt;
        } else {
            seq = NEWOP(kOperationThen, seq, stmt);
        }
        DifNode_t *tok = GetStackElem(tokens, *tokens_pos);
        if (!IsThatOperation(tok, kOperationThen)) {
            break;
        }

        (*tokens_pos)++;
    }

    return seq;
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
    
    arr->var_array[func_name->value.pos].type = kVarFunction;
    size_t cnt = 0;
    DifNode_t *args_root = ParseFunctionArgs(root, tokens, tokens_pos, &cnt);
    if (arr->var_array[func_name->value.pos].variable_value != (int)cnt) {
        if (arr->var_array[func_name->value.pos].variable_value == POISON) arr->var_array[func_name->value.pos].variable_value = (int)cnt;
        else {
            fprintf(stderr, "Number of function arguments is not the same as in declaration: had to be %d, got %d\n", 
                arr->var_array[func_name->value.pos].variable_value, cnt);
            return NULL;
        }
    }
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_FUNC: expected '{' after function declaration\n"));
    
    DifNode_t *body_root = ParseFunctionBody(root, tokens, arr, tokens_pos);
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceClose),
        fprintf(stderr, "SYNTAX_ERROR_FUNC: expected '}' at end of function body %d.\n", *tokens_pos));
    
    return NEWOP(kOperationFunction, func_name, NEWOP(kOperationThen, args_root, body_root));
}

static DifNode_t *GetFunctionCall(DifRoot *root, Stack_Info *tokens, VariableArr *arr, size_t *tokens_pos) {
    assert(root);
    assert(tokens);
    assert(arr);
    assert(tokens_pos);

    size_t save_pos = *tokens_pos;
    DifNode_t *name_token = GetStackElem(tokens, *tokens_pos);
    if (!name_token || name_token->type != kVariable) {
        *tokens_pos = save_pos;
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *par_open = GetStackElem(tokens, *tokens_pos);
    if (!IsThatOperation(par_open, kOperationParOpen)) {
        *tokens_pos = save_pos;
        return NULL;
    }
    (*tokens_pos)++;

    DifNode_t *args_root = NULL;
    DifNode_t *rightmost = NULL;
    int cnt = 0;

    while (true) {
        DifNode_t *next_tok = GetStackElem(tokens, *tokens_pos);
        if (!next_tok) {
            fprintf(stderr, "SYNTAX_ERROR: unexpected end of tokens in call args\n");
            *tokens_pos = save_pos;
            return NULL;
        }

        if (IsThatOperation(next_tok, kOperationParClose)) {
            (*tokens_pos)++;
            break;
        }

        if (IsThatOperation(next_tok, kOperationComma)) {
            (*tokens_pos)++;
            continue;
        }

        DifNode_t *arg = GetExpression(root, tokens, arr, tokens_pos);
        if (!arg) {
            fprintf(stderr, "SYNTAX_ERROR: expected argument in function call\n");
            *tokens_pos = save_pos;
            return NULL;
        }
        
        cnt++;
        if (!args_root) {
            args_root = arg;
        } else {
            DifNode_t *comma_node = NEWOP(kOperationComma, rightmost ? rightmost->right : args_root, arg);
            if (!rightmost) {
                args_root = comma_node;
            } else {
                rightmost->right = comma_node;
            }
            rightmost = comma_node;
        }
    }

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

    value_number = GetFunctionCall(root, tokens, arr, tokens_pos);
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

    DifNode_t *assign_op = GetStackElem(tokens, *tokens_pos);
    if (!assign_op || assign_op->type != kOperation || assign_op->value.operation != kOperationIs) {
        *tokens_pos = save_pos;
        return NULL;
    }
    (*tokens_pos)++;
    size_t value_pos = *tokens_pos;
    DifNode_t *value = NULL;

    DifNode_t *tok = GetStackElem(tokens, *tokens_pos);
    DifNode_t *next = GetStackElem(tokens, *tokens_pos + 1);
    if (tok && IsThatOperation(next, kOperationParOpen)) {
        value = GetFunctionCall(root, tokens, arr, tokens_pos);
    }
    else {
        value = GetExpression(root, tokens, arr, tokens_pos);
    }
    
    if (!value) {
        fprintf(stderr, "SYNTAX_ERROR_ASSIGNMENT: no body of assignment\n");
        *tokens_pos = save_pos;
        return NULL;
    }

    assign_op->left = maybe_var;
    assign_op->right = value;
    maybe_var->parent = assign_op;
    value->parent = assign_op;

    if (arr->var_array[maybe_var->value.pos].variable_value == POISON && 
        value->type == kNumber) {
        arr->var_array[maybe_var->value.pos].variable_value = value->value.number;
    }
    
    return NEWOP(kOperationThen, assign_op, NULL);
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

    DifNode_t *sign = GetStackElem(tokens, *tokens_pos);
    if (IsThatOperation(sign, kOperationBE)) {
        (*tokens_pos)++;
        DifNode_t *number = GetExpression(root, tokens, arr, tokens_pos);
        if (!number) {
            fprintf(stderr, "SYNTAX_ERROR_IF: no expression if if written.\n");
        }
        sign->left = cond;
        cond->parent = sign;
        sign->right = number;
        number->parent = sign;
        cond = sign;
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
    //printf("if %zu\n", *tokens_pos);
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

static DifNode_t *ParseFunctionArgs(DifRoot *root, Stack_Info *tokens, size_t *tokens_pos, size_t *cnt) {
    assert(root);
    assert(tokens);
    assert(tokens_pos);
    assert(cnt);

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
            (*cnt)++;
            
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
        if (!stmt) {
            break;
        }
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