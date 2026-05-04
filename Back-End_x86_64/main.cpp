#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/LanguageFunctions.h"
#include "Common/DoGraph.h"
#include "Common/StackFunctions.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"
#include "Back-End/TreeToAsm.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    (void)argc;

    const char *mode = argv[1];
    const char *filename_in = argv[2];

    INIT_EVERYTHING(root, Variable_Array, lang_info, tokens_no, dump_info);

    CHECK_ERROR_RETURN(ReadTreeAndParse(&lang_info, &dump_info, filename_in), NULL, NULL, NULL); //TODO: наоборот

    DoTreeInGraphviz(lang_info.root->root, &dump_info, &Variable_Array);

    if (strncmp(mode, "--tree_asm", sizeof("--tree_asm")) == 0) {
#include "Back-End/BackFunctions.h"
        const char *filename_out = argv[3];
        CHECK_ERROR_RETURN(PrintAsm(&lang_info, filename_out), NULL, NULL, NULL);

    } else if (strncmp(mode, "--tree_elf", sizeof("--tree_elf")) == 0) {
#include "Back-End/TreeToBin.h"
        CompileTreeToELF(lang_info.root->root, &Variable_Array, "my_program");
    }

    TreeDtor(lang_info.root);
    DtorVariableArray(&Variable_Array);
    return 0;
}