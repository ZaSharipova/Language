#ifndef STRUCTS_H_
#define STRUCTS_H_

#include <stdio.h>

#include "Enums.h"

#define MAX_IMAGE_SIZE 60
#define MAX_TEXT_SIZE 120

typedef char* Dif_t;

struct VariableInfo {
    const char *variable_name;
    double variable_value;
};

union Value {
    OperationTypes operation;
    double number;
    VariableInfo *variable;
};

struct FileInfo {
    char *buf_ptr;
    size_t filesize;
};

struct DifNode_t {
    DifTypes type;
    union Value value;
    DifNode_t *parent;
    DifNode_t *left;
    DifNode_t *right;
};

struct DifRoot {
    DifNode_t *root;
    size_t size;
};

typedef struct DumpInfo {
    DifRoot *tree;
    const char *filename_to_write_dump;
    FILE *file;
    const char *filename_to_write_graphviz;
    const char *filename_dump_made;
    char message[MAX_IMAGE_SIZE];
    char *name;
    char *question;
    char image_file[MAX_TEXT_SIZE];
    size_t graph_counter;
    bool flag_new;

    enum DifErrors error;
} DumpInfo;

typedef struct {
    const char *name;
    OperationTypes type;
} OpEntry;

struct VariableArr {
    VariableInfo *var_array;
    size_t size;
    size_t capacity;
};

struct GraphOperation {
    const char *operation_name;
    const char *color;
};

struct Forest {
    size_t size;
    DifRoot *trees;
};

struct Stack_Info {
    DifNode_t **data;
    ssize_t size;
    ssize_t capacity;
};

#endif //STRUCTS_H_