#include "Front-End/Rules.h"

#include "Common/Enums.h"
#include "Common/Structs.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

#include "Common/LanguageFunctions.h"
#include "Common/StackFunctions.h"
#include "Common/DoGraph.h"
#include "Front-End/LexicalAnalysis.h"
#include "Common/CommonFunctions.h"

#define CHECK_NULL_RETURN(name, cond) \
    LangNode_t *name = cond;          \
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

static LangNode_t *GetGoal(Language *lang_info);
static LangNode_t *GetAssignment(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetOp(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetFunctionDeclare(Language *lang_info);
static LangNode_t *GetFunctionCall(Language *lang_info);


static LangNode_t *GetWhile(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetIf(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetElse(Language *lang_info, LangNode_t *if_node, LangNode_t *func_name);
static LangNode_t *GetReturn(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetPrintf(Language *lang_info);
static LangNode_t *GetScanf(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetUnaryFunc(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetHLT(Language *lang_info);

static LangNode_t *GetExpression(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetTerm(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetPrimary(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetPower(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetTernary(Language *lang_info, LangNode_t *func_name);

static LangNode_t *GetVariableAddr(Language *lang_info, LangNode_t *func_name, ValCategory mode);
static LangNode_t *GetNumber(Language *lang_info);
static LangNode_t *GetString(Language *lang_info, LangNode_t *func_name, ValCategory val_cat);
static LangNode_t *GetArrayElement(Language *lang_info, LangNode_t *func_name);

static LangNode_t *ParseFunctionArgs(Language *lang_info, size_t *cnt);
static LangNode_t *ParseFunctionBody(Language *lang_info, LangNode_t *func_name);

static LangNode_t *GetArrayAssignment(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetAssignmentLValue(Language *lang_info, LangNode_t *func_name);
static bool CheckAndSetFunctionArgsNumber(Language *lang_info, LangNode_t *name_token, size_t cnt);

DifErrors ReadInfix(Language *lang_info, DumpInfo *dump_info, const char *filename) {
    assert(lang_info);
    assert(dump_info);
    assert(filename);

    FILE_OPEN_AND_CHECK(file, filename, "r", NULL, NULL, NULL);

    FileInfo Info = {};
    DoBufRead(file, filename, &Info);
    fclose(file);

    const char *temp_buf_ptr = Info.buf_ptr;
    CheckAndReturn(lang_info, &temp_buf_ptr);
    free(Info.buf_ptr);

    size_t tokens_pos = 0;
    lang_info->tokens_pos = &tokens_pos;

    lang_info->root->root = GetGoal(lang_info);
    if (!lang_info->root->root) {
        return kFailure;
    }
    
    DoTreeInGraphviz(lang_info->root->root, dump_info, lang_info->arr);

    return kSuccess;
}

#define NEWN(num) NewNode(lang_info, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define NEWOP(op, left, right) NewNode(lang_info, kOperation, (Value){ .operation = (op) }, left, right) 

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

    LangNode_t *first = NULL;
    do {
        LangNode_t *next = GetFunctionDeclare(lang_info);
        if (!next) break;
        else if (!first) {
            first = next;
        } else if (first && !first->left) {
            first->left = next;
        } else if (first && !first->right) {
            first->right = next;
        } else {
            first = NEWOP(kOperationThen, first, next);
        }
    } while (true);

    return first;
}

static LangNode_t *GetReturn(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *return_node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!IsThatOperation(return_node, kOperationReturn)) {
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    LangNode_t *node = GetExpression(lang_info, func_name);
    if (!node) {
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    return_node->left = node;
    node->parent = return_node;
    return return_node;
}

static LangNode_t *GetScanf(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = (*lang_info->tokens_pos);

    LangNode_t *read_node = NULL;
    CHECK_EXPECTED_TOKEN(read_node, IsThatOperation(read_node, kOperationRead),);

    LangNode_t *tok = NULL;
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen), );
    
    LangNode_t *node = GetString(lang_info, func_name, krvalue);
    if (!node) {
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParClose), 
        fprintf(stderr, "SYNTAX_ERROR_SCANF: no closing par.\n"));

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationThen), 
        fprintf(stderr, "SYNTAX_ERROR_SCANF: no ';'.\n"));

    read_node->left = node;
    node->parent = read_node;
    return read_node;
}

LangNode_t *GetStatement(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    
    LangNode_t *stmt = GetAssignment(lang_info, func_name);
    if (stmt) {
        return stmt;
    }
    
    *lang_info->tokens_pos = save_pos;

    LangNode_t *func_call = GetFunctionCall(lang_info);
    if (func_call) {
        LangNode_t *tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
        if (IsThatOperation(tok, kOperationThen)) {
            (*lang_info->tokens_pos)++;
            return func_call;

        } else {
            fprintf(stderr, "SYNTAX_ERROR: expected ';' after function call.\n");
            *lang_info->tokens_pos = save_pos;
            return NULL;
        }
    }
    
    return NULL;
}

LangNode_t *GetOp(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *stmt = NULL;
    size_t save_pos = (*lang_info->tokens_pos);

    TRY_PARSE_RETURN(stmt, GetReturn(lang_info, func_name));
    TRY_PARSE_RETURN(stmt, GetPrintf(lang_info));
    TRY_PARSE_RETURN(stmt, GetScanf(lang_info, func_name));
    TRY_PARSE_RETURN(stmt, GetWhile (lang_info, func_name));
    TRY_PARSE_RETURN(stmt, GetIf    (lang_info, func_name));
    TRY_PARSE_RETURN(stmt, GetHLT   (lang_info));

    LangNode_t *seq = NULL;

    while (true) {
        save_pos = *lang_info->tokens_pos;
        
        stmt = GetStatement(lang_info, func_name);
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

#define NEWN(num) NewNode(lang_info, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define NEWV(name) NewVariable(lang_info, name)
#define ADD_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationAdd}, left, right)
#define SUB_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationSub}, left, right)
#define MUL_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationMul}, left, right)
#define DIV_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationDiv}, left, right)
#define POW_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationPow}, left, right)

static LangNode_t *GetPrintf(Language *lang_info) {
    assert(lang_info);

    LangNode_t *print_node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (IsThatOperation(print_node, kOperationWrite) || IsThatOperation(print_node, kOperationWriteChar)) {
        (*lang_info->tokens_pos)++;

        size_t save_pos = *(lang_info->tokens_pos);
        LangNode_t *par = NULL;
        CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParOpen),);

        LangNode_t *printf_arg = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
        if (!(printf_arg && (printf_arg->type == kVariable || printf_arg->type == kNumber))) { // TODO: make it available for functions too
            fprintf(stderr, "NO AVAILABLE ARGUMENT FOR PRINTF WRITTEN.\n");
            return NULL;
        }
        (*lang_info->tokens_pos)++; 

        CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParClose), );

        print_node->left = printf_arg;
        printf_arg->parent = print_node;
        (*lang_info->tokens_pos)++; 
        return print_node;
    }

    return NULL;
}

static LangNode_t *GetFunctionDeclare(Language *lang_info) {
    assert(lang_info);
    
    size_t save_pos = *(lang_info->tokens_pos);
    LangNode_t *func_node = NULL, *token = NULL, *func_name = NULL;
    
    CHECK_EXPECTED_TOKEN(func_node, IsThatOperation(func_node, kOperationFunction), );
    CHECK_EXPECTED_TOKEN(func_name, func_name && func_name->type == kVariable, );
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationParOpen), );
    
    lang_info->arr->var_array[func_name->value.pos].type = kVarFunction;
    size_t cnt = 0;
    LangNode_t *args_root = ParseFunctionArgs(lang_info, &cnt);

    if (!CheckAndSetFunctionArgsNumber(lang_info, func_name, cnt)) {
        return NULL;
    }
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_FUNC: expected '{' after function declaration\n"));
    
    LangNode_t *body_root = ParseFunctionBody(lang_info, func_name);
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceClose),
        fprintf(stderr, "SYNTAX_ERROR_FUNC: expected '}' at end of function body %zu.\n", *(lang_info->tokens_pos)));

    func_node->left = func_name;
    func_name->parent = func_node;
    func_node->right = NEWOP(kOperationThen, args_root, body_root); //

    return func_node;
}

static LangNode_t *GetFunctionCall(Language *lang_info) {
    assert(lang_info);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *name_token = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    if (!name_token || name_token->type != kVariable) {
        *lang_info->tokens_pos = save_pos;
        return NULL;
    }
    (*lang_info->tokens_pos)++; 

    LangNode_t *par_open = NULL;
    CHECK_EXPECTED_TOKEN(par_open, IsThatOperation(par_open, kOperationParOpen),);

    LangNode_t *args_root = NULL;
    LangNode_t *rightmost = NULL;
    size_t cnt = 0;

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

        LangNode_t *arg = GetExpression(lang_info, name_token);
        if (!arg) {
            fprintf(stderr, "SYNTAX_ERROR: expected argument in function call\n");
            *lang_info->tokens_pos = save_pos;
            return NULL;
        }
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

    if (!CheckAndSetFunctionArgsNumber(lang_info, name_token, cnt)) {
        return NULL;
    }

    return NEWOP(kOperationCall, name_token, args_root);
}


static LangNode_t *GetExpression(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *val = GetTerm(lang_info, func_name);
    if (!val) {
        return NULL;
    }

    lang_info->root->size++;
    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    while (IsThatOperation(node, kOperationAdd) || IsThatOperation(node, kOperationSub)) {
        (*lang_info->tokens_pos)++;

        LangNode_t *val2 = GetTerm(lang_info, func_name);
        if (!val2) {
            return val;
        }

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

static LangNode_t *GetTerm(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *left = GetPower(lang_info, func_name);
    if (!left) {
        return NULL;
    }

    lang_info->root->size++;
    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    while (IsThatOperation(node, kOperationMul) || IsThatOperation(node, kOperationDiv)) {
        (*lang_info->tokens_pos)++; 

        LangNode_t *right = GetTerm(lang_info, func_name);
        if (!right) {
            return left;
        }

        node->left  = left;
        node->right = right;
        left->parent  = node;
        right->parent = node;

        left = node;
        node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    }

    return left;
}

static LangNode_t *GetPrimary(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!node) {
        return NULL;
    }
    
    if (IsThatOperation(node, kOperationParOpen)) {
        (*lang_info->tokens_pos)++; 
        
        LangNode_t *val = GetExpression(lang_info, func_name);
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
    
    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *value = NULL;

    TRY_PARSE_RETURN(value, GetNumber(lang_info));
    TRY_PARSE_RETURN(value, GetFunctionCall(lang_info));
    TRY_PARSE_RETURN(value, GetUnaryFunc(lang_info, func_name));
    TRY_PARSE_RETURN(value, GetArrayElement(lang_info, func_name));
    TRY_PARSE_RETURN(value, GetVariableAddr(lang_info, func_name, krvalue));
    
    return GetString(lang_info, func_name, krvalue);
}

static LangNode_t *GetUnaryFunc(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *unary_func_name = NULL;
    size_t save_pos = *lang_info->tokens_pos;
    CHECK_EXPECTED_TOKEN(unary_func_name, IsThatOperation(unary_func_name, kOperationSQRT),);
    
    LangNode_t *par = NULL;
    CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParOpen),);
    
    LangNode_t *value = GetExpression(lang_info, func_name);
    if (!value) {
        fprintf(stderr, "SYNTAX_ERROR_UNARY\n");
        return NULL;
    }
    
    CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParClose),);
    unary_func_name->left = value;
    value->parent = unary_func_name;

    return unary_func_name;
}

