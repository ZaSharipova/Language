#include "Rules.h"
#include "Structs.h"
#include "Enums.h"
#include "LanguageFunctions.h"
#include "DoGraph.h"
#include "TreeToAsm.h"
#include "TreeToCode.h"

int main(void) {
    DifRoot root = {};
    DifRootCtor(&root);

    VariableArr Variable_Array = {};
    DifErrors err = kSuccess;
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 4));

    INIT_DUMP_INFO(dump_info);
    dump_info.tree = &root;
    CHECK_ERROR_RETURN(ReadInfix(&root, &dump_info, &Variable_Array, "input.txt"));
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

    FILE_OPEN_AND_CHECK(asm_file, "asm.asm", "w");
    PrintProgram(asm_file, root.root, &Variable_Array);
    fclose(asm_file);

    size_t pos = 0;
    ParseNodeFromString(info.buf_ptr, &pos, root.root, &new_node_adr, &Variable_Array);
    root1.root = new_node_adr;
    dump_info.tree = &root1;
    DoTreeInGraphviz(root1.root, &dump_info, &Variable_Array);

    FILE_OPEN_AND_CHECK(code_out, "test.txt", "w");
    GenerateCodeFromAST(root1.root, code_out, &Variable_Array, 0);
    
    DtorVariableArray(&Variable_Array);
    //TreeDtor(&root);

    return kSuccess;
}