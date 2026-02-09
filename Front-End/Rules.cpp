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

#define TRY_PARSE_FUNC_AND_RETURN_NULL(result, call, error_handler) \
    LangNode_t *result = NULL;                                            \
    do {                                                                  \
        (result) = (call);                                                \
        if (!(result)) {                                                  \
            error_handler;                                                \
            return NULL;                                                  \
        }                                                                 \
    } while (0)


#define DEFINE_SIMPLE_COMMAND_PARSER(func_name, op_type)                \
static LangNode_t *func_name(Language *lang_info) {                     \
    assert(lang_info);                                                  \
                                                                        \
    size_t save_pos = *(lang_info->tokens_pos);                         \
    LangNode_t *name = NULL, *tok = NULL;                               \
                                                                        \
    CHECK_EXPECTED_TOKEN(name, IsThatOperation(name, op_type), );       \
    CHECK_EXPECTED_TOKEN(tok,  IsThatOperation(tok, kOperationThen), ); \
                                                                        \
    return name;                                                        \
}


static LangNode_t *GetGoal(Language *lang_info);
static LangNode_t *GetAssignment(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetOp(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetStatementSequence(Language *lang_info, LangNode_t *func_name, size_t *save_pos);
static LangNode_t *GetFunctionDeclare(Language *lang_info);
static LangNode_t *GetFunctionCall(Language *lang_info);

static LangNode_t *GetWhile(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetIf(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetElse(Language *lang_info, LangNode_t *if_node, LangNode_t *func_name);
static LangNode_t *GetCondition(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetReturn(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetPrintf(Language *lang_info);
static LangNode_t *GetScanf(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetUnaryFunc(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetHLT(Language *lang_info);
static LangNode_t *GetDraw(Language *lang_info);

static LangNode_t *GetStatement(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetExpression(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetTerm(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetPrimary(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetPower(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetTernary(Language *lang_info, LangNode_t *func_name);

static LangNode_t *GetVariableAddr(Language *lang_info, LangNode_t *func_name, ValCategory mode);
static LangNode_t *GetNumber(Language *lang_info);
static LangNode_t *GetString(Language *lang_info, LangNode_t *func_name, ValCategory val_cat);
static bool SyncFuncMade(VariableArr *arr, size_t var_pos, size_t func_pos, ValCategory val_cat);
static LangNode_t *GetArrayElement(Language *lang_info, LangNode_t *func_name);

static LangNode_t *ParseFunctionArgs(Language *lang_info, size_t *cnt, LangNode_t *func_name);
static LangNode_t *ParseBody(Language *lang_info, LangNode_t *func_name);

static LangNode_t *GetArrayAssignment(Language *lang_info, LangNode_t *func_name);
static LangNode_t *GetAssignmentLValue(Language *lang_info, LangNode_t *func_name);
static LangNode_t *ParseAssignmentRValue(Language *lang_info, LangNode_t *func_name, LangNode_t *lvalue);
static LangNode_t *ParseAddrToken(Language *lang_info, LangNode_t *token);
static bool CheckAndSetFunctionArgsNumber(Language *lang_info, LangNode_t *name_token, size_t cnt);
static LangNode_t *CheckArrayPos(VariableArr *arr, LangNode_t *maybe_var, LangNode_t *number);
static LangNode_t *ParseSimpleAssignment(Language *lang_info, LangNode_t *func_name);
static bool CheckCompareSign(LangNode_t *sign);
static void ConnectParentAndChild(LangNode_t *parent, LangNode_t *child, ChildNode node_type);

LangErrors ReadInfix(Language *lang_info, DumpInfo *dump_info, const char *filename) {
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

static LangNode_t *GetGoal(Language *lang_info) { //
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

    size_t save_pos = (*lang_info->tokens_pos);
    LangNode_t *return_node = NULL;
    CHECK_EXPECTED_TOKEN(return_node, IsThatOperation(return_node, kOperationReturn), );

    CHECK_NULL_RETURN(node, GetExpression(lang_info, func_name));
    (*lang_info->tokens_pos)++; 

    ConnectParentAndChild(return_node, node, kleft);
    return return_node;
}

static LangNode_t *GetScanf(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = (*lang_info->tokens_pos);
    LangNode_t *read_node = NULL, *tok = NULL;

    CHECK_EXPECTED_TOKEN(read_node, IsThatOperation(read_node, kOperationRead),);
    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen), );
    
    TRY_PARSE_FUNC_AND_RETURN_NULL(node, GetString(lang_info, func_name, krvalue), 
        *(lang_info)->tokens_pos = (save_pos););

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParClose), 
        fprintf(stderr, "SYNTAX_ERROR_SCANF: no closing par.\n"));

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationThen), 
        fprintf(stderr, "SYNTAX_ERROR_SCANF: no ';'.\n"));

    ConnectParentAndChild(read_node, node, kleft); 
    return read_node;
}

static LangNode_t *GetStatement(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *stmt = NULL;
    TRY_PARSE_RETURN(stmt, GetAssignment(lang_info, func_name));

    LangNode_t *func_call = GetFunctionCall(lang_info); // do not always need ';', f.e. in printf
    if (func_call) {
        LangNode_t *tok = NULL;
        CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationThen), 
            fprintf(stderr, "SYNTAX_ERROR_STATEMENT: expected ';' after function call.\n"););

        return func_call;
    }
    
    return NULL;
}

static LangNode_t *GetOp(Language *lang_info, LangNode_t *func_name) {
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
    TRY_PARSE_RETURN(stmt, GetDraw  (lang_info));

    return GetStatementSequence(lang_info, func_name, &save_pos);
}

static LangNode_t *GetStatementSequence(Language *lang_info, LangNode_t *func_name, size_t *save_pos) {
    assert(lang_info);
    assert(func_name);
    assert(save_pos);

    LangNode_t *seq = NULL;
    while (true) {
        *save_pos = *lang_info->tokens_pos;
        
        LangNode_t *stmt = GetStatement(lang_info, func_name);
        if (!stmt) {
            (*lang_info->tokens_pos) = *save_pos;
            break;
        }

        seq = (!seq) ? stmt : NEWOP(kOperationThen, seq, stmt);
        
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
        LangNode_t *par = NULL, *printf_arg = NULL;

        CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParOpen),);
        CHECK_EXPECTED_TOKEN(printf_arg, printf_arg && (IsThisNodeType(printf_arg, kVariable) || IsThisNodeType(printf_arg, kNumber)),
            fprintf(stderr, "NO AVAILABLE ARGUMENT FOR PRINTF WRITTEN.\n"););
        CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParClose), );

        ConnectParentAndChild(print_node, printf_arg, kleft);
        (*lang_info->tokens_pos)++; 
        return print_node;
    }

    return NULL;
}

static LangNode_t *GetFunctionDeclare(Language *lang_info) {
    assert(lang_info);
    
    size_t save_pos = *(lang_info->tokens_pos);
    LangNode_t *func_node = NULL, *func_name = NULL;
    
    CHECK_EXPECTED_TOKEN(func_node, IsThatOperation(func_node, kOperationFunction), );
    CHECK_EXPECTED_TOKEN(func_name, IsThisNodeType(func_name, kVariable), );
    
    lang_info->arr->var_array[func_name->value.pos].type = kVarFunction;
    size_t cnt = 0;
    LangNode_t *args_root = ParseFunctionArgs(lang_info, &cnt, func_name);
    save_pos = (*lang_info->tokens_pos);

    if (!CheckAndSetFunctionArgsNumber(lang_info, func_name, cnt)) {
        return NULL;
    }
    
    lang_info->arr->var_array[func_name->value.pos].variable_value = lang_info->arr->var_array[func_name->value.pos].params_number;
    LangNode_t *body_root = ParseBody(lang_info, func_name);

    ConnectParentAndChild(func_node, func_name, kleft);
    func_node->right = NEWOP(kOperationThen, args_root, body_root);
    return func_node;
}

static LangNode_t *GetFunctionCall(Language *lang_info) {
    assert(lang_info);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *name_token = NULL;
    CHECK_EXPECTED_TOKEN(name_token, IsThisNodeType(name_token, kVariable), );
    save_pos++;

    size_t cnt = 0;
    LangNode_t *args_root = ParseFunctionArgs(lang_info, &cnt, name_token);
    if (save_pos >= *lang_info->tokens_pos) {
        return NULL;
    }

    if (!CheckAndSetFunctionArgsNumber(lang_info, name_token, cnt)) {
        return NULL;
    }

    return NEWOP(kOperationCall, name_token, args_root);
}

static LangNode_t *GetExpression(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    CHECK_NULL_RETURN(val, GetTerm(lang_info, func_name));

    lang_info->root->size++;
    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    while (IsThatOperation(node, kOperationAdd) || IsThatOperation(node, kOperationSub)) {
        (*lang_info->tokens_pos)++;

        LangNode_t *val2 = GetTerm(lang_info, func_name);
        if (!val2) {
            return val;
        }

        lang_info->root->size++;
        ConnectParentAndChild(node, val, kleft);
        ConnectParentAndChild(node, val2, kright);

        val = node;
        node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    }

    return val;
}

static LangNode_t *GetTerm(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    CHECK_NULL_RETURN(left, GetPower(lang_info, func_name));

    lang_info->root->size++;
    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    while (IsThatOperation(node, kOperationMul) || IsThatOperation(node, kOperationDiv)) {
        (*lang_info->tokens_pos)++; 

        LangNode_t *right = GetTerm(lang_info, func_name);
        if (!right) {
            return left;
        }
        ConnectParentAndChild(node, left, kleft);
        ConnectParentAndChild(node, right, kright);

        left = node;
        node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    }

    return left;
}

static LangNode_t *GetPrimary(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    CHECK_NULL_RETURN(node, GetStackElem(lang_info->tokens, *(lang_info->tokens_pos)));
    
    if (IsThatOperation(node, kOperationParOpen)) {
        (*lang_info->tokens_pos)++; 
        
        CHECK_NULL_RETURN(val, GetExpression(lang_info, func_name));
        
        size_t save_pos = (*lang_info->tokens_pos); 
        CHECK_EXPECTED_TOKEN(node, IsThatOperation(node, kOperationParClose), 
            fprintf(stderr, "SYNTAX_ERROR_P: expected ')'\n"););
        
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
    
    TRY_PARSE_FUNC_AND_RETURN_NULL(value, GetExpression(lang_info, func_name), 
        fprintf(stderr, "SYNTAX_ERROR_UNARY\n"););
    
    CHECK_EXPECTED_TOKEN(par, IsThatOperation(par, kOperationParClose),);
    ConnectParentAndChild(unary_func_name, value, kleft);

    return unary_func_name;
}

LangNode_t *GetAssignment(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *value = NULL;

    TRY_PARSE_RETURN(value, GetArrayAssignment(lang_info, func_name));
    TRY_PARSE_RETURN(value, GetTernary(lang_info, func_name));

    if (!value) {
        value = ParseSimpleAssignment(lang_info, func_name);
    }

    if (!value) {
        *lang_info->tokens_pos = save_pos;
        return NULL;
    }

    return value;
}
static LangNode_t *GetIf(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *if_node = NULL;

    CHECK_EXPECTED_TOKEN(if_node, IsThatOperation(if_node, kOperationIf),);
    CHECK_NULL_RETURN(cond, GetCondition(lang_info, func_name));

    LangNode_t *last = ParseBody(lang_info, func_name);
    ConnectParentAndChild(if_node, cond, kleft);
    ConnectParentAndChild(if_node, last, kright);

    LangNode_t *else_node = GetElse(lang_info, last, func_name);
    if (else_node) {
        ConnectParentAndChild(if_node, else_node, kright);
    }

    return if_node;
}

static LangNode_t *GetElse(Language *lang_info, LangNode_t *if_node, LangNode_t *func_name) {
    assert(lang_info);
    assert(if_node);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *node = NULL;

    CHECK_EXPECTED_TOKEN(node, IsThatOperation(node, kOperationElse), );
    CHECK_NULL_RETURN(last, ParseBody(lang_info, func_name));

    ConnectParentAndChild(node, if_node, kleft);
    ConnectParentAndChild(node, last, kright);
    lang_info->root->size++;

    return node;
}

static LangNode_t *GetCondition(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    LangNode_t *tok = NULL;

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF/WHILE: expected '('\n"));
    
    TRY_PARSE_FUNC_AND_RETURN_NULL(cond, GetExpression(lang_info, func_name),
        fprintf(stderr, "SYNTAX_ERROR_IF/WHILE: expected condition\n"););

    LangNode_t *sign = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (CheckCompareSign(sign)) {
        (*lang_info->tokens_pos)++;
        TRY_PARSE_FUNC_AND_RETURN_NULL(number, GetExpression(lang_info, func_name), 
            fprintf(stderr, "SYNTAX_ERROR_IF/WHILE: no expression in if written.\n"););

        ConnectParentAndChild(sign, cond, kleft);
        ConnectParentAndChild(sign, number, kright);
        cond = sign;
    }

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationParClose),
        fprintf(stderr, "%s", "SYNTAX_ERROR_IF: expected ')'\n"));
    
    return cond;
}

static LangNode_t *GetWhile(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *(lang_info->tokens_pos);
    LangNode_t *tok = NULL;

    CHECK_EXPECTED_TOKEN(tok, IsThatOperation(tok, kOperationWhile), );
    CHECK_NULL_RETURN(cond, GetCondition(lang_info, func_name));
    CHECK_NULL_RETURN(body, ParseBody(lang_info, func_name));

    ConnectParentAndChild(tok, cond, kleft);
    ConnectParentAndChild(tok, body, kright);
    return tok;
}

LangNode_t *GetPower(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    CHECK_NULL_RETURN(val, GetPrimary(lang_info, func_name));

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    while (IsThatOperation(node, kOperationPow)) {
        (*(lang_info->tokens_pos))++;
        CHECK_NULL_RETURN(val2, GetPower(lang_info, func_name));

        // node->type = kOperation;
        // node->value.operation = kOperationPow;
        ConnectParentAndChild(node, val, kleft);
        ConnectParentAndChild(node, val2, kright);

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

    TRY_PARSE_FUNC_AND_RETURN_NULL(value, GetString(lang_info, func_name, mode), 
        fprintf(stderr, "SYNTAX_ERROR_ADDR: wrong/no variable after addr sign.\n"););
    
    ConnectParentAndChild(addr_node, value, kleft);
    return addr_node;
}

static LangNode_t *GetNumber(Language *lang_info) {
    assert(lang_info);

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));

    if (IsThisNodeType(node, kNumber)) {
        (*(lang_info->tokens_pos))++;
        return node;
    }

    return NULL;
}

