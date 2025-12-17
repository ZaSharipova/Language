#include "Front-End/Rules.h"

#include "Enums.h"
#include "Structs.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "Front-End/LanguageFunctions.h"
#include "StackFunctions.h"
#include "DoGraph.h"
#include "Front-End/LexicalAnalysis.h"

static long long SizeOfFile(const char *filename);
static char *ReadToBuf(const char *filename, FILE *file, size_t filesize);

static bool IsThatOperation(LangNode_t *node, OperationTypes type);
static LangNode_t *ParseFunctionArgs(Language *lang_info, size_t *cnt);
static LangNode_t *ParseFunctionBody(Language *lang_info);
static LangNode_t *ParseStatementSequence(Language *lang_info);

#define CHECK_NULL_RETURN(name, cond) \
    LangNode_t *name = cond;           \
    if (name == NULL) {               \
        return NULL;                  \
    }

#define TRY_PARSE_RETURN(node_var, func_call)   \
    do {                                        \
        (node_var) = (func_call);               \
        if (node_var) {                         \
            return (node_var);                  \
        }                                       \
        *(lang_info->tokens_pos) = save_pos;    \
    } while (0)

#define CHECK_EXPECTED_TOKEN(out_tok, TRUE_COND, error_handler)                  \
    do {                                                                         \
        (out_tok) = GetStackElem((lang_info->tokens), *(lang_info->tokens_pos)); \
        if (!(TRUE_COND)) {                                                      \
            error_handler;                                                       \
            *(lang_info->tokens_pos) = (save_pos);                               \
            return NULL;                                                         \
        }                                                                        \
        (*lang_info->tokens_pos)++;                                              \
    } while (0)

// TODO: 1. перевод из дерева в ассеблер
// TODO: 4. сделать тернарные операторы
// TODO: 6. убрать указатель на таблицу меток в структуру узла
// TODO: 9. разобраться с makefile

// TODO: проверка на инициализированность

static LangNode_t *GetGoal(Language *lang_info);

static LangNode_t *GetAssignment(Language *lang_info);
static LangNode_t *GetOp(Language *lang_info);
static LangNode_t *GetWhile(Language *lang_info);
static LangNode_t *GetIf(Language *lang_info);
static LangNode_t *GetElse(Language *lang_info, LangNode_t *if_node);
static LangNode_t *GetFunctionDeclare(Language *lang_info);
static LangNode_t *GetFunctionCall(Language *lang_info);
LangNode_t *GetReturn(Language *lang_info);
static LangNode_t *GetPrintf(Language *lang_info);

static LangNode_t *GetExpression(Language *lang_info);
static LangNode_t *GetTerm(Language *lang_info);
static LangNode_t *GetPrimary(Language *lang_info);
static LangNode_t *GetPower(Language *lang_info);

static LangNode_t *GetNumber(Language *lang_info);
static LangNode_t *GetString(Language *lang_info);

DifErrors ReadInfix(Language *lang_info, DumpInfo *dump_info, const char *filename) {
    assert(lang_info);
    assert(dump_info);
    assert(filename);

    FILE_OPEN_AND_CHECK(file, filename, "r");

    FileInfo Info = {};
    DoBufRead(file, filename, &Info);
    fclose(file);

    Stack_Info tokens = {};
    StackCtor(&tokens, 1, stderr);
    CheckAndReturn(lang_info->root, (const char **)&Info.buf_ptr, &tokens, lang_info->arr);
    lang_info->tokens = &tokens;

    size_t tokens_pos = 0;
    lang_info->tokens_pos = &tokens_pos;

    lang_info->root->root = GetGoal(lang_info);
    if (!lang_info->root->root) {
        return kFailure;
    }
    
    dump_info->tree = lang_info->root;

    strcpy(dump_info->message, "Code");
    DoTreeInGraphviz(lang_info->root->root, dump_info, lang_info->arr);
    StackDtor(&tokens, stderr);

    return kSuccess;
}

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define NEWOP(op, left, right) NewNode(lang_info->root, kOperation, (Value){ .operation = (op) }, left, right) 

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

