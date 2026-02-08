#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/LanguageFunctions.h"
#include "Common/DoGraph.h"
#include "Common/StackFunctions.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"
#include "Back-End/TreeToAsm.h"
#include "Back-End/BackFunctions.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    (void)argc;

    const char *filename_in = argv[1];
    const char *filename_out= argv[2];

    INIT_EVERYTHING(root, Variable_Array, lang_info, tokens_no, dump_info);

    CHECK_ERROR_RETURN(ReadTreeAndParse(&lang_info, &dump_info, filename_in), NULL, NULL, NULL); //TODO: наоборот

    DoTreeInGraphviz(lang_info.root->root, &dump_info, &Variable_Array);

    CHECK_ERROR_RETURN(PrintAsm(&lang_info, filename_out), NULL, NULL, NULL);

    TreeDtor(lang_info.root);
    DtorVariableArray(&Variable_Array);
    return 0;
}