#include "Back-End/BackFunctions.h"

#include "Common/Structs.h"
#include "Common/Enums.h"
#include "Common/LanguageFunctions.h"
#include "Common/StackFunctions.h"
#include "Common/ReadTree.h"
#include "Common/CommonFunctions.h"
#include "Back-End/TreeToAsm.h"

#include <assert.h>

LangErrors PrintAsm(Language *lang_info, const char *filename_out) {
    assert(lang_info);
    assert(filename_out);

    FILE_OPEN_AND_CHECK(asm_file, filename_out, "w", NULL, lang_info->arr, lang_info->root);
    int ram_base = 0;
    AsmInfo asm_info = {};

    PrintProgram(asm_file, lang_info->root->root, lang_info->arr, &ram_base, &asm_info);
    fclose(asm_file);

    return kSuccess;
}