LangNode_t *GetAssignment(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *value = NULL;

    TRY_PARSE_RETURN(value, GetArrayAssignment(lang_info, func_name));
    TRY_PARSE_RETURN(value, GetTernary(lang_info, func_name));

    LangNode_t *assign_op = NULL;
    if (!value) {
        assign_op = GetAssignmentLValue(lang_info, func_name);
        if (!assign_op) {
            return NULL;
        }

        LangNode_t *tok = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
        LangNode_t *next = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos) + 1);
        if (tok && IsThatOperation(next, kOperationParOpen) && !IsThatOperation(tok, kOperationSQRT) && tok->type == kVariable) {
            value = GetFunctionCall(lang_info);
        } else {
            value = GetExpression(lang_info, func_name);
        }
    }
    
    if (!value) {
        fprintf(stderr, "SYNTAX_ERROR_ASSIGNMENT: no body of assignment %s\n", lang_info->arr->var_array[func_name->value.pos].variable_name);
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }

    assign_op->right = value;
    value->parent = assign_op;

    if (lang_info->arr->var_array[assign_op->left->value.pos].variable_value == POISON && value->type == kNumber) {
        lang_info->arr->var_array[assign_op->left->value.pos].variable_value = (int)value->value.number; //
    }
    
    return assign_op;
}

