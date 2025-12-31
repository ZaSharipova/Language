#ifndef TREE_TO_CODE_H_
#define TREE_TO_CODE_H_

#include "Enums.h"
#include "Structs.h"

void GenerateCodeFromAST(LangNode_t *node, FILE *out, VariableArr *arr, int indent);

#endif //TREE_TO_CODE_H_