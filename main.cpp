#include "Rules.h"
#include "Structs.h"
#include "Enums.h"
#include "LanguageFunctions.h"
#include "DoTex.h"

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
    PrintTree(root.root, out);

    DtorVariableArray(&Variable_Array);
    fclose(out);
    //TreeDtor(&root);

    return kSuccess;
}