static LangNode_t *GetIf(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *if_node = NULL, *tok = NULL;

    CHECK_EXPECTED_TOKEN(if_node, IsThatOperation(if_node, kOperationIf),);
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected '('\n"));
    
    LangNode_t *cond = GetExpression(lang_info, func_name);
    if (!cond) {
        fprintf(stderr, "SYNTAX_ERROR_IF: expected condition\n");
        return NULL;
    }

    LangNode_t *sign = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (IsThatOperation(sign, kOperationBE) || IsThatOperation(sign, kOperationB) || IsThatOperation(sign, kOperationAE) 
            || IsThatOperation(sign, kOperationA) || IsThatOperation(sign, kOperationE) || IsThatOperation(sign, kOperationNE)) {
        (*lang_info->tokens_pos)++;
        LangNode_t *number = GetExpression(lang_info, func_name);
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

    LangNode_t *first = GetOp(lang_info, func_name);
    if (!first) {
        fprintf(stderr, "SYNTAX_ERROR_IF: empty if-body\n");
        return NULL;
    }

    LangNode_t *last = first;
    while (true) {
        LangNode_t *stmt = GetOp(lang_info, func_name);
        if (!stmt) break;
        
        last = NEWOP(kOperationThen, last, stmt);
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceClose), 
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected '}'\n"));

    if_node->left = cond;
    cond->parent = if_node;
    if_node->right = last;
    last->parent = if_node;

    LangNode_t *else_node = GetElse(lang_info, last, func_name);
    if (else_node) {
        if_node->right = else_node;
        else_node->parent = if_node;
    }

    return if_node;
}

static LangNode_t *GetElse(Language *lang_info, LangNode_t *if_node, LangNode_t *func_name) {
    assert(lang_info);
    assert(if_node);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *node = NULL, *tok = NULL, *last = NULL;

    CHECK_EXPECTED_TOKEN(node, IsThatOperation(node, kOperationElse), );
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceOpen), );

    while (true) {
        LangNode_t *stmt = GetOp(lang_info, func_name);
        if (!stmt) {
            break;
        }
        
        if (last) {
            last = NEWOP(kOperationThen, last, stmt);
        } else {
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

static LangNode_t *GetWhile(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *(lang_info->tokens_pos);
    
    LangNode_t *tok = NULL;
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationWhile), );
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen), );

    LangNode_t *cond_left = GetExpression(lang_info, func_name);
    LangNode_t *cond_sign = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    
    if (IsThatOperation(cond_sign, kOperationB) || IsThatOperation(cond_sign, kOperationBE) || 
        IsThatOperation(cond_sign, kOperationA) || IsThatOperation(cond_sign, kOperationAE) ||
        IsThatOperation(cond_sign, kOperationE) || IsThatOperation(cond_sign, kOperationNE)) {
        (*lang_info->tokens_pos)++;

        LangNode_t *cond_right = GetExpression(lang_info, func_name);
        cond_sign->left = cond_left;
        cond_sign->right = cond_right;
        cond_left->parent = cond_sign;
        cond_right->parent = cond_sign;
    } else {
        *(lang_info->tokens_pos) = save_pos;
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParClose), );
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceOpen), );

    LangNode_t *body = NULL, *last = NULL;
    while (true) {
        LangNode_t *stmt = GetOp(lang_info, func_name);
        if (!stmt) break;
        
        if (!body) {
            body = stmt;
        } else {
            body = NEWOP(kOperationThen, last, stmt);
        }
        last = body;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationBraceClose), );

    LangNode_t *while_tok = GetStackElem(lang_info->tokens, save_pos);
    while_tok->type = kOperation;
    while_tok->value.operation = kOperationWhile;
    while_tok->left = cond_sign;
    while_tok->right = body;
    
    return while_tok;
}

