#ifndef LANGUAGE_FUNCTIONS_H_
#define LANGUAGE_FUNCTIONS_H_

#include "Common/Enums.h"
#include "Common/Structs.h"

LangErrors LangRootCtor(LangRoot *root);
LangErrors NodeCtor(LangNode_t **node, Value *value);
LangErrors DeleteNode(LangRoot *root, LangNode_t *node);
LangErrors TreeDtor(LangRoot *tree);

LangNode_t *NewNode(Language *lang_info, DifTypes type, Value value, LangNode_t *left, LangNode_t *right);
    
LangErrors InitArrOfVariable(VariableArr *arr, size_t capacity);
LangErrors ResizeArray(VariableArr *arr);
LangErrors DtorVariableArray(VariableArr *arr);
LangNode_t *NewVariable(Language *lang_info, char *variable);

LangErrors PrintAST(LangNode_t *node, FILE *file, VariableArr *arr, int indent);

#endif //LANGUAGE_FUNCTIONS_H_