static LangNode_t *GetGoal(Language *lang_info) {
    assert(lang_info);

    int cnt = 0;
    LangNode_t *first = NULL;
    do {
        LangNode_t *next = GetFunctionDeclare(lang_info);
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

LangNode_t *GetReturn(Language *lang_info) {
    assert(lang_info);

    LangNode_t *return_node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!IsThatOperation(return_node, kOperationReturn)) {
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    LangNode_t *node = GetExpression(lang_info);
    if (!node) {
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    return NEWOP(kOperationReturn, node, NULL);
}

LangNode_t *GetScanf(Language *lang_info) {
    assert(lang_info);

    size_t save_pos = (*lang_info->tokens_pos);

    LangNode_t *return_node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!IsThatOperation(return_node, kOperationRead)) {
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    LangNode_t *tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!IsThatOperation(tok, kOperationParOpen)) {
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }
    (*lang_info->tokens_pos)++; 
    
    LangNode_t *node = GetString(lang_info);
    if (!node) {
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }

    tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!IsThatOperation(tok, kOperationParClose)) {
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!IsThatOperation(tok, kOperationThen)) {
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    return NEWOP(kOperationRead, node, NULL);
}

LangNode_t *GetOp(Language *lang_info) {
    assert(lang_info);

    LangNode_t *stmt = NULL;
    size_t save_pos = (*lang_info->tokens_pos);

    TRY_PARSE_RETURN(stmt, GetReturn(lang_info));
    TRY_PARSE_RETURN(stmt, GetPrintf(lang_info));
    TRY_PARSE_RETURN(stmt, GetScanf(lang_info));
    TRY_PARSE_RETURN(stmt, GetWhile (lang_info));
    TRY_PARSE_RETURN(stmt, GetIf    (lang_info));
    TRY_PARSE_RETURN(stmt, GetFunctionCall(lang_info));

    LangNode_t *seq = NULL;

    while (1) {
        save_pos = (*lang_info->tokens_pos);
        stmt = GetAssignment(lang_info);
        if (!stmt) {
            (*lang_info->tokens_pos) = save_pos;
            break;
        }

        if (!seq) {
            seq = stmt;
        } else {
            seq = NEWOP(kOperationThen, seq, stmt);
        }
        LangNode_t *tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
        if (!IsThatOperation(tok, kOperationThen)) {
            break;
        }

        (*lang_info->tokens_pos)++; 
    }   

    return seq;
}

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define ADD_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationAdd}, left, right)
#define SUB_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationSub}, left, right)
#define MUL_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationMul}, left, right)
#define DIV_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationDiv}, left, right)
#define POW_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationPow}, left, right)

static LangNode_t *GetPrintf(Language *lang_info) { //
    assert(lang_info);

    LangNode_t *print_f = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (IsThatOperation(print_f, kOperationWrite)) {
        (*lang_info->tokens_pos)++; 

        size_t save_pos = *(lang_info->tokens_pos);
        LangNode_t *par = NULL;
        CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParOpen),);

        LangNode_t *printf_arg = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
        if (!(printf_arg && (printf_arg->type == kVariable || printf_arg->type == kNumber))) { // сделать и для функций
            fprintf(stderr, "NO AVAILABLE ARGUMENT FOR PRINTF WRITTEN.\n");
            return NULL; //
        }
        (*lang_info->tokens_pos)++; 

        CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParClose), );

        print_f->left = printf_arg;
        printf_arg->parent = print_f;
        (*lang_info->tokens_pos)++; 
        return print_f;
    } else {
        return NULL;
    }
}