LangNode_t *GetPower(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *val = GetPrimary(lang_info, func_name);
    if (!val) {
        return NULL;
    }

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    while (IsThatOperation(node, kOperationPow)) {
        (*(lang_info->tokens_pos))++;
        LangNode_t *val2 = GetPower(lang_info, func_name);
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

static LangNode_t *GetVariableAddr(Language *lang_info, LangNode_t *func_name, ValCategory mode) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *addr_node = NULL;

    CHECK_EXPECTED_TOKEN(addr_node, IsThatOperation(addr_node, kOperationCallAddr) || IsThatOperation(addr_node, kOperationGetAddr), );

    LangNode_t *value = GetString(lang_info, func_name, mode);
    if (!value) {
        fprintf(stderr, "SYNTAX_ERROR_ADDR: wrong/no variable after addr sign.\n");
        return NULL;
    }
    
    addr_node->left = value;
    value->parent = addr_node;

    return addr_node;
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

static LangNode_t *GetString(Language *lang_info, LangNode_t *func_name, ValCategory val_cat) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (node && node->type == kVariable) {
        (*(lang_info->tokens_pos))++;
    } else {
        return NULL;
    }

    size_t var_pos = node->value.pos;
    size_t func_pos = func_name->value.pos;

    if (var_pos >= lang_info->arr->size || func_pos >= lang_info->arr->size) {
        fprintf(stderr, "SYNTAX_ERROR_STRING: invalid variable position %zu %zu\n", var_pos, func_pos);
        return NULL;
    }

    VariableInfo *var_info = &lang_info->arr->var_array[var_pos];
    VariableInfo *func_info = &lang_info->arr->var_array[func_pos];

    if (!var_info->func_made || !func_info->func_made || strcmp(var_info->func_made, func_info->func_made) != 0) {
        if (val_cat == klvalue && func_info->func_made) {

            if (var_info->func_made) {
                free(var_info->func_made);
            }
            var_info->func_made = strdup(func_info->func_made);

            if (!var_info->func_made) {
                perror("strdup failed");
                return NULL;
            }
        } else if (var_info->func_made && func_info->func_made) {
            fprintf(stderr, "SYNTAX_ERROR_STRING: usage of undeclared variable %s\n",
                var_info->variable_name ? var_info->variable_name : "unknown");
            return NULL;
        }
    }

    return node;
}

static LangNode_t *GetTernary(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);
    
    size_t save_pos = *lang_info->tokens_pos;
    
    LangNode_t *assign_op = GetAssignmentLValue(lang_info, func_name);

    LangNode_t *condition = GetExpression(lang_info, func_name);
    if (!condition) {
        return NULL;
    }
    
    LangNode_t *question_tok = NULL;
    CHECK_EXPECTED_TOKEN(question_tok, IsThatOperation(question_tok, kOperationTrueSeparator), );
    
    LangNode_t *true_expr = GetExpression(lang_info, func_name);
    if (!true_expr) {
        fprintf(stderr, "SYNTAX_ERROR_TERNARY: expected expression after '?'\n");
        *lang_info->tokens_pos = save_pos;
        return NULL;
    }
    
    LangNode_t *colon_tok = NULL;
    CHECK_EXPECTED_TOKEN(colon_tok, IsThatOperation(colon_tok, kOperationFalseSeparator), 
    fprintf(stderr, "SYNTAX_ERROR_TERNARY: expected ':' after true expression\n"));
    
    LangNode_t *false_expr = GetExpression(lang_info, func_name);
    if (!false_expr) {
        fprintf(stderr, "SYNTAX_ERROR_TERNARY: expected expression after ':'\n");
        *lang_info->tokens_pos = save_pos;
        return NULL;
    }
    
    LangNode_t *if_compare_node = NEWOP(kOperationE, NEWV(lang_info->arr->var_array[assign_op->left->value.pos].variable_name), condition);
    LangNode_t *ternary_root = NEWOP(kOperationTernary, assign_op, NULL);
    LangNode_t *else_node = NEWOP(kOperationElse, true_expr, false_expr);
    LangNode_t *if_node = NEWOP(kOperationIf, if_compare_node, else_node);
    
    assign_op->parent = ternary_root;
    assign_op->right = if_node;
    if_node->parent = assign_op;

    if_compare_node->parent = if_node;
    condition->parent = if_compare_node;
    else_node->parent = if_node;
    false_expr->parent = else_node;
    true_expr->parent = else_node;
    
    return ternary_root;
}

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
            if (IsThatOperation(token, kOperationParClose)) {
                (*(lang_info->tokens_pos))++;
            }
            break;
        }
        
        if (IsThatOperation(token, kOperationComma)) {
            (*(lang_info->tokens_pos))++;
            continue;
        }
        
        if (token->type == kVariable || token->type == kNumber || IsThatOperation(token, kOperationGetAddr)) {
            size_t save_pos = *lang_info->tokens_pos;
            (*(lang_info->tokens_pos))++;

            if (IsThatOperation(token, kOperationGetAddr)) {
                LangNode_t *next_token = NULL;
                CHECK_EXPECTED_TOKEN(next_token, next_token->type == kVariable, 
                    fprintf(stderr, "SYNTAX_ERROR_ADDR: wrong or no continuation after addr.\n"));
                token->left = next_token;
            }

            LangNode_t *arg = token;
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

static LangNode_t *ParseFunctionBody(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    lang_info->arr->var_array[func_name->value.pos].variable_value = lang_info->arr->var_array[func_name->value.pos].params_number;
    LangNode_t *body_root = NULL;
    
    while (true) {
        LangNode_t *stmt = GetOp(lang_info, func_name);
        if (!stmt) {
            break;
        }
        if (!body_root) {
            body_root = stmt;
        } else if (!body_root->right) {
            body_root->right = stmt;
        } else {
            body_root = NEWOP(kOperationThen, body_root, stmt);
        }
    }
    
    return body_root;
}

static LangNode_t *GetHLT(Language *lang_info) {
    assert(lang_info);

    size_t save_pos = *(lang_info->tokens_pos);
    LangNode_t *name = NULL, *tok = NULL;

    CHECK_EXPECTED_TOKEN(name, IsThatOperation(name, kOperationHLT), );
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationThen), );

    return name;
}

