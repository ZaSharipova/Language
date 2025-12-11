#include "Optimise.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>

#include "Enums.h"
#include "Structs.h"
#include "LanguageFunctions.h"
//#include "Differentiate.h"

#include "DoGraph.h"

static DifNode_t *AddOptimise(DifRoot *root, DifNode_t *node, bool *has_change);
static DifNode_t *SubOptimise(DifRoot *root, DifNode_t *node, bool *has_change);
static DifNode_t *MulOptimise(DifRoot *root, DifNode_t *node, bool *has_change);
static DifNode_t *DivOptimise(DifRoot *root, DifNode_t *node, bool *has_change);
static DifNode_t *PowOptimise(DifRoot *root, DifNode_t *node, bool *has_change);

static DifNode_t *CheckNodeAndConstOptimise(DifRoot *root, DifNode_t *node, bool *has_change, VariableArr *arr);
static DifNode_t *GetSubTree(DifRoot *root, DifNode_t *node, DifNode_t *delete_node, DifNode_t *to_main);

static bool IsThisNumber(DifNode_t *node, double number);
// static bool IsZero(DifNode_t *node);
// static bool IsOne(DifNode_t *node);
static bool IsNumber(DifNode_t *node);
static bool IsOperation(DifNode_t *node);

static double EvaluateExpression(DifNode_t *node, VariableArr *arr);

DifNode_t *OptimiseTree(DifRoot *root, DifNode_t *node, FILE *out, const char *main_var, VariableArr *arr) {
    assert(root);
    assert(node);
    assert(out);
    assert(main_var);
    assert(arr);

    bool has_change = true;

    while (has_change) {
        has_change = false;
        node = ConstOptimise(root, node, &has_change, arr); 
        if (has_change) {
            node->parent = NULL;
        }
        node = EraseNeutralElements(root, node, &has_change);
        if (has_change) {
            node->parent = NULL;
        }
    }

    node->parent = NULL;
    return node;
}

DifNode_t *ConstOptimise(DifRoot *root, DifNode_t *node, bool *has_change, VariableArr *arr) {
    assert(node);
    assert(has_change);
    assert(arr);

    node->right = CheckNodeAndConstOptimise(root, node->right, has_change, arr);
    node->left = CheckNodeAndConstOptimise(root, node->left, has_change, arr);

    if (IsNumber(node->left) && IsNumber(node->right)) {
        double ans = EvaluateExpression(node, arr);

        DeleteNode(root, node->left);
        DeleteNode(root, node->right);
        node->left = node->right = NULL;
        node->type = kNumber;
        node->value.number = ans;

        root->size -= 2;
        *has_change = true;
        return node;
    }

    return node;
}

DifNode_t *EraseNeutralElements(DifRoot *root, DifNode_t *node, bool *has_change) {
    assert(root);
    assert(node);
    assert(has_change);

    if (node->left) {
        node->left = EraseNeutralElements(root, node->left, has_change);
    }

    if (node->right) {
        node->right = EraseNeutralElements(root, node->right, has_change);
    }

    if ((!node->left || !node->right) || !IsOperation(node)) {
        return node;
    }

    OperationTypes operation = node->value.operation;

    if (operation == kOperationAdd) {
        return AddOptimise(root, node, has_change);
    }
    if (operation == kOperationSub) {
        return SubOptimise(root, node, has_change);
    } 
    if (operation == kOperationMul) {
        return MulOptimise(root, node, has_change);
    }
    if (operation == kOperationDiv) {
        return DivOptimise(root, node, has_change);
    }
    if (operation == kOperationPow) {
        return PowOptimise(root, node, has_change);
    }

    return node;
}

#define NEWN(num) NewNode(root, kNumber, (Value){ .number = (num)}, NULL, NULL)
#define MUL_(left, right) NewNode(root, kOperation, (Value){ .operation = kOperationMul}, left, right)

static DifNode_t *AddOptimise(DifRoot *root, DifNode_t *node, bool *has_change) {
    assert(root);
    assert(node);
    assert(has_change);

    if (IsThisNumber(node->left, 0)) {
        *has_change = true;
        return GetSubTree(root, node, node->left, node->right);
    }

    if (IsThisNumber(node->right, 0)) {
        *has_change = true;
        return GetSubTree(root, node, node->right, node->left);
    }

    return node;
}

static DifNode_t *SubOptimise(DifRoot *root, DifNode_t *node, bool *has_change) {
    assert(root);
    assert(node);
    assert(has_change);

    if (IsThisNumber(node->right, 0)) {
        *has_change = true;
        return GetSubTree(root, node, node->right, node->left);
    }

    if (IsThisNumber(node->left, 0)) {
        *has_change = true;
        DifNode_t *right = GetSubTree(root, node, node->left, node->right);

        // DifNode_t *negative_node = NEWN(-1.0);
        // DifNode_t *mul_node = MUL_(negative_node, right);
        // if (negative_node) negative_node->parent = mul_node; //
        // if (right) right->parent = mul_node;

        return MUL_(NEWN(-1.0), right);
    }

    return node;
}