static LangNode_t *GetString(Language *lang_info, LangNode_t *func_name, ValCategory val_cat) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *node = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (IsThisNodeType(node, kVariable)) {
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

    if (!SyncFuncMade(lang_info->arr, var_pos, func_pos, val_cat)) {
        return NULL;
    }

    return node;
}

static bool SyncFuncMade(VariableArr *arr, size_t var_pos, size_t func_pos, ValCategory val_cat) {
    assert(arr);

    VariableInfo *var_info  = &arr->var_array[var_pos];
    VariableInfo *func_info = &arr->var_array[func_pos];

    if (!var_info->func_made || !func_info->func_made || strcmp(var_info->func_made, func_info->func_made) != 0) {
        if (val_cat == klvalue && func_info->func_made) {
            if (var_info->func_made) {
                free(var_info->func_made);
            }
            var_info->func_made = strdup(func_info->func_made);
            if (!var_info->func_made) {
                fprintf(stderr, "Error making strdup.\n");
                return false;
            }

        } else if (var_info->func_made && func_info->func_made) {
            fprintf(stderr, "SYNTAX_ERROR_STRING: usage of undeclared variable %s\n",
                var_info->variable_name ? var_info->variable_name : "unknown");
            return false;
        }
    }

    return true;
}

static LangNode_t *GetTernary(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);
    
    size_t save_pos = *lang_info->tokens_pos;
    
    CHECK_NULL_RETURN(assign_op, GetAssignmentLValue(lang_info, func_name));
    CHECK_NULL_RETURN(condition, GetExpression(lang_info, func_name));
    
    LangNode_t *question_tok = NULL;
    CHECK_EXPECTED_TOKEN(question_tok, IsThatOperation(question_tok, kOperationTrueSeparator), );
    
    TRY_PARSE_FUNC_AND_RETURN_NULL(true_expr, GetExpression(lang_info, func_name),
        fprintf(stderr, "SYNTAX_ERROR_TERNARY: expected expression after '?'\n"); *lang_info->tokens_pos = save_pos;);
    
    LangNode_t *colon_tok = NULL;
    CHECK_EXPECTED_TOKEN(colon_tok, IsThatOperation(colon_tok, kOperationFalseSeparator), 
        fprintf(stderr, "SYNTAX_ERROR_TERNARY: expected ':' after true expression\n"));
    
    TRY_PARSE_FUNC_AND_RETURN_NULL(false_expr, GetExpression(lang_info, func_name), 
        fprintf(stderr, "SYNTAX_ERROR_TERNARY: expected expression after ':'\n"); *lang_info->tokens_pos = save_pos;);
    
    LangNode_t *if_compare_node = NEWOP(kOperationE, NEWV(lang_info->arr->var_array[assign_op->left->value.pos].variable_name), condition);
    LangNode_t *ternary_root = NEWOP(kOperationTernary, assign_op, NULL);
    LangNode_t *else_node = NEWOP(kOperationElse, true_expr, false_expr);
    LangNode_t *if_node = NEWOP(kOperationIf, if_compare_node, else_node);
    
    ConnectParentAndChild(assign_op, if_node, kright);
    return ternary_root;
}

