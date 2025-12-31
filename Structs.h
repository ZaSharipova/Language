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
    char *variable_name;
    int variable_value;
    int pos_in_code;
    char *func_made;
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

struct LangTable {
    const char *name_in_lang;
    const char *name_in_tree;
    OperationTypes type;
};

static const LangTable NAME_TYPES_TABLE [] = {
    {"augeo",        "+",      kOperationAdd},
    {"minuo",        "-",      kOperationSub},
    {"multiplico",   "*",      kOperationMul},
    {"divido",       "/",      kOperationDiv},
    {"^",            "^",      kOperationPow},
    {"sin",          "sin",    kOperationSin},
    {"cos",          "cos",    kOperationCos},
    {"tg",           "tg",     kOperationTg},
    {"ln",           "ln",     kOperationLn},
    {"arctg",        "arctg",  kOperationArctg},
    {"sh",           "sh",     kOperationSinh},
    {"ch",           "ch",     kOperationCosh},
    {"th",           "th",     kOperationTgh},

    {"magica",       "=",      kOperationIs},
    {"si",           "if",     kOperationIf},
    {"altius",       "else",   kOperationElse},
    {"perpetuum",    "while",  kOperationWhile},
    {"~~",           ";",      kOperationThen},
    {",",            ",",      kOperationComma},
    {"incantatio",   "func",   kOperationFunction},
    {"",             "call",   kOperationCall},
    {"reporto",      "return", kOperationReturn},

    {"inferior",     "<",      kOperationB},
    {"inferior_aut", "<=",     kOperationBE},
    {"superior",     ">",      kOperationA},
    {"superior_aut", ">=",     kOperationAE},
    {"aequalis",     "==",     kOperationE},
    {"!=",           "!=",     kOperationNE},

    {"sqrt",         "sqrt",   kOperationSQRT},

    {"revelatio",    "print",  kOperationWrite},
    {"printc",       "printc", kOperationWriteChar},
    {"augurio",      "scanf",  kOperationRead},

    {"(",            "(",      kOperationParOpen},
    {")",            ")",      kOperationParClose},
    {"|>",           "{",      kOperationBraceOpen},
    {"<|",           "}",      kOperationBraceClose},
    {"exit",         "exit",   kOperationHLT},
    {"",             "",       kOperationNone},
};
static const size_t OP_TABLE_SIZE = sizeof(NAME_TYPES_TABLE) / sizeof(NAME_TYPES_TABLE[0]);

#endif //STRUCTS_H_