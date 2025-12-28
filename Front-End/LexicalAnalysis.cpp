#include "Front-End/LexicalAnalysis.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Enums.h"
#include "Structs.h"
#include "StackFunctions.h"
#include "Front-End/LanguageFunctions.h"

static bool SkipComment(const char **string);
static void SkipSpaces(const char **string);
static void SkipEmptyLines(const char **string);
static bool ParseNumberToken(LangRoot *root, const char **string, Stack_Info *tokens, size_t *cnt);
static bool ParseStringToken(LangRoot *root, const char **string, Stack_Info *tokens, size_t *cnt, VariableArr *Variable_Array);

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define NEWV(name) NewVariable(root, name, Variable_Array)
#define NEWOP(op, left, right) NewNode(root, kOperation, (Value){ .operation = (op)}, left, right) 

#define CHECK_SYMBOL_AND_PUSH(symbol_to_check, op_type)                \
    if (**string == symbol_to_check) {                                 \
        StackPush(tokens, NEWOP(op_type, NULL, NULL), stderr);         \
        cnt++;                                                         \
        (*string)++;                                                   \
        continue;                                                      \
    }

#define CHECK_STROKE_AND_PUSH(line_to_check, op_type)                     \
    if (strncmp(*string, line_to_check, strlen(line_to_check)) == 0) {    \
        StackPush(tokens, NEWOP(op_type, NULL, NULL), stderr);            \
        cnt++;                                                            \
        (*string) += strlen(line_to_check);                               \
        continue;                                                         \
    }

size_t CheckAndReturn(LangRoot *root, const char **string, Stack_Info *tokens, VariableArr *Variable_Array) {
    assert(root);
    assert(string);
    assert(tokens);
    assert(Variable_Array);

    size_t cnt = 0;

    printf("%s\n", *string);
    while (**string != '\0') {

        //printf("DEBUG:::: %d\n", cnt);
        SkipEmptyLines(string);
        SkipSpaces(string);
        if (**string == '\0') break;

        if (SkipComment(string)) {
            continue;
        }

        CHECK_STROKE_AND_PUSH(BRACEOP, kOperationBraceOpen);
        CHECK_STROKE_AND_PUSH(BRACECL, kOperationBraceClose);
        CHECK_SYMBOL_AND_PUSH('(', kOperationParOpen);
        CHECK_SYMBOL_AND_PUSH(')', kOperationParClose);
        
        CHECK_STROKE_AND_PUSH(BEQ, kOperationBE);
        CHECK_STROKE_AND_PUSH(AE, kOperationAE);
        CHECK_STROKE_AND_PUSH(EQUAL, kOperationE);
        CHECK_STROKE_AND_PUSH("!=", kOperationNE);

        CHECK_STROKE_AND_PUSH(B, kOperationB);
        CHECK_STROKE_AND_PUSH(A, kOperationA);
        CHECK_STROKE_AND_PUSH(IS, kOperationIs);
        CHECK_STROKE_AND_PUSH(THEN, kOperationThen);
        CHECK_SYMBOL_AND_PUSH(',', kOperationComma);
        CHECK_STROKE_AND_PUSH(ADD, kOperationAdd);
        CHECK_STROKE_AND_PUSH(MUL, kOperationMul);
        CHECK_STROKE_AND_PUSH(DIV, kOperationDiv);
        CHECK_STROKE_AND_PUSH(SUB, kOperationSub);
        CHECK_SYMBOL_AND_PUSH('^', kOperationPow);

        CHECK_STROKE_AND_PUSH("sin", kOperationSin);
        CHECK_STROKE_AND_PUSH("sqrt", kOperationSQRT);
        CHECK_STROKE_AND_PUSH(IF, kOperationIf);
        CHECK_STROKE_AND_PUSH(ELSE, kOperationElse);
        CHECK_STROKE_AND_PUSH(WHILE, kOperationWhile);
        CHECK_STROKE_AND_PUSH(PRINTC, kOperationWriteChar);
        CHECK_STROKE_AND_PUSH(PRINT, kOperationWrite);
        CHECK_STROKE_AND_PUSH(SCANF, kOperationRead);
        CHECK_STROKE_AND_PUSH(DECLARE, kOperationFunction);
        CHECK_STROKE_AND_PUSH(RETURN, kOperationReturn);
        CHECK_STROKE_AND_PUSH(GOODBYE, kOperationHLT);

        if (ParseNumberToken(root, string, tokens, &cnt)) {
            continue;
        }

        CHECK_SYMBOL_AND_PUSH('-', kOperationSub);

        if (ParseStringToken(root, string, tokens, &cnt, Variable_Array)) {
            continue;
        }

        return 0;
    }

    // fprintf(stderr, "%zu\n\n", Variable_Array->size);
    // for (size_t i = 0; i < Variable_Array->size; i++) {
    //     fprintf(stderr, "%s %d\n\n", Variable_Array->var_array[i].variable_name, Variable_Array->var_array[i].variable_value);
    // }

    return cnt;
}

static bool SkipComment(const char **string) {
    assert(string);

    if ((*string)[0] == '/' && (*string)[1] == '/') {
        (*string) += 2;
        while (**string != '\n') {
            (*string)++;
        }
        return true;
    }

    if ((*string)[0] == '/' && (*string)[1] == '*') {
        (*string) += 2;
        while (**string != '\0' && !((*string)[0] == '*' && (*string)[1] == '/')) {
            (*string)++;
        }
        if (**string != '\0')
            (*string) += 2;
        return true;
    }

    return false;
}

static void SkipSpaces(const char **string) {
    assert(string);

    while (**string != '\0' && isspace((unsigned char)**string) && **string != '\n') {
        (*string)++;
    }
}

static void SkipEmptyLines(const char **string) {
    assert(string);

    for (;;) {
        const char *ptr = *string;

        while (*ptr != '\0' && isspace((unsigned char)*ptr) && *ptr != '\n') {
            ptr++;
        }

        if (*ptr == '\n') {
            ptr++;
            *string = ptr;
            continue;
        }

        break;
    }
}


static bool ParseNumberToken(LangRoot *root, const char **string, Stack_Info *tokens, size_t *cnt) {
    assert(root);
    assert(string);
    assert(tokens);
    assert(cnt);

    if (!(('0' <= **string && **string <= '9') || **string == '-'))
        return false;

    bool has_minus = false;
    if (**string == '-') {
        has_minus = true;
        (*string)++;

        if (!(**string >= '0' && **string <= '9')) {
            (*string)--;
            return false;
        }
    }

    int value = 0;
    do {
        value = 10 * value + (**string - '0');
        (*string)++;
    } while ('0' <= **string && **string <= '9');

    if (has_minus) value = -value;

    StackPush(tokens, NEWN(value), stderr);
    (*cnt)++;

    return true;
}

static bool ParseStringToken(LangRoot *root, const char **string, Stack_Info *tokens, size_t *cnt, VariableArr *Variable_Array) {
    assert(root);
    assert(string);
    assert(tokens);
    assert(cnt);
    assert(Variable_Array);

    if (!(isalnum(**string) || **string == '_'))
        return false;

    const char *name_start = *string;
    size_t len = 0;

    while (isalnum(**string) || **string == '_') {
        (*string)++;
        len++;
    }

    char *name = (char *)calloc(len + 1, 1);
    strncpy(name, name_start, len);
    name[len] = '\0';

    StackPush(tokens, NEWV(name), stderr);
    (*cnt)++;

    return true;
}
