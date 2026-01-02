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
    VariableArr variable_array = {};
    CHECK_ERROR_RETURN(InitArrOfVariable(&variable_array, 16));

    INIT_DUMP_INFO(dump_info);
    dump_info.tree = &root;

    Language lang_info = {&root, NULL, NULL, &variable_array};

    if (strcmp(mode, "tree-asm") == 0) {

        FILE_OPEN_AND_CHECK(ast_file, filename_in, "r");

        FileInfo info = {};
        DoBufRead(ast_file, filename_in, &info);
        fclose(ast_file);

        LangRoot parsed_root = {};
        LangRootCtor(&parsed_root);

        size_t pos = 0;
        LangNode_t *tree = NULL;

        CHECK_ERROR_RETURN(ParseNodeFromString(info.buf_ptr, &pos, NULL, &tree, &variable_array));

        // fprintf(stderr, "%zu\n\n", variable_array.size);
        // for (size_t i = 0; i < variable_array.size; i++) {
        //     fprintf(stderr, "%s %d\n\n", variable_array.var_array[i].variable_name, variable_array.var_array[i].variable_value);
        // }

        parsed_root.root = tree;
        dump_info.tree = &parsed_root;

        DoTreeInGraphviz(parsed_root.root, &dump_info, &variable_array);

        FILE_OPEN_AND_CHECK(asm_file, filename_out, "w");

        int ram_base = 0;
        AsmInfo asm_info = {};
        PrintProgram(asm_file, parsed_root.root, &variable_array, &ram_base, &asm_info);

        fclose(asm_file);
    }

    else if (strcmp(mode, "code-asm") == 0) {
        CHECK_ERROR_RETURN(ReadInfix(&lang_info, &dump_info, filename_in));

        FILE_OPEN_AND_CHECK(asm_file, filename_out, "w");

        int ram_base = 0;
        AsmInfo asm_info = {};
        PrintProgram(asm_file, root.root, &variable_array, &ram_base, &asm_info);

        fclose(asm_file);
    }

    else if (strcmp(mode, "code-tree") == 0) {

        CHECK_ERROR_RETURN(ReadInfix(&lang_info, &dump_info, filename_in));

        FILE_OPEN_AND_CHECK(ast_file, filename_out, "w");
        PrintAST(root.root, ast_file, &variable_array, 0);
        fclose(ast_file);
    }

    else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        DtorVariableArray(&variable_array);
        return 1;
    }

    DtorVariableArray(&variable_array);

    return 0;
}
