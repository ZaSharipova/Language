#include "Reverse-End/TreeToCode.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Front-End/Rules.h"
#include "Front-End/LanguageFunctions.h"
#include "Common/CommonFunctions.h"

static void GenExpr(LangNode_t *node, FILE *out, VariableArr *arr);
static void GenThenChain(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenIf(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenWhile(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenFunctionDeclare(LangNode_t *node, FILE *out, VariableArr *arr, int indent);
static void GenFunctionCall(LangNode_t *node, FILE *out, VariableArr *arr, int indent);

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
                PrintIndent(out, indent);
                fprintf(out, "%s ", PrintCodeNameFromTable(kOperationReturn));
                if (node->left) {
                    GenExpr(node->left, out, arr);
                }

                fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
                return;

            case kOperationWrite:
                PrintIndent(out, indent);
                fprintf(out, "%s(", PrintCodeNameFromTable(kOperationWrite));
                GenExpr(node->left, out, arr);
                fprintf(out, ")");
                fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
                return;

            case kOperationRead:
                PrintIndent(out, indent);
                fprintf(out, "%s(", PrintCodeNameFromTable(kOperationRead));
                GenExpr(node->left, out, arr);
                fprintf(out, ")");
                fprintf(out, "%s\n", PrintCodeNameFromTable(kOperationThen));
                return;

            case kOperationHLT:
                PrintIndent(out, indent);
                fprintf(out, "%s%s\n", PrintCodeNameFromTable(kOperationHLT), PrintCodeNameFromTable(kOperationThen));
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
    assert(out);
    assert(arr);
    assert(node);

    LangNode_t *condition = node->left;
    LangNode_t *body      = node->right;

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
            const char *op = "?";
            if (node->value.operation == kOperationAdd) op = PrintCodeNameFromTable(kOperationAdd);
            else if (node->value.operation == kOperationSub) op = PrintCodeNameFromTable(kOperationSub);
            else if (node->value.operation == kOperationMul) op = PrintCodeNameFromTable(kOperationMul);
            else if (node->value.operation == kOperationDiv) op = PrintCodeNameFromTable(kOperationDiv);
            else if (node->value.operation == kOperationPow) op = PrintCodeNameFromTable(kOperationPow);

            if (node->left && node->left->type == kOperation &&
                GetOpPrecedence(node->left->value.operation) < GetOpPrecedence(node->value.operation)) {
                fprintf(out, "(");
                GenExpr(node->left, out, arr);
                fprintf(out, ")");
                
            } else {
                GenExpr(node->left, out, arr);
            }
            fprintf(out, " %s ", op);
            GenExpr(node->right, out, arr);
            return;
        }

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
            GenExpr(node->right, out, arr);
            //fprintf(out, ")");
            return;
        }

        case kOperationIs:
            GenExpr(node->left, out, arr);
            fprintf(out, " %s ", PrintCodeNameFromTable(kOperationIs));
            GenExpr(node->right, out, arr);
            return;

        case kOperationCall: //
            GenExpr(node->left, out, arr);
            fprintf(out, "(");
            if (node->right)
                GenExpr(node->right, out, arr);
            fprintf(out, ")");
            return;

        case kOperationComma:
            GenExpr(node->left, out, arr);
            fprintf(out, ", ");
            GenExpr(node->right, out, arr);
            return;


        // case kOperationCall:
        //     GenExpr(node->left, out, arr);
        //     fprintf(out, "(");
        //     if (node->right)
        //         GenExpr(node->right, out, arr);
        //     fprintf(out, ")");
        //     return;

        case kOperationSin:
        case kOperationCos:
        case kOperationTg:
        case kOperationLn:
        case kOperationArctg:
        case kOperationSinh:
        case kOperationCosh:
        case kOperationTgh:
        case kOperationSQRT: {
            const char *name = "func";
            if      (node->value.operation == kOperationSin)   name = PrintCodeNameFromTable(kOperationSin);
            else if (node->value.operation == kOperationCos)   name = PrintCodeNameFromTable(kOperationCos);
            else if (node->value.operation == kOperationTg)    name = PrintCodeNameFromTable(kOperationTg);
            else if (node->value.operation == kOperationLn)    name = PrintCodeNameFromTable(kOperationLn);
            else if (node->value.operation == kOperationArctg) name = PrintCodeNameFromTable(kOperationArctg);
            else if (node->value.operation == kOperationSinh)  name = PrintCodeNameFromTable(kOperationSinh);
            else if (node->value.operation == kOperationCosh)  name = PrintCodeNameFromTable(kOperationCosh);
            else if (node->value.operation == kOperationTgh)   name = PrintCodeNameFromTable(kOperationTgh);
            else if (node->value.operation == kOperationSQRT)  name = PrintCodeNameFromTable(kOperationSQRT);

            fprintf(out, "%s(", name);
            GenExpr(node->left, out, arr);
            fprintf(out, ")");
            return;
        }

        
        default:
            fprintf(out, "UNSUPPORTED_OP");
            return;
    }

    #pragma clang diagnostic pop
}