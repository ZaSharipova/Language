#include "Front-End/LexicalAnalysis.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/StackFunctions.h"
#include "Common/LanguageFunctions.h"
#include "Front-End/Rules.h"

static bool SkipComment(const char **string);
static void SkipSpaces(const char **string);
static void SkipEmptyLines(const char **string);
static bool ParseNumberToken(Language *lang_info, const char **string, size_t *cnt);
static bool ParseStringToken(Language *lang_info, const char **string, size_t *cnt);

#define NEWN(num) NewNode(lang_info, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define NEWV(name) NewVariable(lang_info, name)
#define NEWOP(op, left, right) NewNode(lang_info, kOperation, (Value){ .operation = (op)}, left, right) 

#define CodeNameFromTable(type) NAME_TYPES_TABLE[type].name_in_lang

#define CHECK_SYMBOL_AND_PUSH(symbol_to_check, op_type)                \
    if (**string == symbol_to_check) {                                 \
        node = NEWOP(op_type, NULL, NULL);                             \
        if (!node) {                                                   \
            fprintf(stderr, "Error making new variable.\n");           \
            return NULL;                                               \
        }                                                              \
        cnt++;                                                         \
        (*string)++;                                                   \
        continue;                                                      \
    }

#define CHECK_STROKE_AND_PUSH(line_to_check, op_type)                     \
    if (strncmp(*string, line_to_check, strlen(line_to_check)) == 0) {    \
        node = NEWOP(op_type, NULL, NULL);                                \
        if (!node) {                                                      \
            fprintf(stderr, "Error making new variable.\n");              \
            return NULL;                                                  \
        }                                                                 \
        cnt++;                                                            \
        (*string) += strlen(line_to_check);                               \
        continue;                                                         \
    }

size_t CheckAndReturn(Language *lang_info, const char **string) {
    assert(lang_info);
    assert(string);

    size_t cnt = 0;
    LangNode_t *node = NULL;

    while (**string != '\0') {
        SkipEmptyLines(string);
        SkipSpaces(string);
        if (**string == '\0') break;

        if (SkipComment(string)) {
            continue;
        }

        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationBraceOpen),  kOperationBraceOpen);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationBraceClose), kOperationBraceClose);
        CHECK_SYMBOL_AND_PUSH('(', kOperationParOpen);
        CHECK_SYMBOL_AND_PUSH(')', kOperationParClose);

        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationBE),   kOperationBE);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationAE),   kOperationAE);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationE),    kOperationE);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationNE),   kOperationNE);

        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationB),    kOperationB);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationA),    kOperationA);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationIs),   kOperationIs);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationThen), kOperationThen);
        CHECK_SYMBOL_AND_PUSH(',', kOperationComma);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationAdd),  kOperationAdd);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationMul),  kOperationMul);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationDiv),  kOperationDiv);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationSub),  kOperationSub);
        CHECK_SYMBOL_AND_PUSH('^', kOperationPow);
        CHECK_SYMBOL_AND_PUSH('&', kOperationCallAddr);
        CHECK_SYMBOL_AND_PUSH('$', kOperationGetAddr);
        CHECK_SYMBOL_AND_PUSH('?', kOperationTrueSeparator);
        CHECK_SYMBOL_AND_PUSH(':', kOperationFalseSeparator);
        CHECK_SYMBOL_AND_PUSH('[', kOperationBracketOpen);
        CHECK_SYMBOL_AND_PUSH(']', kOperationBracketClose);
        CHECK_STROKE_AND_PUSH("voco", kOperationArrDecl);

        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationSin),       kOperationSin);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationSQRT),      kOperationSQRT);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationIf),        kOperationIf);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationElse),      kOperationElse);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationWhile),     kOperationWhile);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationWriteChar), kOperationWriteChar);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationWrite),     kOperationWrite);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationRead),      kOperationRead);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationFunction),  kOperationFunction);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationReturn),    kOperationReturn);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationHLT),       kOperationHLT);
        CHECK_STROKE_AND_PUSH(CodeNameFromTable(kOperationDraw),      kOperationDraw);

        if (ParseNumberToken(lang_info, string, &cnt)) {
            continue;
        }

        CHECK_SYMBOL_AND_PUSH('-', kOperationSub);

        if (ParseStringToken(lang_info, string, &cnt)) {
            continue;
        }

        fprintf(stderr, "Lexer has stopped in '%c' (0x%02x), cnt=%zu, due to the lack of appropriate token type.\n", **string, **string, cnt);

        return 0;
    }

    // fprintf(stderr, "%zu\n\n", Variable_Array->size);
    // for (size_t i = 0; i < Variable_Array->size; i++) {
    //     fprintf(stderr, "%s %d\n\n", Variable_Array->var_array[i].variable_name, Variable_Array->var_array[i].variable_value);
    // }
    lang_info->tokens->la_size = cnt;
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
        if (**string != '\0') {
            (*string) += 2;
        }
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


static bool ParseNumberToken(Language *lang_info, const char **string, size_t *cnt) {
    assert(lang_info);
    assert(string);
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

    if (has_minus) {
        value = -value;
    }

    LangNode_t *node = NEWN(value);
    if (!node) {
        fprintf(stderr, "Error making new variable.\n");
    }
    (*cnt)++;

    return true;
}

static bool ParseStringToken(Language *lang_info, const char **string, size_t *cnt) {
    assert(lang_info);
    assert(string);
    assert(cnt);

    if (!(isalnum(**string) || **string == '_'))
        return false;

    const char *name_start = *string;
    size_t len = 0;

    while (isalnum(**string) || **string == '_') {
        (*string)++;
        len++;
    }

    char *name = (char *) calloc (len + 1, 1);
    strncpy(name, name_start, len);
    name[len] = '\0';

    LangNode_t *node = NEWV(name); 
    if (!node) {
        fprintf(stderr, "Error making new variable.\n");
    }

    (*cnt)++;

    return true;
}