#ifndef LANGUAGE_FUNCTIONS_H_
#define LANGUAGE_FUNCTIONS_H_

#include "Enums.h"
#include "Structs.h"

DifErrors DifRootCtor(DifRoot *root);
DifErrors NodeCtor(DifNode_t **node, Value *value);
DifErrors DeleteNode(DifNode_t *node);
DifErrors TreeDtor(DifRoot *tree);

DifNode_t *NewNode(DifRoot *root, DifTypes type, Value value, DifNode_t *left, DifNode_t *right,
    VariableArr *Variable_Array);
    
DifErrors InitArrOfVariable(VariableArr *arr, size_t capacity);
DifErrors ResizeArray(VariableArr *arr);
DifErrors DtorVariableArray(VariableArr *arr);
DifNode_t *NewVariable(DifRoot *root, const char *variable, VariableArr *VariableArr);

#endif //LANGUAGE_FUNCTIONS_H_