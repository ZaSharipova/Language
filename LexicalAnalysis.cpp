#include "LexicalAnalysis.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Enums.h"
#include "Structs.h"
#include "StackFunctions.h"
#include "LanguageFunctions.h"

#define IF "if"
#define ELSE "else"
#define WHILE "while"

#define NEWN(num) NewNode(root, kNumber, ((Value){ .number = (num)}), NULL, NULL)
#define NEWV(name) NewVariable(root, name, Variable_Array)
#define NEWOP(op, left, right) NewNode(root, kOperation, (Value){ .operation = (op)}, left, right) 

size_t CheckAndReturn(DifRoot *root, const char **string, Stack_Info *tokens, VariableArr *Variable_Array) {
    assert(root);
    assert(string);
    assert(tokens);
    assert(Variable_Array);

    size_t cnt = 0;

    while (**string != '\0') {
        if (**string == '(') {
            StackPush(tokens, NEWOP(kOperationParOpen, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == ')') {
            StackPush(tokens, NEWOP(kOperationParClose, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == '{') {
            StackPush(tokens, NEWOP(kOperationBraceOpen, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == '}') {
            StackPush(tokens, NEWOP(kOperationBraceClose, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == '=') {
            StackPush(tokens, NEWOP(kOperationIs, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == ';') {
            StackPush(tokens, NEWOP(kOperationThen, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == ',') {
            StackPush(tokens, NEWOP(kOperationComma, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }        
        if (**string == '+') {
            StackPush(tokens, NEWOP(kOperationAdd, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == '-') {
            StackPush(tokens, NEWOP(kOperationSub, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == '*') {
            StackPush(tokens, NEWOP(kOperationMul, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == '/') {
            StackPush(tokens, NEWOP(kOperationDiv, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }
        if (**string == '^') {
            StackPush(tokens, NEWOP(kOperationPow, NULL, NULL), stderr);
            cnt++;
            (*string)++;
            continue;
        }

        if (strncmp(*string, "sin", strlen("sin")) == 0) {
            StackPush(tokens, NEWOP(kOperationSin, NULL, NULL), stderr);
            cnt++;
            (*string) += strlen("sin");
            continue;
        }
        if (strncmp(*string, IF, strlen(IF)) == 0) {
            StackPush(tokens, NEWOP(kOperationIf, NULL, NULL), stderr);
            cnt++;
            (*string) += strlen(IF);
            continue;
        }
        if (strncmp(*string, WHILE, strlen(WHILE)) == 0) {
            StackPush(tokens, NEWOP(kOperationWhile, NULL, NULL), stderr);
            cnt++;
            (*string) += strlen(WHILE);
            continue;
        }
        if (strncmp(*string, ELSE, strlen(ELSE)) == 0) {
            StackPush(tokens, NEWOP(kOperationElse, NULL, NULL), stderr);
            cnt++;
            (*string) += strlen(ELSE);
            continue;
        }

        if ('0' <= **string && **string <= '9') {
            int value = 0;
            do {
                value = 10 * value + (**string - '0');
                (*string)++;
            } while ('0' <= **string && **string <= '9');
            StackPush(tokens, NEWN(value), stderr);
            cnt++;
            continue;
        }

        if (isalnum(**string) || **string == '_') {
            const char *name_start = *string;
            size_t len = 0;
            while (isalnum(**string) || **string == '_') {
                (*string)++;
                len++;
            }

            char *name = (char *) calloc (len + 1, 1);
            strncpy(name, name_start, len);
            name[len] = '\0';

            StackPush(tokens, NEWV(name), stderr);

            fprintf(stderr, "%s\n", name);
            cnt++;
            continue;
        }


        if (isspace(**string)) {
            while (isspace(**string)) {
                (*string)++;
                continue;
            }
        }

        fprintf(stderr, "AAAAAA, SYNTAX_ERROR.");
        return 0;
    }
    fprintf(stderr, "%zu\n\n", Variable_Array->size);
    for (size_t i = 0; i < Variable_Array->size; i++) {
        fprintf(stderr, "%s %lf\n\n", Variable_Array->var_array[i].variable_name, Variable_Array->var_array[i].variable_value);
    }
    return cnt;
}