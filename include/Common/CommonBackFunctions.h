#ifndef COMMON_BACK_FUNCTIONS_H_
#define COMMON_BACK_FUNCTIONS_H_

#include "Common/Structs.h"
#include <stdio.h>

int FindVarPos(VariableArr *VariableArr, LangNode_t *node, AsmInfo *info);
void CleanPositions(VariableArr *arr);
int CountArgs(LangNode_t *args_node);

#endif // COMMON_BACK_FUNCTIONS_H_