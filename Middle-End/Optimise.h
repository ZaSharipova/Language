#ifndef OPTIMISE_H_
#define OPTIMISE_H_

#include "Enums.h"
#include "Structs.h"

DifNode_t *OptimiseTree(DifRoot *root, DifNode_t *node, FILE *out, const char *main_var, VariableArr *arr);
DifNode_t *ConstOptimise(DifRoot *root, DifNode_t *node,  bool *has_change, VariableArr *arr);
DifNode_t *EraseNeutralElements(DifRoot *root, DifNode_t *node, bool *has_change);

#endif //OPTIMISE_H_