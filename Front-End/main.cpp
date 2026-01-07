#include "Front-End/Rules.h"
#include "Common/Structs.h"
#include "Common/Enums.h" //TODO
#include "Front-End/LanguageFunctions.h"
#include "Common/DoGraph.h"
#include "Front-End/TreeToAsm.h"
#include "Reverse-End/TreeToCode.h"
#include "Common/StackFunctions.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

int main(int argc, char *argv[]) {

    if (argc < 4) {
        fprintf(stderr,
                "Usage: %s <mode> <input_file> <output_file>\n"
                "Modes:\n"
                "  tree-asm\n"
                "  code-asm\n"
                "  code-tree\n",
                argv[0]);
        return 1;
    }

    const char *mode = argv[1];
    const char *filename_in = argv[2];
    const char *filename_out= argv[3];

    LangRoot root = {};
    LangRootCtor(&root);

    DifErrors err = kSuccess;
    VariableArr Variable_Array = {};
    CHECK_ERROR_RETURN(InitArrOfVariable(&Variable_Array, 16), &Variable_Array, &root);

    INIT_DUMP_INFO(dump_info);
    dump_info.tree = &root;

    Language lang_info = {&root, NULL, NULL, &Variable_Array};

    if (strcmp(mode, "tree-asm") == 0) {

        FILE_OPEN_AND_CHECK(ast_file, filename_in, "r", lang_info.arr, lang_info.root);

        FileInfo info = {};
        DoBufRead(ast_file, filename_in, &info);
        fclose(ast_file);

        // LangRoot parsed_root = {};
        // LangRootCtor(&parsed_root);

        size_t pos = 0;
        LangNode_t *tree = NULL;

        CHECK_ERROR_RETURN(ParseNodeFromString(info.buf_ptr, &pos, NULL, &tree, &Variable_Array), lang_info.arr, lang_info.root);

        // fprintf(stderr, "%zu\n\n", Variable_Array.size);
        // for (size_t i = 0; i < Variable_Array.size; i++) {
        //     fprintf(stderr, "%s %d\n\n", Variable_Array.var_array[i].variable_name, Variable_Array.var_array[i].variable_value);
        // }

        lang_info.root->root = tree;
        dump_info.tree = lang_info.root;

        DoTreeInGraphviz(lang_info.root->root, &dump_info, &Variable_Array);

        FILE_OPEN_AND_CHECK(asm_file, filename_out, "w", &Variable_Array, lang_info.root->root, lang_info.arr, lang_info.root);

        int ram_base = 0;
        AsmInfo asm_info = {};
        PrintProgram(asm_file, lang_info.root->root, lang_info.arr, &ram_base, &asm_info);

        fclose(asm_file);
    }

    else if (strcmp(mode, "code-asm") == 0) {
        CHECK_ERROR_RETURN(ReadInfix(&lang_info, &dump_info, filename_in), lang_info.arr, lang_info.root);

        FILE_OPEN_AND_CHECK(asm_file, filename_out, "w", lang_info.arr, lang_info.root);

        int ram_base = 0;
        AsmInfo asm_info = {};
        PrintProgram(asm_file, root.root, &Variable_Array, &ram_base, &asm_info);

        fclose(asm_file);
    }

    else if (strcmp(mode, "code-tree") == 0) {

        CHECK_ERROR_RETURN(ReadInfix(&lang_info, &dump_info, filename_in), lang_info.arr, lang_info.root);

        FILE_OPEN_AND_CHECK(ast_file, filename_out, "w", lang_info.arr, lang_info.root);
        PrintAST(root.root, ast_file, &Variable_Array, 0); //
        fclose(ast_file);
    }

    else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        DtorVariableArray(&Variable_Array);
        return 1;
    }

    DtorVariableArray(&Variable_Array);

    return 0;
}
