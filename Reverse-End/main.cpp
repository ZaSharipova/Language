#include "Front-End/Rules.h"
#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/LanguageFunctions.h"
#include "Common/DoGraph.h"
// #include "Front-End/TreeToAsm.h"
#include "Reverse-End/TreeToCode.h"
#include "Common/StackFunctions.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"

#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "less args than 3.\n");
        return 1;
    } 
    
    char *tree_file = argv[1];
    char *code_file = argv[2];

    INIT_EVERYTHING(root, Variable_Array, lang_info, tokens_no, dump_info);
    
    ReadTreeAndParse(&lang_info, &dump_info, tree_file);
    DoTreeInGraphviz(root.root, &dump_info, &Variable_Array);

    FILE_OPEN_AND_CHECK(code_out, code_file, "w", &Variable_Array, &root, NULL);
    GenerateCodeFromAST(root.root, code_out, &Variable_Array, 0);
    fclose(code_out);
    
    DtorVariableArray(&Variable_Array);
    TreeDtor(&root);

    return kSuccess;
}