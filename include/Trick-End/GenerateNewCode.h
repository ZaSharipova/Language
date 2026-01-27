#ifndef GENERATE_NEW_CODE_H_
#define GENERATE_NEW_CODE_H_

#include "Common/Enums.h"
#include "Common/Structs.h"

void GenerateNewCodeFromAST(LangNode_t *node, FILE *out, VariableArr *arr, int indent);

#endif //GENERATE_NEW_CODE_H_