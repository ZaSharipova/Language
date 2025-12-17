#ifndef STRUCTS_H_
#define STRUCTS_H_

#include <stdio.h>

#include "Enums.h"

#define MAX_IMAGE_SIZE 60
#define MAX_TEXT_SIZE 120
#define POISON -666

typedef char * Lang_t;

struct VariableInfo {
    VariableModes type;
    const char *variable_name;
    int variable_value;
    int pos_in_code;
};

union Value {
    OperationTypes operation;
    double number;

    size_t pos; //
};

struct FileInfo {
    char *buf_ptr;
    size_t filesize;
};

struct LangNode_t {
    DifTypes type;
    union Value value;
    LangNode_t *parent;
    LangNode_t *left;
    LangNode_t *right;
};

struct LangRoot {
    LangNode_t *root;
    size_t size;
};

typedef struct DumpInfo {
    LangRoot *tree;
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

struct Stack_Info {
    LangNode_t **data;
    ssize_t size;
    ssize_t capacity;
};


struct Language {
    LangRoot *root;
    Stack_Info *tokens;
    size_t *tokens_pos;
    VariableArr *arr;
};


#endif //STRUCTS_H_