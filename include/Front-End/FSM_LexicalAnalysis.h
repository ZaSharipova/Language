#ifndef LEXICAL_ANALYSIS1_H_
#define LEXICAL_ANALYSIS1_H_

#include "Common/Enums.h"
#include "Common/Structs.h"

size_t CheckAndReturn_fsm(Language *lang_info, const char **string, Stack_Info *tokens, VariableArr *Variable_Array);

#endif // LEXICAL_ANALYSIS1_H_