static LangNode_t *GetAssignmentLValue(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    
    LangNode_t *maybe_var = GetVariableAddr(lang_info, func_name, klvalue);
    if (!maybe_var) {
        *lang_info->tokens_pos = save_pos;
        maybe_var = GetString(lang_info, func_name, klvalue);
        if (!maybe_var) {
            *lang_info->tokens_pos = save_pos;
            return NULL;
        }
    }
           
    LangNode_t *assign_op = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!IsThatOperation(assign_op, kOperationIs)) {
        (*lang_info->tokens_pos) = save_pos;
        return NULL;
    }
    (*lang_info->tokens_pos)++;

    if (!lang_info->arr->var_array[maybe_var->value.pos].func_made 
            || strcmp(lang_info->arr->var_array[maybe_var->value.pos].func_made, lang_info->arr->var_array[func_name->value.pos].variable_name) != 0) {
        if (lang_info->arr->var_array[maybe_var->value.pos].func_made) {
            free(lang_info->arr->var_array[maybe_var->value.pos].func_made);
        }
        lang_info->arr->var_array[maybe_var->value.pos].func_made = strdup(lang_info->arr->var_array[func_name->value.pos].variable_name);
        lang_info->arr->var_array[func_name->value.pos].variable_value ++;
    }

    assign_op->left = maybe_var;
    maybe_var->parent = assign_op;
    
    return assign_op;
}

