#include "Reverse-End/TreeToCode.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Front-End/Rules.h"
#include "Common/LanguageFunctions.h"
#include "Common/CommonFunctions.h"
#include "Reverse-End/TreeToCode.h"

#define PrintCodeNameFromTable(type) NAME_TYPES_TABLE[type].name_in_lang
#define DEFAULT_NAME_SIZE 32

typedef struct {
    char new_name[DEFAULT_NAME_SIZE];
} RenameEntry;

static void GenExpr(LangNode_t *node, FILE *out, VariableArr *arr);
static void GenThenChain(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenIf(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenTernary(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenWhile(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenFunctionDeclare(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenFunctionCall(LangNode_t *node, FILE *out, VariableArr *arr, int indent);

static RenameEntry *rename_table = NULL;
static size_t rename_table_size = 0;

static bool RandSpace(void);
static bool RandNewline(void);
static void MaybeSpace(FILE *out);
static void MaybeNewline(FILE *out);
static void GenerateMagicName(char *buffer, size_t buffer_size);
static void InitRenameTable(VariableArr *arr);
static const char *GetRenamedVar(size_t pos);
static void PrintIndent(FILE *out, int indent);

void GenerateNewCodeFromAST(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    static int flag_rename_initialized = 0;
    if (!flag_rename_initialized) {
        InitRenameTable(arr);
        flag_rename_initialized = 1;
    }

    if (!node) return;

    if (node->type == kOperation) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"

        switch (node->value.operation) {
            case kOperationIf:
                GenIf(node, out, arr, indent);
                MaybeNewline(out);
                return;

            case kOperationTernary:
                GenTernary(node, out, arr, indent);
                MaybeNewline(out);
                return;

            case kOperationWhile:
                GenWhile(node, out, arr, indent);
                MaybeNewline(out);
                return;

            case kOperationFunction:
                GenFunctionDeclare(node, out, arr, indent);
                MaybeNewline(out);
                return;

            case kOperationCall:
                GenFunctionCall(node, out, arr, indent);
                MaybeNewline(out);
                return;

            case kOperationThen:
                GenThenChain(node, out, arr, indent);
                MaybeNewline(out);
                return;

            case kOperationReturn:
                PrintIndent(out, indent);
                fprintf(out, "%s ", PrintCodeNameFromTable(kOperationReturn));
                MaybeSpace(out);
                if (node->left) {
                    GenExpr(node->left, out, arr);
                }
                fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
                MaybeNewline(out);
                MaybeNewline(out);
                MaybeNewline(out);
                return;

            case kOperationWrite:
                PrintIndent(out, indent);
                fprintf(out, "%s(", PrintCodeNameFromTable(kOperationWrite));
                MaybeSpace(out);
                GenExpr(node->left, out, arr);
                fprintf(out, ")%s\n", PrintCodeNameFromTable(kOperationThen));
                MaybeNewline(out);
                MaybeNewline(out);
                MaybeNewline(out);
                return;

            case kOperationWriteChar:
                PrintIndent(out, indent);
                fprintf(out, "%s(", PrintCodeNameFromTable(kOperationWriteChar));
                MaybeSpace(out);
                GenExpr(node->left, out, arr);
                fprintf(out, ")");
                fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
                MaybeNewline(out);
                MaybeNewline(out);
                MaybeNewline(out);
                return;

            case kOperationRead:
                PrintIndent(out, indent);
                fprintf(out, "%s(", PrintCodeNameFromTable(kOperationRead));
                MaybeSpace(out);
                GenExpr(node->left, out, arr);
                fprintf(out, ")%s\n", PrintCodeNameFromTable(kOperationThen));
                MaybeNewline(out);
                MaybeNewline(out);
                MaybeNewline(out);
                return;

            case kOperationHLT:
                PrintIndent(out, indent);
                fprintf(out, "%s%s\n", PrintCodeNameFromTable(kOperationHLT), PrintCodeNameFromTable(kOperationThen));
                return;

            case kOperationArrDecl:
                PrintIndent(out, indent);
                fprintf(out, "%s %s%s%d%s %s %d%s\n",
                    PrintCodeNameFromTable(kOperationArrDecl), GetRenamedVar(node->left->left->left->value.pos),
                    PrintCodeNameFromTable(kOperationBracketOpen), (int)node->left->left->right->value.number,
                    PrintCodeNameFromTable(kOperationBracketClose), PrintCodeNameFromTable(kOperationIs),
                    (int)node->left->right->value.number, PrintCodeNameFromTable(kOperationThen));
                return;

            default:
                break;
        }
        #pragma clang diagnostic pop
    }

    PrintIndent(out, indent);
    GenExpr(node, out, arr);
    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
}

static void GenThenChain(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    LangNode_t *stmt = node;

    while (IsThatOperation(stmt, kOperationThen)) {
        if (stmt->left) {
            GenerateNewCodeFromAST(stmt->left, out, arr, indent);
        }
        stmt = stmt->right;
    }

    if (stmt) {
        GenerateNewCodeFromAST(stmt, out, arr, indent);
    }
}

static void GenIf(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    LangNode_t *condition = node->left;
    LangNode_t *body = node->right;

    LangNode_t *then_branch = NULL;
    LangNode_t *else_branch = NULL;

    if (IsThatOperation(body, kOperationElse)) {
        then_branch = body->left;
        else_branch = body->right;
    } else {
        then_branch = body;
    }

    PrintIndent(out, indent);
    fprintf(out, "%s (", PrintCodeNameFromTable(kOperationIf));
    MaybeSpace(out);
    GenExpr(condition, out, arr);
    fprintf(out, ") %s\n", PrintCodeNameFromTable(kOperationBraceOpen));

    if (then_branch) {
        GenThenChain(then_branch, out, arr, indent + 1);
    }

    PrintIndent(out, indent);
    fprintf(out, "%s", PrintCodeNameFromTable(kOperationBraceClose));

    if (else_branch) {
        fprintf(out, " %s %s\n", PrintCodeNameFromTable(kOperationElse), PrintCodeNameFromTable(kOperationBraceOpen));
        GenThenChain(else_branch, out, arr, indent + 1);
        PrintIndent(out, indent);
        fprintf(out, "%s", PrintCodeNameFromTable(kOperationBraceClose));
    }

    fprintf(out, "\n");
}

static void GenWhile(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);
    
    PrintIndent(out, indent);
    fprintf(out, "%s (", PrintCodeNameFromTable(kOperationWhile));
    MaybeSpace(out);
    GenExpr(node->left, out, arr);
    fprintf(out, ") %s\n", PrintCodeNameFromTable(kOperationBraceOpen));

    if (node->right) {
        GenThenChain(node->right, out, arr, indent + 1);
        MaybeNewline(out);
    }

    PrintIndent(out, indent);
    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationBraceClose));
}

static void GenExpr(LangNode_t *node, FILE *out, VariableArr *arr) {
    assert(out);
    assert(arr);
    if (!node) return;

    switch (node->type) {
        case kNumber:
            fprintf(out, "%.0f", node->value.number);
            return;

        case kVariable:
            fprintf(out, "%s", GetRenamedVar(node->value.pos));
            return;

        case kOperation:
            if (IsThatOperation(node, kOperationArrPos)) {
                fprintf(out, "%s%s%d%s", GetRenamedVar(node->left->value.pos), PrintCodeNameFromTable(kOperationBracketOpen), 
                    (int)node->right->value.number, PrintCodeNameFromTable(kOperationBracketClose));
                return;
            }
            break;

        default:
            fprintf(out, "UNKNOWN");
            return;
    }

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"
    switch (node->value.operation) {

        case kOperationAdd:
        case kOperationSub:
        case kOperationMul:
        case kOperationDiv:
        case kOperationPow: {
            const char *op = PrintCodeNameFromTable(node->value.operation);
            fprintf(out, "(");
            GenExpr(node->left, out, arr);
            fprintf(out, " %s ", op);
            GenExpr(node->right, out, arr);
            fprintf(out, ")");
            return;
        }

        case kOperationSQRT:
            fprintf(out, "%s(", PrintCodeNameFromTable(node->value.operation));
            GenExpr(node->left, out, arr);
            fprintf(out, ")");
            return;

        case kOperationB:
        case kOperationBE:
        case kOperationA:
        case kOperationAE:
        case kOperationE:
        case kOperationNE: {
            const char *op = "?";
            if      (node->value.operation == kOperationB)  op = PrintCodeNameFromTable(kOperationB);
            else if (node->value.operation == kOperationBE) op = PrintCodeNameFromTable(kOperationBE);
            else if (node->value.operation == kOperationA)  op = PrintCodeNameFromTable(kOperationA);
            else if (node->value.operation == kOperationAE) op = PrintCodeNameFromTable(kOperationAE);
            else if (node->value.operation == kOperationE)  op = PrintCodeNameFromTable(kOperationE);
            else if (node->value.operation == kOperationNE) op = PrintCodeNameFromTable(kOperationNE);

            //fprintf(out, "(");
            GenExpr(node->left, out, arr);
            fprintf(out, " %s ", op);
            MaybeSpace(out);
            GenExpr(node->right, out, arr);
            //fprintf(out, ")");
            return;
        }
        case kOperationIs:
            GenExpr(node->left, out, arr);
            fprintf(out, " %s ", PrintCodeNameFromTable(kOperationIs));
            MaybeSpace(out);
            GenExpr(node->right, out, arr);
            // MaybeNewline(out);
            return;

        case kOperationCall:
            GenExpr(node->left, out, arr);
            fprintf(out, "(");
            if (node->right) {
                GenExpr(node->right, out, arr);
            }
            fprintf(out, ")");
            return;

        case kOperationComma:
            GenExpr(node->left, out, arr);
            fprintf(out, ", ");
            GenExpr(node->right, out, arr);
            return;

        default:
            fprintf(out, "UNSUPPORTED_OP");
            return;
    }
    #pragma clang diagnostic pop
}

static void GenTernary(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    PrintIndent(out, indent);
    GenExpr(node->left->left, out, arr);
    fprintf(out, " %s ", PrintCodeNameFromTable(kOperationIs));

    LangNode_t *body = node->left->right;
    GenExpr(body->left->right, out, arr);

    fprintf(out, " %s ", PrintCodeNameFromTable(kOperationTrueSeparator));
    GenExpr(body->right->left, out, arr);

    fprintf(out, " %s ", PrintCodeNameFromTable(kOperationFalseSeparator));
    GenExpr(body->right->right, out, arr);
    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));

}

