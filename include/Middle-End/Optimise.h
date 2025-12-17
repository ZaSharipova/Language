#ifndef OPTIMISE_H_
#define OPTIMISE_H_

#include "Enums.h"
#include "Structs.h"

LangNode_t *OptimiseTree(LangRoot *root, LangNode_t *node, FILE *out, const char *main_var, VariableArr *arr);
LangNode_t *ConstOptimise(LangRoot *root, LangNode_t *node,  bool *has_change, VariableArr *arr);
LangNode_t *EraseNeutralElements(LangRoot *root, LangNode_t *node, bool *has_change);

#endif //OPTIMISE_H_