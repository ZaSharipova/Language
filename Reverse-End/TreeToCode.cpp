#include "Reverse-End/TreeToCode.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Front-End/Rules.h"
#include "Common/LanguageFunctions.h"
#include "Common/CommonFunctions.h"

#define GEN_OP(op_name, func)               \
    const char *name = "?";                 \
    name = PrintCodeNameFromTable(op_name); \
    func(out, node, arr, name);                                   

static void GenExpr(LangNode_t *node, FILE *out, VariableArr *arr);
static void GenThenChain(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenIf(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenTernary(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenWhile(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenFunctionDeclare(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenFunctionCall(LangNode_t *node, FILE *out, VariableArr *arr, int indent);

static void GenReturn(FILE *out, LangNode_t *node, VariableArr *arr, int indent);
static void GenWrite(FILE *out, LangNode_t *node, VariableArr *arr, int indent);
static void GenWriteChar(FILE *out, LangNode_t *node, VariableArr *arr, int indent);
static void GenRead(FILE *out, LangNode_t *node, VariableArr *arr, int indent);
static void GenHlt(FILE *out, LangNode_t *node, VariableArr *arr, int indent);
static void GenDraw(FILE *out, LangNode_t *node, VariableArr *arr, int indent);
static void GenArrDecl(FILE *out, LangNode_t *node, VariableArr *arr, int indent);

static void GenUnaryOperation(FILE *out, LangNode_t *node, VariableArr *arr, const char *name);
static void GenBinaryOperation(FILE *out, LangNode_t *node, VariableArr *arr, const char *name);
static void GenExprWithPrecedence(LangNode_t *node, OperationTypes parent_op, FILE *out, VariableArr *arr);
static void GenCondition(FILE *out, LangNode_t *node, VariableArr *arr, const char *name);
static void GenCall(FILE *out, LangNode_t *node, VariableArr *arr);

#define PrintCodeNameFromTable(type) NAME_TYPES_TABLE[type].name_in_lang

static void PrintIndent(FILE *out, int indent) {
    assert(out);

    for (int i = 0; i < indent; i++) {
        fputc('\t', out);
    }
}

void GenerateCodeFromAST(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(out);
    assert(arr);
    if (!node) return;

    if (node->type == kOperation) {
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wswitch-enum"
        switch (node->value.operation) {

            case kOperationIf:
                GenIf(node, out, arr, indent);
                return;

            case kOperationTernary:
                GenTernary(node, out, arr, indent);
                return;

            case kOperationWhile:
                GenWhile(node, out, arr, indent);
                return;

            case kOperationFunction:
                GenFunctionDeclare(node, out, arr, indent);
                return;

            case kOperationCall:
                GenFunctionCall(node, out, arr, indent);
                return;

            case kOperationThen:
                GenThenChain(node, out, arr, indent);
                return;

            case kOperationReturn:
                GenReturn(out, node, arr, indent);
                return;

            case kOperationWrite:
                GenWrite(out, node, arr, indent);
                return;

            case kOperationWriteChar:
                GenWriteChar(out, node, arr, indent);
                return;

            case kOperationRead:
                GenRead(out, node, arr, indent);
                return;

            case kOperationHLT:
                GenHlt(out, node, arr, indent);
                return;

            case kOperationDraw:
                GenDraw(out, node, arr, indent);
                return;

            case kOperationArrDecl:
                GenArrDecl(out, node, arr, indent);
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

static void GenExpr(LangNode_t *node, FILE *out, VariableArr *arr) { //
    if (!node) return;
    assert(out);
    assert(arr);

    switch (node->type) {
        case kNumber:
            fprintf(out, "%.0f", node->value.number);
            return;

        case kVariable:
            fprintf(out, "%s", arr->var_array[node->value.pos].variable_name);
            return;

        case kOperation:
            if (IsThatOperation(node, kOperationArrPos)) {
                fprintf(out, "%s%s%d%s", 
                    arr->var_array[node->left->value.pos].variable_name, PrintCodeNameFromTable(kOperationBracketOpen),
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
            GEN_OP(node->value.operation, GenBinaryOperation);
            return;
        }

        case kOperationB:
        case kOperationBE:
        case kOperationA:
        case kOperationAE:
        case kOperationE:
        case kOperationNE: {
            GEN_OP(node->value.operation, GenCondition);
            return;
        }

        case kOperationIs:
            GenExpr(node->left, out, arr); //
            fprintf(out, " %s ", PrintCodeNameFromTable(kOperationIs));
            GenExpr(node->right, out, arr);
            return;

        case kOperationCall:
            GenCall(out, node, arr);
            return;

        case kOperationComma:
            GenExpr(node->left, out, arr);
            fprintf(out, ", ");
            GenExpr(node->right, out, arr);
            return;

        case kOperationSin:
        case kOperationCos:
        case kOperationTg:
        case kOperationLn:
        case kOperationArctg:
        case kOperationSinh:
        case kOperationCosh:
        case kOperationTgh:
        case kOperationSQRT: {
            GEN_OP(node->value.operation, GenUnaryOperation);
            return;
        }
        
        default:
            fprintf(out, "UNSUPPORTED_OP");
            return;
    }

    #pragma clang diagnostic pop
}

static void GenThenChain(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    LangNode_t *stmt = node;

    while (IsThatOperation(stmt, kOperationThen)) {
        if (stmt->left) {
            GenerateCodeFromAST(stmt->left, out, arr, indent);
        }

        stmt = stmt->right;
    }

    if (stmt) {
        GenerateCodeFromAST(stmt, out, arr, indent);
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

static void GenWhile(LangNode_t *node, FILE *out, VariableArr *arr, int indent) {
    assert(node);
    assert(out);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s (", PrintCodeNameFromTable(kOperationWhile));
    GenExpr(node->left, out, arr);
    fprintf(out, ") %s\n", PrintCodeNameFromTable(kOperationBraceOpen));

    if (node->right) {
        GenThenChain(node->right, out, arr, indent + 1);
    }

    PrintIndent(out, indent);
    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationBraceClose));
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

static int GetOpPrecedence(OperationTypes op) {

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wswitch-enum"

    switch (op) {
        case kOperationPow:     return 4;
        case kOperationMul:     return 3;
        case kOperationDiv:     return 3;
        case kOperationAdd:     return 2;
        case kOperationSub:     return 2;
        case kOperationB:       return 1;
        case kOperationBE:      return 1;
        case kOperationA:       return 1;
        case kOperationAE:      return 1;
        case kOperationE:       return 1;
        case kOperationNE:      return 1;
        default:                return 0;
    }

    #pragma clang diagnostic pop
}

static void GenReturn(FILE *out, LangNode_t *node, VariableArr *arr, int indent) {
    assert(out);
    assert(node);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s ", PrintCodeNameFromTable(kOperationReturn));
    if (node->left) {
        GenExpr(node->left, out, arr);
    }

    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
}

static void GenWrite(FILE *out, LangNode_t *node, VariableArr *arr, int indent) {
    assert(out);
    assert(node);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s(", PrintCodeNameFromTable(kOperationWrite));
    GenExpr(node->left, out, arr);
    fprintf(out, ")");
    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
}

static void GenWriteChar(FILE *out, LangNode_t *node, VariableArr *arr, int indent) {
    assert(out);
    assert(node);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s(", PrintCodeNameFromTable(kOperationWriteChar));
    GenExpr(node->left, out, arr);
    fprintf(out, ")");
    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
}

static void GenRead(FILE *out, LangNode_t *node, VariableArr *arr, int indent) {
    assert(out);
    assert(node);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s(", PrintCodeNameFromTable(kOperationRead));
    GenExpr(node->left, out, arr);
    fprintf(out, ")");
    fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
}

static void GenHlt(FILE *out, LangNode_t *node, VariableArr *arr, int indent) {
    assert(out);
    assert(node);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s%s\n", PrintCodeNameFromTable(kOperationHLT), PrintCodeNameFromTable(kOperationThen));
}

static void GenDraw(FILE *out, LangNode_t *node, VariableArr *arr, int indent) {
    assert(out);
    assert(node);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s%s\n", PrintCodeNameFromTable(kOperationDraw), PrintCodeNameFromTable(kOperationThen));
}

static void GenArrDecl(FILE *out, LangNode_t *node, VariableArr *arr, int indent) {
    assert(out);
    assert(node);
    assert(arr);

    PrintIndent(out, indent);
    fprintf(out, "%s %s%s%d%s %s %d%s\n", 
        PrintCodeNameFromTable(kOperationArrDecl), arr->var_array[node->left->left->left->value.pos].variable_name, 
        PrintCodeNameFromTable(kOperationBracketOpen), (int)node->left->left->right->value.number, 
        PrintCodeNameFromTable(kOperationBracketClose), PrintCodeNameFromTable(kOperationIs),  
        (int)node->left->right->value.number, PrintCodeNameFromTable(kOperationThen));
}

static void GenUnaryOperation(FILE *out, LangNode_t *node, VariableArr *arr, const char *name) {
    assert(out);
    assert(node);
    assert(arr);
    assert(name);

    fprintf(out, "%s(", name);
    GenExpr(node->left, out, arr);
    fprintf(out, ")");
}

static void GenBinaryOperation(FILE *out, LangNode_t *node, VariableArr *arr, const char *name) {
    assert(out);
    assert(node);
    assert(arr);
    assert(name);

    GenExprWithPrecedence(node->left, node->value.operation, out, arr);
    fprintf(out, " %s ", name);

    if (node->right && node->right->type == kOperation)  {
        fprintf(out, "(");
        GenExpr(node->right, out, arr);
        fprintf(out, ")");
    } else {
        GenExpr(node->right, out, arr);
    }
}

static void GenExprWithPrecedence(LangNode_t *node, OperationTypes parent_op, FILE *out, VariableArr *arr) {
    assert(node);
    assert(out);
    assert(arr);

    if (node && node->type == kOperation && GetOpPrecedence(node->value.operation) <= GetOpPrecedence(parent_op)) {
        fprintf(out, "(");
        GenExpr(node, out, arr);
        fprintf(out, ")");
    } else {
        GenExpr(node, out, arr);
    }
}

static void GenCondition(FILE *out, LangNode_t *node, VariableArr *arr, const char *name) {
    assert(out);
    assert(node);
    assert(arr);
    assert(name);

    GenExpr(node->left, out, arr);
    fprintf(out, " %s ", name);
    GenExpr(node->right, out, arr);
}

static void GenCall(FILE *out, LangNode_t *node, VariableArr *arr) {
    assert(out);
    assert(node);
    assert(arr);

    GenExpr(node->left, out, arr);
    fprintf(out, "(");

    if (node->right) {
        GenExpr(node->right, out, arr);
    }
    fprintf(out, ")");
}