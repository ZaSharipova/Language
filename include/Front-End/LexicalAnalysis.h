#ifndef LEXICAL_ANALYSIS_H_
#define LEXICAL_ANALYSIS_H_

#include "Common/Enums.h"
#include "Common/Structs.h"

size_t CheckAndReturn(LangRoot *root, const char **string, Stack_Info *tokens, VariableArr *Variable_Array);

#endif // LEXICAL_ANALYSIS_H_