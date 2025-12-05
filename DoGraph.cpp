#include "DoGraph.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "Enums.h"
#include "Structs.h"

#define FILE_OUT "output.txt"
#define MAX_COMMAND_SIZE 50

static const char *PrintOperationType(const DifNode_t *node);
static GraphOperation PrintExpressionType(const DifNode_t *node);
static void DoSnprintf(DumpInfo *Info);
static void PrintDotNode(FILE *file, const DifNode_t *node, const DifNode_t *node_colored, bool flag);

void DoTreeInGraphviz(const DifNode_t *node, DumpInfo *Info, const DifNode_t *node_colored) {
    assert(node);
    assert(Info);
    assert(node_colored);

    FILE *file = fopen(Info->filename_to_write_graphviz, "w");
    if (!file) {
        perror("Cannot open file for writing.");
        return;
    }

    fprintf(file, "digraph BinaryTree {\n");
    fprintf(file, "    rankdir=TB;\n");
    fprintf(file, "    node [shape=record, style=filled, fillcolor=lightblue];\n");
    fprintf(file, "    edge [fontsize=10];\n\n");
    fprintf(file, "    graph [fontname=\"Arial\"];\n");
    fprintf(file, "    node [fontname=\"Arial\"];\n");
    fprintf(file, "    edge [fontname=\"Arial\"];\n");

    if (node->parent == NULL) {
        fprintf(file, "    \"1\" [label=\"ROOT\", shape=rect, fillcolor=pink];\n");
        fprintf(file, "    \"1\" -> \"%p\";\n", (void *)node);
    } else {
        fprintf(file, "    // Empty tree\n");
    }

    PrintDotNode(file, node, node_colored, Info->flag_new);

    fprintf(file, "}\n");
    fclose(file);

    DoSnprintf(Info);
}

static void PrintDotNode(FILE *file, const DifNode_t *node, const DifNode_t *node_colored, bool flag) {
    assert(file);
    assert(node);
    assert(node_colored);

    const char *color_number    = "dodgerblue";
    const char *color_variable  = "gold";

    if (node->type == kNumber || node->type == kVariable) {
        fprintf(file, "    \"%p\" [label=\"Parent: %p \n  Addr: %p \n  Type: %s\n", 
            (void *)node, (void *)node->parent, (void *)node, PrintOperationType(node));
        if (node->type == kNumber) {
            fprintf(file, "  Value: %lf  \nLeft: %p | Right: %p\" shape=egg color=black fillcolor=%s style=filled width=4 height=1.5 fixedsize=true];\n", 
                node->value.number, (void *)node->left, (void *)node->right, color_number);
        } else {
            fprintf(file, "  Value: %s  \nLeft: %p | Right: %p\" shape=octagon color=black fillcolor=%s style=filled width=4 height=1.5 fixedsize=true];\n", 
                (node->value).variable->variable_name, (void *)node->left, (void *)node->right, color_variable);
        }
    } else if (node->type == kOperation) {
        fprintf(file, "    \"%p\" [label=\"{Parent: %p \n | Addr: %p \n | Type: %s\n", 
            (void *)node, (void *)node->parent, (void *)node, PrintOperationType(node));
        fprintf(file, " | Value: %s | {Left: %p | Right: %p}}\" shape=Mrecord color=black fillcolor=%s, style=filled];\n", 
                PrintExpressionType(node).operation_name, (void *)node->left, (void *)node->right, PrintExpressionType(node).color);
    }

    if (node->left) {
        fprintf(file, "    \"%p\" -> \"%p\";\n", 
                (void *)node, (void *)node->left);
        PrintDotNode(file, node->left, node_colored, flag);
    }

    if (node->right) {
        fprintf(file, "    \"%p\" -> \"%p\";\n\n", 
                (void *)node, (void *)node->right);
        PrintDotNode(file, node->right, node_colored, flag);
    }
}

static void DoSnprintf(DumpInfo *Info) {
    assert(Info);

    snprintf(Info->image_file, sizeof(Info->image_file), "Images/graph_%zu.svg", Info->graph_counter);
    Info->graph_counter ++;
    char cmd[MAX_COMMAND_SIZE] = {};
    snprintf(cmd, sizeof(cmd), "dot " FILE_OUT " -T svg -o %s", Info->image_file);
    
    system(cmd);
}

static const char *PrintOperationType(const DifNode_t *node) {
    assert(node);

    switch(node->type) {
    case (kNumber):
        return "NUM";
    case (kVariable):
        return "VAR";
    case (kOperation):
        return "OP";
    }
}

static GraphOperation PrintExpressionType(const DifNode_t *node) {
    assert(node);

    switch (node->value.operation) {
        case (kOperationAdd):
            return {"ADD", "plum"};
        case (kOperationSub):
            return {"SUB", "orchid3"};
        case (kOperationMul):
            return {"MUL", "salmon1"};
        case (kOperationDiv):
            return {"DIV", "skyblue"};
        case (kOperationPow):
            return {"POW", "darkseagreen3"};
        case (kOperationSin):
            return {"SIN", "khaki3"};
        case (kOperationCos):
            return {"COS", "cornsilk3"};
        case (kOperationTg):
            return {"TAN", "tan"};
        case (kOperationLn):
            return {"LOG", "cadetblue1"};
        case (kOperationArctg):
            return {"ARCTG", "lightgoldenrod"};
        case (kOperationSinh):
            return {"SH", "lemonchiffon"};
        case (kOperationCosh):
            return {"CH", "lightpink"};
        case (kOperationTgh):
            return {"TH", "lightskyblue3"};
        case (kOperationIs):
            return {"=", "darkseagreen3"};
        case (kOperationIf):
            return {"if", "navajowhite1"};
        case (kOperationThen):
            return {";", "pink"};
        case (kOperationWhile):
            return {"while", "rosybrown"};
        case (kOperationNone):
        default: return {NULL, NULL};
    }
}