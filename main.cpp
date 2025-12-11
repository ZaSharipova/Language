#include "Rules.h"
#include "Structs.h"
#include "Enums.h"
#include "LanguageFunctions.h"
#include "DoGraph.h"

int main(void) {
    DifRoot root = {};
    DifRootCtor(&root);

    VariableArr Variable_Array = {};
    DifErrors err = kSuccess;
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 4));

    FILE_OPEN_AND_CHECK(out, "diftex.tex", "w");

    INIT_DUMP_INFO(dump_info);
    dump_info.tree = &root;
    CHECK_ERROR_RETURN(ReadInfix(&root, &dump_info, &Variable_Array, "input.txt", out));
    //PrintTree(root.root, out);

    FILE_OPEN_AND_CHECK(ast_file, "ast.txt", "w");
    PrintAST(root.root, ast_file, &Variable_Array);
    fclose(ast_file);

    
    FILE_OPEN_AND_CHECK(ast_file_read, "ast.txt", "r");
    FileInfo info = {};
    DoBufRead(ast_file_read, "ast.txt", &info);
    DifRoot root1 = {};
    DifRootCtor(&root1);
    DifNode_t new_node = {};
    DifNode_t *new_node_adr = &new_node;
    size_t pos = 0;
    ParseNodeFromString(info.buf_ptr, &pos, root.root, &new_node_adr, &Variable_Array);
    root1.root = new_node_adr;
    dump_info.tree = &root1;
    DoTreeInGraphviz(root1.root, &dump_info, &Variable_Array);

    FILE_OPEN_AND_CHECK(code_out, "test.txt", "w");
    GenerateCodeFromAST(root1.root, code_out, &Variable_Array);
    fclose(out);
    
    DtorVariableArray(&Variable_Array);
    fclose(out);
    //TreeDtor(&root);

    return kSuccess;
}