#include "Front-End/Rules.h"
#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Front-End/LanguageFunctions.h"
#include "Reverse-End/TreeToCode.h"
#include "Common/DoGraph.h"
#include "Middle-End/Optimise.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <tree_file>\n", argv[0]);
        return 1;
    }

    char *tree_file = argv[1];
    
    LangRoot root = {};
    LangRootCtor(&root);

    VariableArr Variable_Array = {};
    DifErrors err = kSuccess;
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 4));

    INIT_DUMP_INFO(dump_info);
    dump_info.tree = &root;
    Language lang_info = {&root, NULL, NULL, &Variable_Array};

    FILE_OPEN_AND_CHECK(ast_file_read, tree_file, "r");
    
    FileInfo info = {};
    DoBufRead(ast_file_read, tree_file, &info);
    fclose(ast_file_read);

    LangRoot root1 = {};
    LangRootCtor(&root1);
    
    LangNode_t *root_node = NULL;
    size_t pos = 0;
    CHECK_ERROR_RETURN(ParseNodeFromString(info.buf_ptr, &pos, NULL, &root_node, &Variable_Array));
    root1.root = root_node;
    
    dump_info.tree = &root1;
    DoTreeInGraphviz(root1.root, &dump_info, &Variable_Array);

    root1.root = OptimiseTree(&root1, root1.root, &Variable_Array);
    
    FILE_OPEN_AND_CHECK(ast_file_write, tree_file, "w");
    PrintAST(root1.root, ast_file_write, &Variable_Array, 0);
    fclose(ast_file_write);
    
    DoTreeInGraphviz(root1.root, &dump_info, &Variable_Array);

    DtorVariableArray(&Variable_Array);

    return kSuccess;
}