static LangNode_t *GetArrayAssignment(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    
    LangNode_t *declare_node = GetStackElem(lang_info->tokens, *lang_info->tokens_pos);
    if (IsThatOperation(declare_node, kOperationArrDecl)) {
        (*lang_info->tokens_pos)++;
    }

    LangNode_t *maybe_var = GetString(lang_info, func_name, klvalue);
    if (!maybe_var) {
        *lang_info->tokens_pos = save_pos;
        return NULL;
    }

    LangNode_t *bracket_node = NULL;
    CHECK_EXPECTED_TOKEN(bracket_node, IsThatOperation(bracket_node, kOperationBracketOpen), );

    LangNode_t *number = GetNumber(lang_info);
    if (!number) {
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no position or size of array.\n");
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(bracket_node, IsThatOperation(bracket_node, kOperationBracketClose), 
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no closing bracket.\n"););

    LangNode_t *is_node = NULL;
    CHECK_EXPECTED_TOKEN(is_node, IsThatOperation(is_node, kOperationIs), 
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no assignment while dealing with array.\n"));

    LangNode_t *rvalue_node = GetExpression(lang_info, func_name);
    if (!rvalue_node) {
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no assignment after '='.\n");
        return NULL;
    }
    
    is_node->left = NEWOP(kOperationArrPos, maybe_var, number);

    if (IsThatOperation(declare_node, kOperationArrDecl)) {
        declare_node->left = is_node;
        is_node->parent = declare_node;

        is_node->right = rvalue_node; // TODO: doesn't have to be only 0
        lang_info->arr->var_array[maybe_var->value.pos].variable_value = (int)number->value.number;
        lang_info->arr->var_array[func_name->value.pos].variable_value += (int)number->value.number; // TODO

        return declare_node;
    }
    is_node->right = rvalue_node;
    rvalue_node->parent = is_node;

    if (lang_info->arr->var_array[maybe_var->value.pos].variable_value == POISON 
        || lang_info->arr->var_array[maybe_var->value.pos].variable_value <= number->value.number) {
            fprintf(stderr, "SYNTAX_ERROR_ARRAY: usage of undeclared array or index out of range.\n");
        return NULL;
    }

    return is_node;

}

static LangNode_t *GetArrayElement(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;

    LangNode_t *maybe_var = GetString(lang_info, func_name, krvalue);
    if (!maybe_var) {
        *lang_info->tokens_pos = save_pos;
        return NULL;
    }

    LangNode_t *bracket_node = NULL;
    CHECK_EXPECTED_TOKEN(bracket_node, IsThatOperation(bracket_node, kOperationBracketOpen), );

    LangNode_t *number = GetNumber(lang_info);
    if (!number) {
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no position or size of array.\n");
        return NULL;
    }

    if (lang_info->arr->var_array[maybe_var->value.pos].variable_value <= number->value.number) {
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: usage of undeclared array or index out of range.\n");
        return NULL;
    }

    CHECK_EXPECTED_TOKEN(bracket_node, IsThatOperation(bracket_node, kOperationBracketClose), 
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no closing bracket.\n"););
    
    return NEWOP(kOperationArrPos, maybe_var, number);
}

#undef NEWN


static bool CheckAndSetFunctionArgsNumber(Language *lang_info, LangNode_t *name_token, size_t cnt) {
    assert(lang_info);
    assert(name_token);

    VariableInfo *variable = &lang_info->arr->var_array[name_token->value.pos];
    
    if (variable->params_number != (int)cnt) {
        if (variable->params_number == POISON) {
            variable->params_number = (int)cnt;
        } else {
            fprintf(stderr, "Number of function arguments is not the same as in its first mention: expected %d, got %zu\n", 
                variable->params_number, cnt);
            return false;
        }
    }
    
    return true;
}