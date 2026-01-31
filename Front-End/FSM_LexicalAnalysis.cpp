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
    kStateStart,
    kStateNumber,
    kStateIdentifier,
} LexerState;

typedef struct {
    const char *src;
    size_t pos;
    size_t line;
    size_t col;
    size_t token_start;
    LexerState state;
} Lexer;

#define kContinue 2

#define NEWN(num) NewNode(lang_info, kNumber, (Value){.number = (num)}, NULL, NULL)
#define NEWV(name) NewVariable(lang_info, name)
#define NEWOP(op)  NewNode(lang_info, kOperation, (Value){.operation = (op)}, NULL, NULL)

static void InitLexer(Lexer *lexer, const char *src);
static inline char Peek(const Lexer *lexer);
static inline char PeekNext(const Lexer *lexer);
static inline char Advance(Lexer *lexer);
static inline int CheckEnd(const Lexer *lexer);
static inline int IsWordChar(char c);
static void SkipWhitespaceAndComments(Lexer *lexer);
static OperationTypes FindOperator(const Lexer *lexer, size_t *matched_size);
static int ParseToken(Lexer *lexer, Language *lang_info, size_t *cnt);

static int ProcessLexerState(Lexer *lexer, Language *lang_info, size_t *cnt);
static int HandleStartState(Lexer *lexer, Language *lang_info, size_t *cnt);
static int HandleOperator(Lexer *lexer, Language *lang_info, size_t *cnt, OperationTypes op, size_t op_len);
static int HandleNumberState(Lexer *lexer, Language *lang_info, size_t *cnt);
static int FinishNumberToken(Lexer *lexer, Language *lang_info, size_t *cnt);
static int HandleIdentifierState(Lexer *lexer, Language *lang_info, size_t *cnt);
static int FinishIdentifierToken(Lexer *lexer, Language *lang_info, size_t *cnt);

// static void DumpToken(const Language *lang_info, const LangNode_t *node, size_t i);
// void DumpAllTokens(const Language *lang_info) {
//     assert(lang_info);
//     assert(lang_info->tokens);

//     Stack_Info *stk = lang_info->tokens;

//     fprintf(stderr, "\n===== TOKENS DUMP (%zu) =====\n", stk->la_size);

//     for (size_t i = 0; i < stk->la_size; ++i) {
//         LangNode_t *node = stk->data[i];
//         if (!node) {
//             fprintf(stderr, "[%zu] NULL\n", i);
//             continue;
//         }

//         DumpToken(lang_info, node, i);
//     }

//     fprintf(stderr, "===== END TOKENS =====\n\n");
// }
// #define PrintCodeNameFromTable(type) NAME_TYPES_TABLE[type].name_in_lang
// static void DumpToken(const Language *lang_info, const LangNode_t *node, size_t i) {
//     assert(node);

//     fprintf(stderr, "[%zu] ", i);

//     switch (node->type) {
//         case kNumber:
//             fprintf(stderr, "NUMBER    %d\n", node->value.number);
//             break;

//         case kVariable:
//             fprintf(stderr, "VARIABLE  %s\n", lang_info->arr->var_array[node->value.pos].variable_name);
//             break;

//         case kOperation:
//             fprintf(stderr, "OPERATOR  %s (%d)\n",
//                     PrintCodeNameFromTable(node->value.operation),
//                     node->value.operation);
//             break;

//         default:
//             fprintf(stderr, "UNKNOWN\n");
//             break;
//     }
// }

size_t CheckAndReturn_fsm(Language *lang_info, const char **string) {
    assert(lang_info);
    assert(string);

    Lexer lexer = {};
    InitLexer(&lexer, *string);

    size_t cnt = 0;

    int result = 0;
    while ((result = ParseToken(&lexer, lang_info, &cnt)) > 0) {
        continue;
    }

    
    if (result < 0) {
        fprintf(stderr, "Lexer error\n");
    }
    
    *string = lexer.src + lexer.pos;
    lang_info->tokens->la_size = cnt;
    return cnt;
}