static DifNode_t *MulOptimise(DifRoot *root, DifNode_t *node, bool *has_change) {
    assert(root);
    assert(node);
    assert(has_change);

    if (IsThisNumber(node->left, 1)) {
        *has_change = true;
        return GetSubTree(root, node, node->left, node->right);
    }

    if (IsThisNumber(node->right, 1)) {
        *has_change = true;
        return GetSubTree(root, node, node->right, node->left);
    }

    if (IsThisNumber(node->left, 0) || IsThisNumber(node->right, 0)) {
        DeleteNode(root, node);
        *has_change = true;

        return NEWN(0.0);
    }

    return node;
}

static DifNode_t *DivOptimise(DifRoot *root, DifNode_t *node, bool *has_change) {
    assert(root);
    assert(node);
    assert(has_change);

    if (IsThisNumber(node->right, 1)) {
        *has_change = true;
        return GetSubTree(root, node, node->right, node->left);
    }

    if (IsThisNumber(node->left, 0)) {
        DeleteNode(root, node);
        *has_change = true;

        return NEWN(0.0);
    }

    return node;
}

static DifNode_t *PowOptimise(DifRoot *root, DifNode_t *node, bool *has_change) {
    assert(root);
    assert(node);
    assert(has_change);

    if (IsThisNumber(node->left, 0)) {
        DeleteNode(root, node);

        *has_change = true;
        return NEWN(0.0);
    }

    if (IsThisNumber(node->right, 0)) {
        DeleteNode(root, node);

        *has_change = true;
        return NEWN(1);
    }

    if (IsThisNumber(node->right, 1)) {
        *has_change = true;
        
        return GetSubTree(root, node, node->right, node->left);
    }

    return node;
}
#undef NEWN
#undef MUL_

static DifNode_t *GetSubTree(DifRoot *root, DifNode_t *node, DifNode_t *delete_node, DifNode_t *to_main) {
    assert(root);
    assert(node);
    assert(delete_node);
    assert(to_main);
    
    DifNode_t *res = to_main;

    if (res) {
        res->parent = node->parent;
    }

    if (delete_node) {
        delete_node->parent = NULL;
        DeleteNode(root, delete_node);
    }

    to_main = NULL;
    free(node);

    return res;
}

static bool IsThisNumber(DifNode_t *node, double number) {
    if (!node) {
        return false;
    }

    return (node->type == kNumber && fabs(node->value.number - number) < eps);
}

// static bool IsOne(DifNode_t *node) {
//     if (!node) {
//         return false;
//     }

//     return (node->type == kNumber && fabs(node->value.number - 1) < eps);
// }

static bool IsNumber(DifNode_t *node) {
    if (!node) {
        return false;
    }

    return (node->type == kNumber);
}

static bool IsOperation(DifNode_t *node) {
    if (!node) {
        return false;
    }

    return (node->type == kOperation);
}

static DifNode_t *CheckNodeAndConstOptimise(DifRoot *root, DifNode_t *node, 
    bool *has_change, VariableArr *arr) {
    assert(has_change);
    assert(arr);

    if (node && !IsNumber(node)) {
        node = ConstOptimise(root, node, has_change, arr);
        if (!node) return NULL;
    }
    return node;
}

static double EvaluateExpression(DifNode_t *node, VariableArr *arr) {
    assert(node);
    assert(arr);

    if (node->type == kNumber) {
        return node->value.number;
    }
    if (node->type == kVariable) {
        return arr->var_array[node->value.pos].variable_value;
    }

    switch (node->value.operation) {
    case (kOperationAdd):
        return EvaluateExpression(node->left, arr) +
            EvaluateExpression(node->right, arr);
    case (kOperationSub):
        return EvaluateExpression(node->left, arr) -
            EvaluateExpression(node->right, arr);
    case (kOperationMul):
        return EvaluateExpression(node->left, arr) *
            EvaluateExpression(node->right, arr);
    case (kOperationDiv): {
        double right = EvaluateExpression(node->right, arr);
        if (fabs(right) < eps) {
            fprintf(stderr, "Division by zero.\n");
            return 0;
        }
        return EvaluateExpression(node->left, arr) / right;
    }
    case (kOperationPow):
        return pow(EvaluateExpression(node->left, arr),
                EvaluateExpression(node->right, arr));
    case (kOperationSin):
        return sin(EvaluateExpression(node->right, arr));
    case (kOperationCos):
        return cos(EvaluateExpression(node->right, arr));
    case (kOperationTg):
        return tan(EvaluateExpression(node->right, arr));
    case (kOperationLn):
        return log(EvaluateExpression(node->right, arr));
    case (kOperationArctg):
        return atan(EvaluateExpression(node->right, arr));
    case (kOperationSinh):
        return sinh(EvaluateExpression(node->right, arr));    
    case (kOperationCosh):
        return cosh(EvaluateExpression(node->right, arr));
    case (kOperationTgh):
        return tanh(EvaluateExpression(node->right, arr));

    case (kOperationNone):
    default:
        fprintf(stderr, "Unknown operation: %d.\n", node->value.operation);
        return 0;
    }
}