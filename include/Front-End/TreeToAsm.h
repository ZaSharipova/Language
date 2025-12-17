#ifndef TREE_TO_ASM_H_
#define TREE_TO_ASM_H_

#include "Enums.h"
#include "Structs.h"

void PrintProgram(FILE *file, LangNode_t *root, VariableArr *vars, int *ram_base);

#endif //TREE_TO_ASM_H_