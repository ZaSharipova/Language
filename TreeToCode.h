#ifndef TREE_TO_CODE_H_
#define TREE_TO_CODE_H_

#include "Enums.h"
#include "Structs.h"

DifErrors ParseNodeFromString(const char *buffer, size_t *pos, LangNode_t *parent, LangNode_t **node_to_add, VariableArr *arr);
void GenerateCodeFromAST(LangNode_t *node, FILE *out, VariableArr *arr, int indent);

#endif //TREE_TO_CODE_H_