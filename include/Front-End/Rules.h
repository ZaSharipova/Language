#ifndef RULES_H_
#define RULES_H_

#include <stdio.h>
#include <string.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/CommonFunctions.h"

void CleanupOnFileError(void *arg1, void *arg2, void *arg3);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#define CHECK_ERROR_RETURN(cond, arg1, arg2, arg3)                                            \
    do {                                                                                      \
        err = (cond);                                                                         \
        if (err != kSuccess) {                                                                \
            CleanupOnFileError(arg1, arg2, arg3);                                             \
            return err;                                                                       \
        }                                                                                     \
    } while (0)
#pragma GCC diagnostic pop

#define FILE_OPEN_AND_CHECK(file, filename, mode, arg1, arg2, arg3)       \
    FILE *file = fopen(filename, mode);                                   \
    if (!file) {                                                          \
        perror("Error opening file");                                     \
        CleanupOnFileError(arg1, arg2, arg3);                             \
        return kErrorOpening;                                             \
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