static void GenFunctionDeclare(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    LangNode_t *name = node->left;
    LangNode_t *pair = node->right;

    LangNode_t *args = NULL;
    LangNode_t *body = NULL;

    if (IsThatOperation(pair, kOperationThen)) {
        args = pair->left;
        body = pair->right;
    } else {
        body = pair;
    }

    PrintIndent(out, indent);
    fprintf(out, "\n%s ", PrintCodeNameFromTable(kOperationFunction));

    if (name) {
        GenExpr(name, out, arr);
    }

    fprintf(out, "(");
    if (args) {
        GenExpr(args, out, arr);
    }
    fprintf(out, ") %s\n", PrintCodeNameFromTable(kOperationBraceOpen));

    if (body) {
        GenThenChain(body, out, arr, indent + 1);
    }

    PrintIndent(out, indent);
    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationBraceClose));
}

static void GenFunctionCall(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    LangNode_t *name = node->left;
    LangNode_t *args = node->right;

    PrintIndent(out, indent);

    if (name) {
        GenExpr(name, out, arr);
    }

    fprintf(out, "(");
    if (args) {
        GenExpr(args, out, arr);
    }
    fprintf(out, ")%s\n", PrintCodeNameFromTable(kOperationThen));
}

static const char *magic_prefixes[] = {
    "arcane", "crystal", "dragon", "elder", "ethereal", "forgotten",
    "hidden", "lost", "mystic", "phantom", "shadow", "spirit",
    "stellar", "tempest", "void", "whispering", "ancient"
};

