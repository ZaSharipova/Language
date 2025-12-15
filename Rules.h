#ifndef RULES_H_
#define RULES_H_

#include <stdio.h>
#include <string.h>

#include "Enums.h"
#include "Structs.h"

#define CHECK_ERROR_RETURN(cond) \
    err = cond;                  \
    if (err != kSuccess) {       \
        return err;              \
    }

#define FILE_OPEN_AND_CHECK(file, filename, mode) \
    FILE *file = fopen(filename, mode);           \
    if (!file) {                                  \
        perror("Error opening file");             \
        return kErrorOpening;                     \
    }

#define INIT_DUMP_INFO(name)                                       \
    DumpInfo name = {};                                            \
    dump_info.filename_to_write_dump = "alldump.html";             \
    dump_info.file = fopen(dump_info.filename_to_write_dump, "w"); \
    dump_info.filename_to_write_graphviz = "output.txt";           \
    strcpy(dump_info.message, "Expression tree");

DifErrors ReadInfix(DifRoot *root, DumpInfo *dump_info, VariableArr *Variable_Array, const char *filename);
void DoBufRead(FILE *file, const char *filename, FileInfo *Info);

#endif //RULES_H_