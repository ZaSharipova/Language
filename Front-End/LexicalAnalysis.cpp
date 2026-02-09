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


static bool TryParseOperation(Language *lang_info, const char **string, bool *flag_found);
static bool SkipComment(const char **string);
static void SkipSpaces(const char **string);
static void SkipEmptyLines(const char **string);
static bool ParseNumberToken(Language *lang_info, const char **string);
static bool ParseStringToken(Language *lang_info, const char **string);

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
        lang_info->tokens->la_size ++;                                 \
        (*string)++;                                                   \
        continue;                                                      \
    }

#define CHECK_STROKE_AND_PUSH(line_to_check, op_type, flag_found)         \
    if ((strncmp(*string, line_to_check, strlen(line_to_check)) == 0)) {  \
        node = NEWOP(op_type, NULL, NULL);                                \
        if (!node) {                                                      \
            fprintf(stderr, "Error making new operation.\n");             \
            return NULL;                                                  \
        }                                                                 \
        lang_info->tokens->la_size ++;                                    \
        (*string) += strlen(line_to_check);                               \
        flag_found = true; \
    }

size_t CheckAndReturn(Language *lang_info, const char **string) {
    assert(lang_info);
    assert(string);

    LangNode_t *node = NULL;
    bool flag_found = false;

    while (**string != '\0') {
        SkipEmptyLines(string);
        SkipSpaces(string);
        if (**string == '\0') break;

        if (SkipComment(string)) {
            continue;
        }

        if (TryParseOperation(lang_info, string, &flag_found)) {
            flag_found = false;
            continue;
        }

        if (ParseNumberToken(lang_info, string)) {
            continue;
        }

        CHECK_SYMBOL_AND_PUSH('-', kOperationSub);

        if (ParseStringToken(lang_info, string)) {
            continue;
        }

        fprintf(stderr, "Lexer has stopped in '%c' (0x%02x), cnt=%zu, due to the lack of appropriate token type.\n", **string, **string, lang_info->tokens->la_size);

        return 0;
    }

    // fprintf(stderr, "%zu\n\n", Variable_Array->size);
    // for (size_t i = 0; i < Variable_Array->size; i++) {
    //     fprintf(stderr, "%s %d\n\n", Variable_Array->var_array[i].variable_name, Variable_Array->var_array[i].variable_value);
    // }

    return lang_info->tokens->la_size;
}

static bool TryParseOperation(Language *lang_info, const char **string, bool *flag_found) {
    assert(lang_info);
    assert(string);
    assert(flag_found);

    *flag_found = false;
    LangNode_t *node = NULL;

    for (size_t i = 0; i < OP_TABLE_SIZE; i++) {
        CHECK_STROKE_AND_PUSH(CodeNameFromTable((OperationTypes)i), (OperationTypes)i, *flag_found);
        if (*flag_found) {
            return true;
        }
    }
    return false;
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

static bool ParseNumberToken(Language *lang_info, const char **string) {
    assert(lang_info);
    assert(string);

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

    lang_info->tokens->la_size ++;
    return true;
}

static bool ParseStringToken(Language *lang_info, const char **string) {
    assert(lang_info);
    assert(string);

    if (!(isalnum(**string) || **string == '_')) {
        return false;
    }

    const char *name_start = *string;
    size_t len = 0;

    while (isalnum(**string) || **string == '_') {
        (*string)++;
        len++;
    }

    char *name = (char *) calloc (len + 1, 1);
    if (!name) {
        fprintf(stderr, "Error making new name in calloc.\n");
        return false;
    }

    strncpy(name, name_start, len);
    name[len] = '\0';

    LangNode_t *node = NEWV(name); 
    if (!node) {
        fprintf(stderr, "Error making new variable.\n");
        return false;
    }

    lang_info->tokens->la_size ++;
    return true;
}