static void InitLexer(Lexer *lexer, const char *src) {
    assert(lexer);
    assert(src);

    lexer->src = src;
    lexer->pos = 0;
    lexer->line = 1;
    lexer->col = 1;
    lexer->state = kStateStart;
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

static int ParseToken(Lexer *lexer, Language *lang_info, size_t *cnt) {
    assert(lexer);
    assert(lang_info);
    assert(cnt);

    SkipWhitespaceAndComments(lexer);

    if (CheckEnd(lexer)) {
        return 0;
    }

    lexer->token_start = lexer->pos;
    lexer->state = kStateStart;

    while (true) {
        int res = ProcessLexerState(lexer, lang_info, cnt);

        if (res == kContinue) {
            continue;
        }

        if (res == kFailure) {
            return -1;
        }

        return 1;
    }
}

static int ProcessLexerState(Lexer *lexer, Language *lang_info, size_t *cnt) {
    assert(lexer);
    assert(lang_info);
    assert(cnt);

    while (true) {
        switch (lexer->state) {
            case kStateStart:
                return HandleStartState(lexer, lang_info, cnt);
                
            case kStateNumber:
                return HandleNumberState(lexer, lang_info, cnt);
                
            case kStateIdentifier:
                return HandleIdentifierState(lexer, lang_info, cnt);
                
            default:
                fprintf(stderr, "Unknown lexer state: %d.\n", lexer->state);
                return kFailure;
        }
    }
}

static int HandleStartState(Lexer *lexer, Language *lang_info, size_t *cnt) {
    assert(lexer);
    assert(lang_info);
    assert(cnt);

    char c = Peek(lexer);
    
    if (c == '-' && isdigit((unsigned char)lexer->src[lexer->pos + 1])) {
        lexer->state = kStateNumber;
        Advance(lexer);
        Advance(lexer);
        return kContinue;
    }
    
    size_t op_len = 0;
    OperationTypes op = FindOperator(lexer, &op_len);
    if (op != kOperationNone) {
        return HandleOperator(lexer, lang_info, cnt, op, op_len);
    }
    
    if (isdigit((unsigned char)c)) {
        lexer->state = kStateNumber;
        Advance(lexer);
        return kContinue;
    }
    
    if (isalpha((unsigned char)c) || c == '_') {
        lexer->state = kStateIdentifier;
        Advance(lexer);
        return kContinue;
    }
    
    fprintf(stderr, "Lexer error [%zu:%zu]: '%c'.\n", lexer->line, lexer->col, c);
    Advance(lexer);
    return kFailure;
}

static int HandleOperator(Lexer *lexer, Language *lang_info, size_t *cnt, OperationTypes op, size_t op_len) {
    assert(lexer);
    assert(lang_info);
    assert(cnt);

    LangNode_t *node = NEWOP(op);
    if (!node) {
        fprintf(stderr, "Error creating operator node.\n");
        return kFailure;
    }
    
    (*cnt)++;
    lexer->pos += op_len;
    lexer->col += op_len;
    return kSuccess;
}

static int HandleNumberState(Lexer *lexer, Language *lang_info, size_t *cnt) {
    assert(lexer);
    assert(lang_info);
    assert(cnt);

    char c = Peek(lexer);
    
    if (isdigit((unsigned char)c)) {
        Advance(lexer);
        return kContinue;
    }
    
    return FinishNumberToken(lexer, lang_info, cnt);
}

static int FinishNumberToken(Lexer *lexer, Language *lang_info, size_t *cnt) {
    assert(lexer);
    assert(lang_info);
    assert(cnt);

    size_t len = lexer->pos - lexer->token_start;
    
    char *buf = (char *) calloc (len + 1, sizeof(char));
    if (!buf) {
        fprintf(stderr, "Memory allocation failed for number buffer.\n");
        return kFailure;
    }
    
    strncpy(buf, lexer->src + lexer->token_start, len);
    
    int number = atoi(buf);
    LangNode_t *node = NEWN(number);
    if (!node) {
        fprintf(stderr, "Error creating number node.\n");
        free(buf);
        return kFailure;
    }
    
    free(buf);
    (*cnt)++;
    return kSuccess;
}

static int HandleIdentifierState(Lexer *lexer, Language *lang_info, size_t *cnt) {
    assert(lexer);
    assert(lang_info);
    assert(cnt);

    char c = Peek(lexer);
    
    if (IsWordChar(c)) {
        Advance(lexer);
        return kContinue;
    }
    
    return FinishIdentifierToken(lexer, lang_info, cnt);
}

static int FinishIdentifierToken(Lexer *lexer, Language *lang_info, size_t *cnt) {
    assert(lexer);
    assert(lang_info);
    assert(cnt);

    size_t len = lexer->pos - lexer->token_start;
    
    char *name = (char *) calloc (len + 1, 1);
    if (!name) {
        fprintf(stderr, "Memory allocation failed for identifier name.\n");
        return kFailure;
    }
    
    strncpy(name, lexer->src + lexer->token_start, len);
    
    LangNode_t *node = NEWV(name);
    if (!node) {
        fprintf(stderr, "Error creating variable node.\n");
        return kFailure;
    }
    
    (*cnt)++;
    return kSuccess;
}