static LangNode_t *GetFunctionDeclare(Language *lang_info) {
    assert(lang_info);
    
    size_t save_pos = *(lang_info->tokens_pos);
    LangNode_t *token = NULL, *func_name = NULL;
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationFunction), );
    CHECK_EXPECTED_TOKEN(func_name, func_name && func_name->type == kVariable, );
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationParOpen), );
    
    lang_info->arr->var_array[func_name->value.pos].type = kVarFunction;
    size_t cnt = 0;
    LangNode_t *args_root = ParseFunctionArgs(lang_info, &cnt);
    if (lang_info->arr->var_array[func_name->value.pos].variable_value != (int)cnt) {
        if (lang_info->arr->var_array[func_name->value.pos].variable_value == POISON) lang_info->arr->var_array[func_name->value.pos].variable_value = (int)cnt;
        else {
            fprintf(stderr, "Number of function arguments is not the same as in declaration: had to be %d, got %zu\n", 
                lang_info->arr->var_array[func_name->value.pos].variable_value, cnt);
            return NULL;
        }
    }
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_FUNC: expected '{' after function declaration\n"));
    
    LangNode_t *body_root = ParseFunctionBody(lang_info);
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceClose),
        fprintf(stderr, "SYNTAX_ERROR_FUNC: expected '}' at end of function body %zu.\n", *(lang_info->tokens_pos)));
    
    return NEWOP(kOperationFunction, func_name, NEWOP(kOperationThen, args_root, body_root));
}

static LangNode_t *GetFunctionCall(Language *lang_info) {
    assert(lang_info);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *name_token = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!name_token || name_token->type != kVariable) {
        *lang_info->tokens_pos= save_pos;
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    LangNode_t *par_open = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!IsThatOperation(par_open, kOperationParOpen)) {
        *lang_info->tokens_pos= save_pos;
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    LangNode_t *args_root = NULL;
    LangNode_t *rightmost = NULL;

    while (true) {
        LangNode_t *next_tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
        if (!next_tok) {
            fprintf(stderr, "SYNTAX_ERROR: unexpected end of tokens in call args\n");
            *lang_info->tokens_pos = save_pos;
            return NULL;
        }

        if (IsThatOperation(next_tok, kOperationParClose)) {
            (*(lang_info->tokens_pos))++;
            break;
        }

        if (IsThatOperation(next_tok, kOperationComma)) {
            (*(lang_info->tokens_pos))++;
            continue;
        }

        LangNode_t *arg = GetExpression(lang_info);
        if (!arg) {
            fprintf(stderr, "SYNTAX_ERROR: expected argument in function call\n");
            *lang_info->tokens_pos = save_pos;
            return NULL;
        }
        
        int cnt = 0;
        cnt++;
        if (!args_root) {
            args_root = arg;
        } else {
            LangNode_t *comma_node = NEWOP(kOperationComma, rightmost ? rightmost->right : args_root, arg);
            if (!rightmost) {
                args_root = comma_node;
            } else {
                rightmost->right = comma_node;
            }
            rightmost = comma_node;
        }
    }
    // проверка что функция содержит правильное количество аргументов

    return NEWOP(kOperationCall, name_token, args_root);
}


static LangNode_t *GetExpression(Language *lang_info) {
    assert(lang_info);

    LangNode_t *val = GetTerm(lang_info);
    if (!val) return NULL;

    lang_info->root->size++;
    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    while (IsThatOperation(node, kOperationAdd) || IsThatOperation(node, kOperationSub)) {
        (*lang_info->tokens_pos)++;

        LangNode_t *val2 = GetTerm(lang_info);
        if (!val2) return val;

        lang_info->root->size++;
        node->left  = val;
        node->right = val2;
        val->parent  = node;
        val2->parent = node;
        val = node;

        node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    }

    return val;
}

static LangNode_t *GetTerm(Language *lang_info) {
    assert(lang_info);

    LangNode_t *left = GetPower(lang_info);
    if (!left) return NULL;

    lang_info->root->size++;
    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    while (IsThatOperation(node, kOperationMul) || IsThatOperation(node, kOperationDiv)) {
        (*lang_info->tokens_pos)++; 

        LangNode_t *right = GetTerm(lang_info);
        if (!right) return left;

        if (lang_info &&  lang_info->root->size++);

        node->left  = left;
        node->right = right;
        left->parent  = node;
        right->parent = node;

        left = node;

        node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    }

    return left;
}

