#ifndef LANGUAGE_FUNCTIONS_H_
#define LANGUAGE_FUNCTIONS_H_

#include "Enums.h"
#include "Structs.h"

DifErrors DifRootCtor(DifRoot *root);
DifErrors NodeCtor(DifNode_t **node, Value *value);
DifErrors DeleteNode(DifRoot *root, DifNode_t *node);
DifErrors TreeDtor(DifRoot *tree);

DifNode_t *NewNode(DifRoot *root, DifTypes type, Value value, DifNode_t *left, DifNode_t *right,
    VariableArr *Variable_Array);
    
DifErrors InitArrOfVariable(VariableArr *arr, size_t capacity);
DifErrors ResizeArray(VariableArr *arr);
DifErrors DtorVariableArray(VariableArr *arr);
DifNode_t *NewVariable(DifRoot *root, const char *variable, VariableArr *VariableArr);

DifErrors PrintAST(DifNode_t *node, FILE *file);
DifErrors ParseNodeFromString(const char *buffer, size_t *pos, DifNode_t *parent, DifNode_t **node_to_add, VariableArr *arr);
void GenerateCodeFromAST(DifNode_t *node, FILE *out);
#endif //LANGUAGE_FUNCTIONS_H_