static const char *magic_suffixes[] = {
    "blood", "bone", "breath", "claw", "core", "essence", "eye",
    "fang", "flame", "heart", "leaf", "light", "mist", "moon",
    "scale", "shadow", "shard", "shell", "sky", "soul", "star",
    "stone", "thorn", "veil", "wind", "wing"
};

static const char *magic_connectors[] = {
    "_of_", "_", ""
};

static bool RandSpace(void) {
    return rand() % 3 != 0;
}

static bool RandNewline(void) {
    return rand() % 4 == 0;
}

static void MaybeSpace(FILE *out) {
    assert(out);

    if (RandSpace()) fputc(' ', out);
}

static void MaybeNewline(FILE *out) {
    assert(out);

    if (RandNewline()) fputc('\n', out);
}

static void GenerateMagicName(char *buffer, size_t buffer_size) {
    assert(buffer);

    int prefix_idx = (size_t)rand() % (sizeof(magic_prefixes) / sizeof(magic_prefixes[0]));
    int suffix_idx = (size_t)rand() % (sizeof(magic_suffixes) / sizeof(magic_suffixes[0]));
    int connector_idx = (size_t)rand() % (sizeof(magic_connectors) / sizeof(magic_connectors[0]));
    
    snprintf(buffer, buffer_size, "%s%s%s",
        magic_prefixes[prefix_idx],
        magic_connectors[connector_idx],
        magic_suffixes[suffix_idx]);
}

static void InitRenameTable(VariableArr *arr) {
    assert(arr);

    srand((unsigned int)time(NULL));
    
    rename_table_size = arr->size;
    rename_table = (RenameEntry *) calloc (rename_table_size, sizeof(RenameEntry));
    assert(rename_table);

    for (size_t i = 0; i < rename_table_size; i++) {
        if (rand() % 2 == 0) {
            GenerateMagicName(rename_table[i].new_name, sizeof(rename_table[i].new_name));
        } else {
            char random_name[8] = {};
            for (int j = 0; j < 6; j++) {
                random_name[j] = 'a' + (rand() % 26);
            }
            random_name[6] = '\0';
            snprintf(rename_table[i].new_name, sizeof(rename_table[i].new_name), "%s", random_name);
        }
    }
}

static const char *GetRenamedVar(size_t pos) {
    assert(rename_table);
    assert(pos < rename_table_size);

    return rename_table[pos].new_name;
}

static void PrintIndent(FILE *out, int indent) {
    assert(out);

    for (int i = 0; i < indent; i++) {
        fputc('\t', out);
    }
}