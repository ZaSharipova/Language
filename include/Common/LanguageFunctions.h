#ifndef LANGUAGE_FUNCTIONS_H_
#define LANGUAGE_FUNCTIONS_H_

#include "Common/Enums.h"
#include "Common/Structs.h"

DifErrors LangRootCtor(LangRoot *root);
DifErrors NodeCtor(LangNode_t **node, Value *value);
DifErrors DeleteNode(LangRoot *root, LangNode_t *node);
DifErrors TreeDtor(LangRoot *tree);

LangNode_t *NewNode(Language *lang_info, DifTypes type, Value value, LangNode_t *left, LangNode_t *right);
    
DifErrors InitArrOfVariable(VariableArr *arr, size_t capacity);
DifErrors ResizeArray(VariableArr *arr);
DifErrors DtorVariableArray(VariableArr *arr);
LangNode_t *NewVariable(Language *lang_info, char *variable, VariableArr *VariableArr);

DifErrors PrintAST(LangNode_t *node, FILE *file, VariableArr *arr, int indent);

#endif //LANGUAGE_FUNCTIONS_H_