#include <stdio.h>
#include <assert.h>

#include "Front-End/Rules.h"
#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/LanguageFunctions.h"
#include "Common/DoGraph.h"
#include "Trick-End/GenerateNewCode.h"
#include "Common/StackFunctions.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"

int main(int argc, char *argv[]) {
    (void)argc;
    const char *filename_in = argv[1];
    const char *filename_out= argv[2];

    LangRoot root = {};
    LangRootCtor(&root);

    DifErrors err = kSuccess;
    VariableArr Variable_Array = {};
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 16), &Variable_Array, &root);

    INIT_DUMP_INFO(dump_info);
    dump_info.tree = &root;

    Language lang_info = {&root, NULL, NULL, &Variable_Array};

    Stack_Info tokens = {};
    StackCtor(&tokens, 1, stderr);
    lang_info.tokens = &tokens;
    CHECK_ERROR_RETURN(ReadInfix(&lang_info, &dump_info, filename_in), lang_info.arr, lang_info.root);

    FILE_OPEN_AND_CHECK(out_file, filename_out, "w", lang_info.arr, lang_info.root);

    dump_info.tree = &root;
    DoTreeInGraphviz(root.root, &dump_info, &Variable_Array);

    GenerateNewCodeFromAST(root.root, out_file, &Variable_Array, 0);
    fclose(out_file);
    
    DtorVariableArray(&Variable_Array);
    StackDtor(&tokens, stderr);
    TreeDtor(&root);

    return 0;
}