#undef ADD_
#undef SUB_
#undef MUL_
#undef DIV_
#undef POW_

static LangNode_t *ParseFunctionArgsRecursive(Language *lang_info, size_t *cnt, LangNode_t *func_name) {
    assert(lang_info);
    assert(cnt);

    CHECK_NULL_RETURN(token, GetStackElem(lang_info->tokens, *lang_info->tokens_pos));

    if (IsThatOperation(token, kOperationParClose)) {
        (*lang_info->tokens_pos)++;
        return NULL;
    }

    if (IsThatOperation(token, kOperationComma)) {
        (*lang_info->tokens_pos)++;
        return ParseFunctionArgsRecursive(lang_info, cnt, func_name);
    }

    CHECK_NULL_RETURN(expr, GetExpression(lang_info, func_name));

    token = expr;
    (*lang_info->tokens_pos)++;

    if (IsThatOperation(token, kOperationGetAddr)) {
        token = ParseAddrToken(lang_info, token);
        if (!token) return NULL;
    }

    (*cnt)++;
    LangNode_t *next_arg = ParseFunctionArgsRecursive(lang_info, cnt, func_name);
    if (next_arg) {
        return NEWOP(kOperationComma, token, next_arg);
    }

    return token;
}

static LangNode_t *ParseFunctionArgs(Language *lang_info, size_t *cnt, LangNode_t *func_name) {
    assert(lang_info);
    assert(cnt);

    LangNode_t *token = NULL;
    size_t save_pos = (*lang_info->tokens_pos);
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationParOpen), ); //

    return ParseFunctionArgsRecursive(lang_info, cnt, func_name);
}

