#ifndef COMMON_FUNCTIONS_H_
#define COMMON_FUNCTIONS_H_

#include "Common/Enums.h"
#include "Common/Structs.h"

bool IsThatOperation(LangNode_t *node, OperationTypes type);
void DoBufRead(FILE *file, const char *filename, FileInfo *Info);

#endif //COMMON_FUNCTIONS_H_