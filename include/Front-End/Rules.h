#ifndef RULES_H_
#define RULES_H_

#include <stdio.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/CommonFunctions.h"

// void CleanupOnFileError(void *arg1, void *arg2, void *arg3);
LangErrors ReadInfix(Language *root, DumpInfo *dump_info, const char *filename);
void DoBufRead(FILE *file, const char *filename, FileInfo *Info);

#endif //RULES_H_