static LangNode_t *ParseBody(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    LangNode_t *body_root = NULL, *token = NULL;
    size_t save_pos = *(lang_info->tokens_pos);

    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceOpen),
        fprintf(stderr, "%s", "SYNTAX_ERROR_FUNC: expected '{' after function declaration\n"));
    
    while (true) {
        LangNode_t *stmt = GetOp(lang_info, func_name);
        if (!stmt) {
            break;
        }

        if (!body_root) {
            body_root = stmt;
        } else if (!body_root->right && IsThatOperation(body_root, kOperationThen)) {
            body_root->right = stmt;
        } else {
            body_root = NEWOP(kOperationThen, body_root, stmt);
        }
    }
    
    CHECK_EXPECTED_TOKEN(token, IsThatOperation(token, kOperationBraceClose),
        fprintf(stderr, "SYNTAX_ERROR_FUNC: expected '}' at end of function body %zu.\n", *(lang_info->tokens_pos)));

    return body_root;
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
    
    LangNode_t *assign_op = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos)); // TODO:
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

    ConnectParentAndChild(assign_op, maybe_var, kleft);
    
    return assign_op;
}

static LangNode_t *ParseAssignmentRValue(Language *lang_info, LangNode_t *func_name, LangNode_t *lvalue) {
    assert(lang_info);
    assert(func_name);
    assert(lvalue);

    LangNode_t *tok  = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    LangNode_t *next = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos) + 1);

    if (tok && IsThatOperation(next, kOperationParOpen) && !IsThatOperation(tok, kOperationSQRT) && tok->type == kVariable) {
        return GetFunctionCall(lang_info);
    } else {
        return GetExpression(lang_info, func_name);
    }
}


