#include "Front-End/FSM_LexicalAnalysis.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/StackFunctions.h"
#include "Common/LanguageFunctions.h"

typedef enum {
    STATE_START,
    STATE_NUMBER,
    STATE_IDENTIFIER,
} LexerState;

typedef struct {
    const char *src;
    size_t pos;
    size_t line;
    size_t col;
    size_t token_start;
    LexerState state;
} Lexer;

#define NEWN(num) NewNode(root, kNumber, (Value){.number = (num)}, NULL, NULL)
#define NEWV(name) NewVariable(root, name, Variable_Array)
#define NEWOP(op)  NewNode(root, kOperation, (Value){.operation = (op)}, NULL, NULL)

static void InitLexer(Lexer *lexer, const char *src);
static inline char Peek(const Lexer *lexer);
static inline char PeekNext(const Lexer *lexer);
static inline char Advance(Lexer *lexer);
static inline int CheckEnd(const Lexer *lexer);
static inline int IsWordChar(char c);
static void SkipWhitespaceAndComments(Lexer *lexer);
static OperationTypes FindOperator(const Lexer *lexer, size_t *matched_size);
static int ParseToken(Lexer *lexer, LangRoot *root, Stack_Info *tokens, size_t *cnt, VariableArr *Variable_Array);
static int ParseNumber(Lexer *lexer, LangRoot *root, Stack_Info *tokens, size_t *cnt);

size_t CheckAndReturn_fsm(LangRoot *root, const char **string, Stack_Info *tokens, VariableArr *Variable_Array) {
    assert(root);
    assert(string);
    assert(tokens);
    assert(Variable_Array);

    Lexer lexer = {};
    InitLexer(&lexer, *string);

    size_t cnt = 0;
    while(ParseToken(&lexer, root, tokens, &cnt, Variable_Array)) {
        continue;
    }

    *string = lexer.src + lexer.pos;
    return cnt;
}

static void InitLexer(Lexer *lexer, const char *src) {
    assert(lexer);
    assert(src);

    lexer->src = src;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->col = 1;
    lexer->state = STATE_START;
    lexer->token_start = 0;
}

static inline char Peek(const Lexer *lexer) {
    assert(lexer);

    return lexer->src[lexer->pos];
}

static inline char PeekNext(const Lexer *lexer) {
    assert(lexer);

    return lexer->src[lexer->pos + 1];
}

static inline char Advance(Lexer *lexer) {
    assert(lexer);

    char c = lexer->src[lexer->pos++];
    if (c == '\n') {
        lexer->line++;
        lexer->col = 1;
    } else {
        lexer->col++;
    }

    return c;
}

static inline int CheckEnd(const Lexer *lexer) {
    assert(lexer);

    return Peek(lexer) == '\0';
}

static inline int IsWordChar(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static void SkipWhitespaceAndComments(Lexer *lexer) {
    assert(lexer);

    while (true) {
        char c = Peek(lexer);

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            Advance(lexer);
            continue;
        }

        if (c == '/' && PeekNext(lexer) == '/') {
            while (!CheckEnd(lexer) && Peek(lexer) != '\n') {
                Advance(lexer);
            }
            continue;
        }

        if (c == '/' && PeekNext(lexer) == '*') {
            Advance(lexer);
            Advance(lexer);
            while (!CheckEnd(lexer)) {
                if (Peek(lexer) == '*' && PeekNext(lexer) == '/') {
                    Advance(lexer);
                    Advance(lexer);
                    break;
                }
                Advance(lexer);
            }
            continue;
        }

        break;
    }
}

static OperationTypes FindOperator(const Lexer *lexer, size_t *matched_size) {
    assert(lexer);
    assert(matched_size);

    OperationTypes result = kOperationNone;
    size_t best_size = 0;
    const char *src = lexer->src + lexer->pos;

    for (size_t i = 0; i < OP_TABLE_SIZE; i++) {
        const char *name = NAME_TYPES_TABLE[i].name_in_lang;
        if (!name || !name[0]) {
            continue;
        }

        size_t size = strlen(name);
        if (strncmp(src, name, size) != 0) {
            continue;
        }

        if (isalpha((unsigned char)name[0])) {
            char next = src[size];
            if (IsWordChar(next)) {
                continue;
            }
        }

        if (size > best_size) {
            best_size = size;
            result = NAME_TYPES_TABLE[i].type;
        }
    }

    if (result != kOperationNone) {
        *matched_size = best_size;
    }

    return result;
}

