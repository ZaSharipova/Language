#include "Rules.h"
#include "Structs.h"
#include "Enums.h"
#include "LanguageFunctions.h"
#include "DoTex.h"
#include "DoGraph.h"

int main(void) {
    DifRoot root = {};
    DifRootCtor(&root);

    VariableArr Variable_Array = {};
    DifErrors err = kSuccess;
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 4));

    FILE_OPEN_AND_CHECK(out, "diftex.tex", "w");

    INIT_DUMP_INFO(dump_info);
    
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
    DoTreeInGraphviz(root1.root, &dump_info, new_node_adr);
    
    DtorVariableArray(&Variable_Array);
    fclose(out);
    TreeDtor(&root1);

    return kSuccess;
}