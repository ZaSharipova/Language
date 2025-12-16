#ifndef ENUMS_H_
#define ENUMS_H_

#define MAX_VARIABLES 26
#define eps 1e-12 //

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
    kOperationAdd,
    kOperationSub,
    kOperationMul,
    kOperationDiv,
    kOperationPow,
    kOperationSin,
    kOperationCos,
    kOperationTg,
    kOperationLn,
    kOperationArctg,
    kOperationSinh,
    kOperationCosh,
    kOperationTgh,
    kOperationIs,
    kOperationIf,
    kOperationElse,
    kOperationWhile,
    kOperationThen,
    kOperationComma,
    kOperationFunction,
    kOperationCall,
    kOperationReturn,
    kOperationWrite,
    kOperationRead,
    kOperationParOpen,
    kOperationParClose,
    kOperationBraceOpen,
    kOperationBraceClose,
    kOperationNone,
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
};

#endif //ENUMS_H_