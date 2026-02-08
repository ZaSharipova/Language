#ifndef COMMON_FUNCTIONS_H_
#define COMMON_FUNCTIONS_H_

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/CommonFunctions.h"
#include <stdlib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"

void CleanupOnFileError(void *arg1, void *arg2, void *arg3);
#define INIT_EVERYTHING(root, Variable_Array, lang_info, token, dump_info)                    \
    LangErrors err = kSuccess;                                                                \
                                                                                              \
    LangRoot root = {};                                                                       \
    VariableArr Variable_Array = {};                                                          \
    CHECK_ERROR_RETURN(LangRootCtor(&root), NULL, &Variable_Array, &root);                    \
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 16), NULL, &Variable_Array, &root); \
                                                                                              \
    Language lang_info = {&root, NULL, NULL, &Variable_Array};                                \
    Stack_Info token = {};                                                                    \
    if (strcmp(#token, "tokens_no") != 1) {                                                   \
        CHECK_ERROR_RETURN(StackCtor(&token, 1, stderr),  NULL, &Variable_Array, &root);      \
        lang_info.tokens = &token;                                                            \
    }                                                                                         \
                                                                                              \
    INIT_DUMP_INFO(dump_info);                                                                \
    dump_info.tree = &root;

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

LangErrors ReadTreeAndParse(Language *lang_info, DumpInfo *dump_info, const char *filename_in);
bool IsThatOperation(LangNode_t *node, OperationTypes type);
void DoBufRead(FILE *file, const char *filename, FileInfo *Info);

#endif //COMMON_FUNCTIONS_H_