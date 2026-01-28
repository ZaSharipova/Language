#ifndef RULES_H_
#define RULES_H_

#include <stdio.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#define CHECK_ERROR_RETURN(cond, ...)                                                         \
    do {                                                                                      \
        err = (cond);                                                                         \
        if (err != kSuccess) {                                                                \
            void *args[] = {__VA_ARGS__};                                                     \
            int arg_count = sizeof(args) / sizeof(args[0]);                                   \
            if (arg_count >= 1 && args[0] != NULL) DtorVariableArray((VariableArr *)args[0]); \
            if (arg_count >= 2 && args[1] != NULL) TreeDtor((LangRoot *)args[1]);             \
            return err;                                                                       \
        }                                                                                     \
    } while (0)
#pragma GCC diagnostic pop

#define FILE_OPEN_AND_CHECK(file, filename, mode, ...) \
    FILE *file = fopen(filename, mode);                                                   \
    if (!file) {                                                                          \
        perror("Error opening file");                                                     \
        void *args[] = {__VA_ARGS__};                                                     \
        int arg_count = sizeof(args)/sizeof(args[0]);                                     \
        if (arg_count >= 1 && args[0] != NULL) DtorVariableArray((VariableArr *)args[0]); \
        if (arg_count >= 2 && args[1] != NULL) TreeDtor((LangRoot *)args[1]);             \
        return kErrorOpening;                                                             \
    }

#define INIT_DUMP_INFO(name)                                       \
    DumpInfo name = {};                                            \
    dump_info.filename_to_write_dump = "alldump.html";             \
    dump_info.file = fopen(dump_info.filename_to_write_dump, "w"); \
    dump_info.filename_to_write_graphviz = "output.txt";           \
    strcpy(dump_info.message, "Expression tree");

DifErrors ReadInfix(Language *root, DumpInfo *dump_info, const char *filename);
void DoBufRead(FILE *file, const char *filename, FileInfo *Info);

#endif //RULES_H_