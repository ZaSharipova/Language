#ifndef ENUMS_H_
#define ENUMS_H_

#define MAX_VARIABLES 26
#define eps 1e-12 //

#define MAIN "adepio_maximus"

enum DifErrors {
    kSuccess,
    kErrorStat,
    kSyntaxError,
    kNoMemory,
    kFailure,
    kZeroRoot,
    kWrongTreeSize,
    kWrongParent,
    kHasCycle,
    kErrorOpening,
};

enum DifTypes {
    kOperation,
    kVariable,
    kNumber,
};

enum OperationTypes {
    kOperationAdd               = 0,
    kOperationSub               = 1,
    kOperationMul               = 2,
    kOperationDiv               = 3,
    kOperationPow               = 4,
    kOperationSin               = 5,
    kOperationCos               = 6,
    kOperationTg                = 7,
    kOperationLn                = 8,
    kOperationArctg             = 9,
    kOperationSinh              = 10,
    kOperationCosh              = 11,
    kOperationTgh               = 12,
    kOperationIs                = 13,
    kOperationIf                = 14,
    kOperationElse              = 15,
    kOperationWhile             = 16,
    kOperationThen              = 17,
    kOperationComma             = 18,
    kOperationFunction          = 19,
    kOperationCall              = 20,
    kOperationReturn            = 21,

    kOperationB                 = 22,
    kOperationBE                = 23,
    kOperationA                 = 24,
    kOperationAE                = 25,
    kOperationE                 = 26,
    kOperationNE                = 27,
    
    kOperationSQRT              = 28,

    kOperationWrite             = 29,
    kOperationWriteChar         = 30,
    kOperationRead              = 31,
    kOperationParOpen           = 32,
    kOperationParClose          = 33,
    kOperationBraceOpen         = 34,
    kOperationBraceClose        = 35,
    kOperationHLT               = 36,
    
    kOperationTrueSeparator     = 37,
    kOperationFalseSeparator    = 38,
    kOperationTernary           = 39,
    
    kOperationBracketOpen       = 40,
    kOperationBracketClose      = 41,
    kOperationArrPos            = 42,
    kOperationArrDecl           = 43,

    kOperationCallAddr          = 44,
    kOperationGetAddr           = 45,

    kOperationNone              = -1,
};

enum DiffModes {
    kAll             = 1,
    kDerivative      = 2,
    kDerivativeInPos = 3,
    kCount           = 4,
    kTeilor          = 5,
    kGraph           = 6,
    kExit            = 7,
};

enum Realloc_Mode {
    kIncrease,
    kDecrease,
    kNoChange,
    kIncreaseZero,
};

enum VariableModes {
    kVarVariable,
    kVarFunction,
    kUnknown,
};

enum ValCategory {
    klvalue,
    krvalue,
};

#endif //ENUMS_H_