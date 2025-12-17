#include "Front-End/Rules.h"
#include "Structs.h"
#include "Enums.h"
#include "Front-End/LanguageFunctions.h"
#include "DoGraph.h"
#include "Front-End/TreeToAsm.h"
#include "TreeToCode.h"

int main(void) {
    LangRoot root = {};
    LangRootCtor(&root);

    VariableArr Variable_Array = {};
    DifErrors err = kSuccess;
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 4));

    INIT_DUMP_INFO(dump_info);
    dump_info.tree = &root;
    Language lang_info = {&root, NULL, NULL, &Variable_Array};
    CHECK_ERROR_RETURN(ReadInfix(&lang_info, &dump_info, "input.txt"));
    //PrintTree(root.root, out);


    FILE_OPEN_AND_CHECK(ast_file, "ast.txt", "w");
    PrintAST(root.root, ast_file, &Variable_Array, 0);
    fclose(ast_file);

    
    FILE_OPEN_AND_CHECK(ast_file_read, "ast.txt", "r");
    FileInfo info = {};
    DoBufRead(ast_file_read, "ast.txt", &info);
    LangRoot root1 = {};
    LangRootCtor(&root1);
    LangNode_t new_node = {};
    LangNode_t *new_node_adr = &new_node;

    fprintf(stderr, "%zu\n\n", Variable_Array.size);
    for (size_t i = 0; i < Variable_Array.size; i++) {
        fprintf(stderr, "%s %d\n\n", Variable_Array.var_array[i].variable_name, Variable_Array.var_array[i].variable_value);
    }
    FILE_OPEN_AND_CHECK(asm_file, "asm.asm", "w");
    int ram_base = 0;
    PrintProgram(asm_file, root.root, &Variable_Array, &ram_base);
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