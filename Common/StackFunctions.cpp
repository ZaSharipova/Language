#include "Common/StackFunctions.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "Common/Enums.h"
#include "Common/Structs.h"

static Realloc_Mode CheckSize(ssize_t size, ssize_t *capacity);

DifErrors StackCtor(Stack_Info *stk, ssize_t capacity, FILE *open_log_file) {
    assert(stk);
    assert(open_log_file);

    if (capacity <= 0) {
        capacity = 1;
    }

    stk->size = 0;
    stk->capacity = capacity;
    stk->la_size = 0;

    stk->data = (LangNode_t **) calloc ((size_t)capacity, sizeof(LangNode_t *));
    if (!stk->data) {
        return kNoMemory;
    }

    return kSuccess;
}

DifErrors StackPush(Stack_Info *stk, LangNode_t *value, FILE *open_log_file) {
    assert(stk);
    assert(value);
    assert(open_log_file);

    Realloc_Mode realloc_type = CheckSize(stk->size, &stk->capacity);
    if (realloc_type != kNoChange) {
        StackRealloc(stk, open_log_file, realloc_type);
    }

    stk->data[stk->size++] = value;
    return kSuccess;
}

DifErrors StackPop(Stack_Info *stk, LangNode_t **value, FILE *open_log_file) {
    assert(stk);
    assert(value);
    assert(open_log_file);

    if (stk->size == 0) {
        return kFailure;
    }

    *value = stk->data[--stk->size];

    Realloc_Mode realloc_type = CheckSize(stk->size, &stk->capacity);
    if (realloc_type != kNoChange) {
        StackRealloc(stk, open_log_file, realloc_type);
    } 
    
    return kSuccess;
}

static Realloc_Mode CheckSize(ssize_t size, ssize_t *capacity) {
    assert(capacity);

    if (size * 2 >= *capacity) {
        return kIncrease;
    } else if (size * 4 < *capacity && *capacity > 4) {
        return kDecrease;
    } else {
        return kNoChange;
    }
}

DifErrors StackRealloc(Stack_Info *stk, FILE *open_log_file, Realloc_Mode realloc_type) {
    assert(stk);
    assert(open_log_file);

    if (realloc_type == kIncrease) {
        stk->capacity *= 2;
    } else if (realloc_type == kDecrease) {
        stk->capacity /= 2;
    } else if (realloc_type == kIncreaseZero) {
        stk->capacity = 1;
    }

    LangNode_t **new_data = (LangNode_t **) realloc (stk->data, (size_t)stk->capacity * sizeof(LangNode_t*));
    if (!new_data) {
        return kNoMemory;
    } 

    stk->data = new_data;

    return kSuccess;
}

DifErrors StackDtor(Stack_Info *stk, FILE *open_log_file) {
    assert(stk);
    assert(open_log_file);

    if (stk->data != NULL) {
        LangNode_t **nodes = (LangNode_t**)stk->data;
        
        for (size_t i = 0; i < (size_t)stk->size; ++i) {
            if (nodes[i]) {
                free(nodes[i]);
                nodes[i] = NULL;
            }
        }
        
        free(stk->data);
    }

    stk->data = NULL;
    stk->size = 0;
    stk->capacity = 0;

    return kSuccess;
}

LangNode_t *GetStackElem(Stack_Info *stk, size_t pos) {
    assert(stk);

    if (pos >= (size_t)stk->la_size) {
        return NULL;
    }

    return stk->data[pos];
}