#ifndef READ_TREE_H_
#define READ_TREE_H_

#include "Enums.h"
#include "Structs.h"

DifErrors ParseNodeFromString(const char *buffer, size_t *pos, LangNode_t *parent, LangNode_t **node_to_add, VariableArr *arr);

#endif //READ_TREE_H_