static LangNode_t *GetPrimary(Language *lang_info) {
    assert(lang_info);

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!node) {
        return NULL;
    }

    if (IsThatOperation(node, kOperationParOpen)) {
        (*lang_info->tokens_pos)++; 

        LangNode_t *val = GetExpression(lang_info);
        if (!val) {
            return NULL;
        }

        node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
        if (IsThatOperation(node, kOperationParClose)) {
            (*lang_info->tokens_pos)++; 
        } else {
            fprintf(stderr, "SYNTAX_ERROR_P: expected ')'\n");
            return NULL;
        }

        return val;
    }

    
    LangNode_t *value_number = GetNumber(lang_info);
    if (value_number) {
        return value_number;
    }

    value_number = GetFunctionCall(lang_info);
    if (value_number) {
        return value_number;
    }

    return GetString(lang_info);
}


LangNode_t *GetAssignment(Language *lang_info) {
    assert(lang_info);

    size_t save_pos = *lang_info->tokens_pos;
    
    LangNode_t *maybe_var = GetString(lang_info);
    if (!maybe_var) {
        *lang_info->tokens_pos = save_pos;
        return NULL;
    }

    LangNode_t *assign_op = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!assign_op || assign_op->type != kOperation || assign_op->value.operation != kOperationIs) {
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }
    (*lang_info->tokens_pos)++;
    LangNode_t *value = NULL;

    LangNode_t *tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    LangNode_t *next = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos) + 1);
    if (tok && IsThatOperation(next, kOperationParOpen)) {
        value = GetFunctionCall(lang_info);
    }
    else {
        value = GetExpression(lang_info);
    }
    
    if (!value) {
        fprintf(stderr, "SYNTAX_ERROR_ASSIGNMENT: no body of assignment\n");
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }

    assign_op->left = maybe_var;
    assign_op->right = value;
    maybe_var->parent = assign_op;
    value->parent = assign_op;

    if (lang_info->arr->var_array[maybe_var->value.pos].variable_value == POISON && 
        value->type == kNumber) {
        lang_info->arr->var_array[maybe_var->value.pos].variable_value = value->value.number;
    }
    
    return NEWOP(kOperationThen, assign_op, NULL);
}

static LangNode_t *GetIf(Language *lang_info) {
    assert(lang_info);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *tok = NULL;

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationIf),);
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected '('\n"));
    
    LangNode_t *cond = GetExpression(lang_info);
    if (!cond) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected condition\n");
        return NULL;
    }

    LangNode_t *sign = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (IsThatOperation(sign, kOperationBE) || IsThatOperation(sign, kOperationB) || IsThatOperation(sign, kOperationAE) 
            || IsThatOperation(sign, kOperationA) || IsThatOperation(sign, kOperationE)) {
        (*lang_info->tokens_pos)++;
        LangNode_t *number = GetExpression(lang_info);
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

    LangNode_t *first = GetOp(lang_info);
    if (!first) {
        fprintf(stderr, "SYNTAX_ERROR_IF: empty if-body\n");
        return NULL;
    }

    LangNode_t *last = first;

    while (true) {
        LangNode_t *stmt = GetOp(lang_info);
        if (!stmt) break;
        
        last = NEWOP(kOperationThen, last, stmt);
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceClose), 
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected '}'\n"));

    LangNode_t *if_node = NEWOP(kOperationIf, cond, last);

    LangNode_t *else_node = GetElse(lang_info, last);
    if (else_node) {
        if_node->right = else_node;
        else_node->parent = if_node;
    }
    //printf("if %zu\n", *tokens_pos);
    return if_node;
}

static LangNode_t *GetElse(Language *lang_info, LangNode_t *if_node) {
    assert(lang_info);
    assert(if_node);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *node = NULL, *tok = NULL, *last = NULL;

    CHECK_EXPECTED_TOKEN(node, IsThatOperation(node, kOperationElse), );
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceOpen), );

    while (true) {
        LangNode_t *stmt = GetOp(lang_info);
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
    lang_info->root->size++;

    return node;
}

