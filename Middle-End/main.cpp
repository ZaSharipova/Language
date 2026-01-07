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
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 16), &Variable_Array, &root);

    INIT_DUMP_INFO(dump_info);
    dump_info.tree = &root;
    //Language lang_info = {&root, NULL, NULL, &Variable_Array};

    FILE_OPEN_AND_CHECK(ast_file_read, tree_file, "r", &Variable_Array, &root);
    
    FileInfo info = {};
    DoBufRead(ast_file_read, tree_file, &info);
    fclose(ast_file_read);
    
    LangNode_t *root_node = NULL;
    size_t pos = 0;
    CHECK_ERROR_RETURN(ParseNodeFromString(info.buf_ptr, &pos, NULL, &root_node, &Variable_Array), &Variable_Array, &root);
    root.root = root_node;
    
    dump_info.tree = &root;
    DoTreeInGraphviz(root.root, &dump_info, &Variable_Array);

    root.root = OptimiseTree(&root, root.root, &Variable_Array);
    
    FILE_OPEN_AND_CHECK(ast_file_write, tree_file, "w", &Variable_Array, &root);
    PrintAST(root.root, ast_file_write, &Variable_Array, 0);
    fclose(ast_file_write);
    
    DoTreeInGraphviz(root.root, &dump_info, &Variable_Array);

    DtorVariableArray(&Variable_Array);
    TreeDtor(&root);

    return kSuccess;
}