static int ParseToken(Lexer *lexer, LangRoot *root, Stack_Info *tokens, size_t *cnt, VariableArr *Variable_Array) {
    assert(lexer);
    assert(root);
    assert(tokens);
    assert(cnt);
    assert(Variable_Array);

   SkipWhitespaceAndComments(lexer);
    if (CheckEnd(lexer)) {
        return kSuccess;
    }

    lexer->token_start = lexer->pos;
    lexer->state = STATE_START;

    while (true) {
        char c = Peek(lexer);

        switch (lexer->state) {
        case STATE_START: {
            if (c == '-' && isdigit((unsigned char)lexer->src[lexer->pos + 1])) {
                lexer->state = STATE_NUMBER;
                Advance(lexer);
                Advance(lexer);
                break;
            }

            size_t op_len = 0;
            OperationTypes op = FindOperator(lexer, &op_len);

            if (op != kOperationNone) {
                StackPush(tokens, NEWOP(op), stderr);
                (*cnt)++;
                lexer->pos += op_len;
                lexer->col += op_len;
                return kFailure;
            }

            if (isdigit((unsigned char)c)) {
                lexer->state = STATE_NUMBER;
                Advance(lexer);
                break;
            }

            if (isalpha((unsigned char)c) || c == '_') {
                lexer->state = STATE_IDENTIFIER;
                Advance(lexer);
                break;
            }

            fprintf(stderr, "Lexer error [%zu:%zu]: '%c'\n", lexer->line, lexer->col, c);
            Advance(lexer);
            return kFailure;
        }

        case STATE_NUMBER:
            // ParseNumber(lexer, root, tokens, cnt);
            if (isdigit((unsigned char)c)) {
                Advance(lexer);
            } else {
                size_t len = lexer->pos - lexer->token_start;
                char *buf = (char *) calloc (MAX_TEXT_SIZE, sizeof(char));

                strncpy(buf, lexer->src + lexer->token_start, len);
                StackPush(tokens, NEWN(atoi(buf)), stderr);
                free(buf);
                (*cnt)++;
                return 1;
            }
            break;

        case STATE_IDENTIFIER:
            if (IsWordChar(c)) {
                Advance(lexer);
            } else {
                size_t len = lexer->pos - lexer->token_start;
                char *name = (char *) calloc (len + 1, 1);
                strncpy(name, lexer->src + lexer->token_start, len);
                StackPush(tokens, NEWV(name), stderr);
                (*cnt)++;
                free(name);
                return 1;
            }
            break;
        }
    }
}

static int ParseNumber(Lexer *lexer, LangRoot *root, Stack_Info *tokens, size_t *cnt) {
    assert(lexer);
    assert(root);
    assert(tokens);
    assert(cnt);

    char c = Peek(lexer);
    if (isdigit((unsigned char)c)) {
        Advance(lexer);
    } else {
        size_t len = lexer->pos - lexer->token_start;
        char *buf = (char *) calloc (MAX_TEXT_SIZE, sizeof(char));

        strncpy(buf, lexer->src + lexer->token_start, len);
        StackPush(tokens, NEWN(atoi(buf)), stderr);
        free(buf);
        (*cnt)++;
        return 1;
    }

    return 1;

    // size_t len = lexer->pos - lexer->token_start;
    // char *buf = (char *) calloc (MAX_TEXT_SIZE, sizeof(char));
    // if (!buf) return 0;

    // strncpy(buf, lexer->src + lexer->token_start, len);
    // buf[len] = '\0';

    // int value = atoi(buf);
    // StackPush(tokens, NEWN(value), stderr);
    // free(buf);
    // (*cnt)++;

    // return 1;
}