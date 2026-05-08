#include "Front-End/Rules.h"
#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/LanguageFunctions.h"
#include "Common/DoGraph.h"
#include "Reverse-End/TreeToCode.h"
#include "Common/StackFunctions.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    (void)argc;
    const char *filename_in  = argv[1];
    const char *filename_out = argv[2];

    INIT_EVERYTHING(root, Variable_Array, lang_info, tokens, dump_info);
    CHECK_ERROR_RETURN(ReadInfix(&lang_info, &dump_info, filename_in), &tokens, lang_info.arr, NULL);

    FILE_OPEN_AND_CHECK(ast_file, filename_out, "w", &tokens, lang_info.arr, NULL);
    PrintAST(root.root, ast_file, &Variable_Array, 0);
    fclose(ast_file);

    StackDtor(&tokens, stderr);
    DtorVariableArray(&Variable_Array);
    return 0;
}