#include "Common/CommonFunctions.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "Common/Enums.h"
#include "Common/Structs.h"
#include "Common/StackFunctions.h"
#include "Common/LanguageFunctions.h"
#include "Common/ReadTree.h"

bool IsThatOperation(LangNode_t *node, OperationTypes type) {
    if (node && node->type == kOperation && node->value.operation == type) {
        return true;
    }

    return false;
}

static long long SizeOfFile(const char *filename) {
    assert(filename);

    struct stat stbuf = {};

    int err = stat(filename, &stbuf);
    if (err != kSuccess) {
        perror("stat() failed");
        return kErrorStat;
    }

    return stbuf.st_size;
}

static char *ReadToBuf(const char *filename, FILE *file, size_t filesize) {
    assert(filename);
    assert(file);

    char *buf_in = (char *) calloc (filesize + 2, sizeof(char));
    if (!buf_in) {
        fprintf(stderr, "ERROR while calloc.\n");
        return NULL;
    }

    size_t bytes_read = fread(buf_in, 1, filesize, file);

    char *buf_out = (char *) calloc (bytes_read + 1, 1);
    if (!buf_out) {
        fprintf(stderr, "ERROR while calloc buf_out.\n");
        free(buf_in);
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < bytes_read; i++) {
        buf_out[j++] = buf_in[i];
    }

    buf_out[j] = '\0';

    free(buf_in);

    return buf_out;
}

void DoBufRead(FILE *file, const char *filename, FileInfo *Info) {
    assert(file);
    assert(filename);
    assert(Info);

    Info->filesize = (size_t)SizeOfFile(filename) * 4;

    Info->buf_ptr = ReadToBuf(filename, file, Info->filesize);
    assert(Info->buf_ptr != NULL);
}

void CleanupOnFileError(void *arg1, void *arg2, void *arg3) {
    if (arg1 != NULL) StackDtor((Stack_Info *)arg1, stderr);
    if (arg2 != NULL) DtorVariableArray((VariableArr *)arg2);
    if (arg3 != NULL) TreeDtor((LangRoot *)arg3);
}

LangErrors ReadTreeAndParse(Language *lang_info, DumpInfo *dump_info, const char *filename_in) {
    assert(lang_info);
    assert(dump_info);
    assert(filename_in);

    FILE_OPEN_AND_CHECK(ast_file, filename_in, "r", NULL, lang_info->arr, lang_info->root);
    FileInfo info = {};
    DoBufRead(ast_file, filename_in, &info);
    fclose(ast_file);

    size_t pos = 0;
    LangNode_t *tree = NULL;

    LangErrors err = kSuccess;
    CHECK_ERROR_RETURN(ParseNodeFromString(info.buf_ptr, &pos, NULL, &tree, lang_info->arr), NULL, lang_info->arr, lang_info->root);
    free(info.buf_ptr);

    lang_info->root->root = tree;
    dump_info->tree = lang_info->root;

    return kSuccess;
}