static LangNode_t *GetArrayAssignment(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    
    LangNode_t *declare_node = GetStackElem(lang_info->tokens, *lang_info->tokens_pos);
    if (IsThatOperation(declare_node, kOperationArrDecl)) {
        (*lang_info->tokens_pos)++;
    }

    TRY_PARSE_FUNC_AND_RETURN_NULL(maybe_var, GetString(lang_info, func_name, klvalue),
        *lang_info->tokens_pos = save_pos;);

    LangNode_t *bracket_node = NULL;
    CHECK_EXPECTED_TOKEN(bracket_node, IsThatOperation(bracket_node, kOperationBracketOpen), );

    TRY_PARSE_FUNC_AND_RETURN_NULL(pos_node, GetTerm(lang_info, func_name),
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no position or size of array.\n"););

    CHECK_EXPECTED_TOKEN(bracket_node, IsThatOperation(bracket_node, kOperationBracketClose), 
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no closing bracket.\n"););

    LangNode_t *is_node = NULL;
    CHECK_EXPECTED_TOKEN(is_node, IsThatOperation(is_node, kOperationIs), 
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no assignment while dealing with array.\n"));

    TRY_PARSE_FUNC_AND_RETURN_NULL(rvalue_node, GetExpression(lang_info, func_name),
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no assignment after '='.\n"););
    
    is_node->left = NEWOP(kOperationArrPos, maybe_var, pos_node);

    if (IsThatOperation(declare_node, kOperationArrDecl)) {
        ConnectParentAndChild(declare_node, is_node, kleft);
        lang_info->arr->var_array[maybe_var->value.pos].variable_value = (int)pos_node->value.number;
        lang_info->arr->var_array[func_name->value.pos].variable_value += (int)pos_node->value.number; // TODO

        return declare_node;
    }

    ConnectParentAndChild(is_node, rvalue_node, kright);

    if (lang_info->arr->var_array[maybe_var->value.pos].variable_value == POISON 
        || lang_info->arr->var_array[maybe_var->value.pos].variable_value <= pos_node->value.number) {
            fprintf(stderr, "SYNTAX_ERROR_ARRAY: usage of undeclared array or index out of range.\n");
        return NULL;
    }

    return is_node;

}

static LangNode_t *GetArrayElement(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    size_t save_pos = *lang_info->tokens_pos;
    TRY_PARSE_FUNC_AND_RETURN_NULL(maybe_var, GetString(lang_info, func_name, krvalue), *lang_info->tokens_pos = save_pos;);

    LangNode_t *bracket_node = NULL;
    CHECK_EXPECTED_TOKEN(bracket_node, IsThatOperation(bracket_node, kOperationBracketOpen), );

    TRY_PARSE_FUNC_AND_RETURN_NULL(number, GetNumber(lang_info),
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no position or size of array.\n"););

    CHECK_NULL_RETURN(err, CheckArrayPos(lang_info->arr, maybe_var, number));

    CHECK_EXPECTED_TOKEN(bracket_node, IsThatOperation(bracket_node, kOperationBracketClose), 
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: no closing bracket.\n"););
    
    return NEWOP(kOperationArrPos, maybe_var, number);
}

#undef NEWN

static LangNode_t *CheckArrayPos(VariableArr *arr, LangNode_t *maybe_var, LangNode_t *number) {
    assert(arr);
    assert(maybe_var);
    assert(number);

    if (arr->var_array[maybe_var->value.pos].variable_value <= number->value.number) {
        fprintf(stderr, "SYNTAX_ERROR_ARRAY: usage of undeclared array or index out of range.\n");
        return NULL;
    }

    return maybe_var;
}

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

static void ConnectParentAndChild(LangNode_t *parent, LangNode_t *child, ChildNode node_type) {
    assert(parent);
    assert(child);

    child->parent = parent;
    if (node_type == kleft) {
        parent->left = child;
    } else {
        parent->right = child;
    }
}

static LangNode_t *ParseAddrToken(Language *lang_info, LangNode_t *token) {
    assert(lang_info);
    assert(token);

    if (!IsThatOperation(token, kOperationGetAddr)) {
        return token;
    }

    LangNode_t *next_token = GetStackElem(lang_info->tokens, *(lang_info->tokens_pos));
    if (!next_token || !IsThisNodeType(next_token, kVariable)) {
        fprintf(stderr, "SYNTAX_ERROR_ADDR: & must be followed by variable\n");
        return NULL;
    }

    (*lang_info->tokens_pos)++;
    token->left = next_token;
    return token;
}

static LangNode_t *ParseSimpleAssignment(Language *lang_info, LangNode_t *func_name) {
    assert(lang_info);
    assert(func_name);

    CHECK_NULL_RETURN(assign_op, GetAssignmentLValue(lang_info, func_name));
    CHECK_NULL_RETURN(value, ParseAssignmentRValue(lang_info, func_name, assign_op));

    ConnectParentAndChild(assign_op, value, kright);

    if (IsThisNodeType(assign_op->left, kVariable) && IsThisNodeType(value, kNumber) &&
            lang_info->arr->var_array[assign_op->left->value.pos].variable_value == POISON) {
        lang_info->arr->var_array[assign_op->left->value.pos].variable_value = (int)value->value.number;
    }

    return assign_op;
}

static bool CheckCompareSign(LangNode_t *sign) {
    if (sign && (IsThatOperation(sign, kOperationBE) || IsThatOperation(sign, kOperationB) || IsThatOperation(sign, kOperationAE) 
            || IsThatOperation(sign, kOperationA) || IsThatOperation(sign, kOperationE) || IsThatOperation(sign, kOperationNE))) {
        return true;
    }

    return false;
}
DEFINE_SIMPLE_COMMAND_PARSER(GetHLT,  kOperationHLT)
DEFINE_SIMPLE_COMMAND_PARSER(GetDraw, kOperationDraw)