static LangNode_t *GetWhile(Language *lang_info) {
    assert(lang_info);

    size_t save_pos = *(lang_info->tokens_pos);
    LangNode_t *tok = NULL;

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationWhile), );
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen), );

    LangNode_t *condition_node = GetExpression(lang_info);
    if (!condition_node) {
        *(lang_info->tokens_pos)= save_pos;
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParClose),
        fprintf(stderr, "%s", "SYNTAX_ERROR_WHILE: expected ')'\n"));
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_WHILE: expected '{'\n"));

    LangNode_t *body_node = ParseStatementSequence(lang_info);
    if (!body_node) {
        *(lang_info->tokens_pos) = save_pos;
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceClose),
        fprintf(stderr, "%s", "SYNTAX_ERROR_WHILE: expected '}'\n"));

    LangNode_t *while_node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos) - 1);
    while_node->type = kOperation;
    while_node->value.operation = kOperationWhile;
    while_node->left  = condition_node;
    while_node->right = body_node;
    condition_node->parent = while_node;
    body_node->parent = while_node;

    return while_node;
}

LangNode_t *GetPower(Language *lang_info) {
    assert(lang_info);

    LangNode_t *val = GetPrimary(lang_info);
    if (!val) {
        return NULL;
    }

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    while (IsThatOperation(node, kOperationPow)) {
        (*(lang_info->tokens_pos))++;
        LangNode_t *val2 = GetPower(lang_info);
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

        node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    }

    return val;
}

static LangNode_t *GetNumber(Language *lang_info) {
    assert(lang_info);

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    if (node && node->type == kNumber) {
        (*(lang_info->tokens_pos))++;
        return node;
    }

    return NULL;
}

static LangNode_t *GetString(Language *lang_info) {
    assert(lang_info);

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (node && node->type == kVariable) {
        (*(lang_info->tokens_pos))++;
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

static LangNode_t *ParseFunctionArgs(Language *lang_info, size_t *cnt) {
    assert(lang_info);
    assert(cnt);

    LangNode_t *args_root = NULL, *rightmost = NULL;
    
    while (true) {
        LangNode_t *token = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
        if (!token || IsThatOperation(token, kOperationParClose)) {
            if (IsThatOperation(token, kOperationParClose)) (*(lang_info->tokens_pos))++;
            break;
        }
        
        if (IsThatOperation(token, kOperationComma)) {
            (*(lang_info->tokens_pos))++;
            continue;
        }
        
        if (token->type == kVariable || token->type == kNumber) {
            LangNode_t *arg = token;
            (*(lang_info->tokens_pos))++;
            (*cnt)++;
            
            if (!args_root) {
                args_root = arg;
            } else {
                LangNode_t *comma = NEWOP(kOperationComma, NULL, NULL);
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

static LangNode_t *ParseFunctionBody(Language *lang_info) {
    assert(lang_info);

    LangNode_t *body_root = NULL;
    
    while (true) {
        LangNode_t *stmt = GetOp(lang_info);
        if (!stmt) {
            break;
        }
        body_root = !body_root ? stmt : NEWOP(kOperationThen, body_root, stmt);
    }
    
    return body_root;
}

static LangNode_t *ParseStatementSequence(Language *lang_info) {
    assert(lang_info);

    LangNode_t *first_stmt = GetOp(lang_info);
    if (!first_stmt) {
        fprintf(stderr, "SYNTAX_ERROR_WHILE: expected statements in body\n");
        return NULL;
    }

    LangNode_t *last_stmt = first_stmt;
    LangNode_t *tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    while (IsThatOperation(tok, kOperationThen)) {
        *(lang_info->tokens_pos)++;

        LangNode_t *next_stmt = GetOp(lang_info);
        if (!next_stmt) {
            fprintf(stderr, "SYNTAX_ERROR_WHILE: expected statement after ';'\n");
            return NULL;
        }

        LangNode_t *then_node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos) - 1);
        then_node->left  = last_stmt;
        then_node->right = next_stmt;
        last_stmt->parent = then_node;
        next_stmt->parent = then_node;

        last_stmt = then_node;
        tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    }

    return last_stmt;
}

static bool IsThatOperation(LangNode_t *node, OperationTypes type) {
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