#ifndef BACK_FUNCTIONS_H_
#define BACK_FUNCTIONS_H_

#include "Common/Enums.h"
#include "Common/Structs.h"

// LangErrors ReadTreeAndParse(Language *lang_info, DumpInfo *dump_info, const char *filename_in);
LangErrors PrintAsm(Language *lang_info, const char *filename_out);

#endif //BACK_FUNCTIONS_H_