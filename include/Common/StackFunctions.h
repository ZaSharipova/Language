#ifndef STACK_FUNCTIONS_H_
#define STACK_FUNCTIONS_H_

#include <stdio.h>

#include "Common/Enums.h"
#include "Common/Structs.h"

DifErrors StackCtor(Stack_Info *stk, ssize_t capacity, FILE *open_log_file);
DifErrors StackPush(Stack_Info *stk, LangNode_t *value, FILE *open_log_file);
DifErrors StackPop(Stack_Info *stk, LangNode_t **value, FILE *open_log_file);
DifErrors StackRealloc(Stack_Info *stk, FILE *open_log_file, Realloc_Mode realloc_type);
DifErrors StackDtor(Stack_Info *stk, FILE *open_log_file);
LangNode_t *GetStackElem(Stack_Info *stk, size_t pos);

#endif //STACK_FUNCTIONS_H_