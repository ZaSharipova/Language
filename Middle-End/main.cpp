#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/LanguageFunctions.h"
#include "Common/DoGraph.h"
#include "Middle-End/Optimise.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"
#include "Common/StackFunctions.h" 

#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <tree_file>\n", argv[0]);
        return 1;
    }
    char *tree_file = argv[1];
    
    INIT_EVERYTHING(root, Variable_Array, lang_info, tokens_no, dump_info);

    ReadTreeAndParse(&lang_info, &dump_info, tree_file);
    DoTreeInGraphviz(root.root, &dump_info, &Variable_Array);

    root.root = OptimiseTree(&lang_info, root.root, &Variable_Array);
    
    FILE_OPEN_AND_CHECK(ast_file_write, tree_file, "w", &Variable_Array, &root, NULL);
    PrintAST(root.root, ast_file_write, &Variable_Array, 0);
    fclose(ast_file_write);
    
    DoTreeInGraphviz(root.root, &dump_info, &Variable_Array);

    DtorVariableArray(&Variable_Array);
    TreeDtor(&root);

    return kSuccess;
}
