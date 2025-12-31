#include "DoGraph.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include "Enums.h"
#include "Structs.h"

#define FILE_OUT "output.txt"
#define MAX_COMMAND_SIZE 50
#define DOT_INDENT "   "

static const char *GetNodeTypeString(DifTypes type);
static void GenerateGraphImage(DumpInfo *info);
static void WriteDotNode(FILE *file, const LangNode_t *node, bool flag, VariableArr *arr);
static GraphOperation PrintExpressionType(const LangNode_t *node);

static void WriteDotHeader(FILE *file);
static void WriteRootNode(FILE *file, const LangNode_t *root);
static void WriteNodeDefinition(FILE *file, const LangNode_t *node, VariableArr *arr);
static void WriteNodeConnection(FILE *file, const LangNode_t *from, const LangNode_t *to);

static void WriteNumberNode(FILE *file, const LangNode_t *node);
static void WriteVariableNode(FILE *file, const LangNode_t *node, VariableArr *arr);
static void WriteOperationNode(FILE *file, const LangNode_t *node);
static void WriteUnknownNode(FILE *file, const LangNode_t *node);

void DoTreeInGraphviz(const LangNode_t *root, DumpInfo *info, VariableArr *arr) {
    assert(root);
    assert(info);
    assert(arr);

    FILE *dot_file = fopen(info->filename_to_write_graphviz, "w");
    if (!dot_file) {
        perror("Cannot open DOT file for writing");
        return;
    }

    WriteDotHeader(dot_file);
    WriteRootNode(dot_file, root);
    WriteDotNode(dot_file, root, info->flag_new, arr);
    fprintf(dot_file, "}\n");
    
    fclose(dot_file);
    GenerateGraphImage(info);
}

static void WriteDotHeader(FILE *file) {
    assert(file);

    fprintf(file, "digraph BinaryTree {\n");
    fprintf(file, DOT_INDENT "rankdir=TB;\n");
    fprintf(file, DOT_INDENT "node [shape=record, style=filled, fillcolor=lightblue];\n");
    fprintf(file, DOT_INDENT "edge [fontsize=10];\n\n");
    fprintf(file, DOT_INDENT "graph [fontname=\"Arial\"];\n");
    fprintf(file, DOT_INDENT "node [fontname=\"Arial\"];\n");
    fprintf(file, DOT_INDENT "edge [fontname=\"Arial\"];\n\n");
}

static void WriteRootNode(FILE *file, const LangNode_t *root) {
    assert(file);
    assert(root);

    if (root->parent == NULL) {
        fprintf(file, DOT_INDENT "\"1\" [label=\"ROOT\", shape=rect, fillcolor=pink];\n");
        fprintf(file, DOT_INDENT "\"1\" -> \"%p\";\n\n", (void *)root);
    } else {
        fprintf(file, DOT_INDENT "// Tree with parent node\n");
    }
}

static void WriteDotNode(FILE *file, const LangNode_t *node, bool flag, VariableArr *arr) {
    assert(file);
    assert(node);
    assert(arr);

    WriteNodeDefinition(file, node, arr);
    
    if (node->left) {
        WriteNodeConnection(file, node, node->left);
        WriteDotNode(file, node->left, flag, arr);
    }
    
    if (node->right) {
        WriteNodeConnection(file, node, node->right);
        fprintf(file, "\n");
        WriteDotNode(file, node->right, flag, arr);
    }
}

static void WriteNodeDefinition(FILE *file, const LangNode_t *node, VariableArr *arr) {
    assert(file);
    assert(node);
    assert(arr);

    switch (node->type) {
        case kNumber:
            WriteNumberNode(file, node);
            break;
        case kVariable:
            WriteVariableNode(file, node, arr);
            break;
        case kOperation:
            WriteOperationNode(file, node);
            break;
        default:
            WriteUnknownNode(file, node);
            break;
    }
}

static void WriteNumberNode(FILE *file, const LangNode_t *node) {
    assert(file);
    assert(node);

    const char *color = "dodgerblue";
    fprintf(file, DOT_INDENT "\"%p\" [label=\"", (void *)node);
    fprintf(file, "Parent: %p\\n", (void *)node->parent);
    fprintf(file, "Addr: %p\\n", (void *)node);
    fprintf(file, "Type: %s\\n", GetNodeTypeString(node->type));
    fprintf(file, "Value: %.2lf\\n", node->value.number);
    fprintf(file, "Left: %p | Right: %p\"", (void *)node->left, (void *)node->right);
    fprintf(file, " shape=egg color=black fillcolor=%s", color);
    fprintf(file, " style=filled width=4 height=1.5 fixedsize=true];\n");
}

static void WriteVariableNode(FILE *file, const LangNode_t *node, VariableArr *arr) {
    assert(file);
    assert(node);
    assert(arr);

    const char *color = "gold";
    fprintf(file, DOT_INDENT "\"%p\" [label=\"", (void *)node);
    fprintf(file, "Parent: %p\\n", (void *)node->parent);
    fprintf(file, "Addr: %p\\n", (void *)node);
    fprintf(file, "Type: %s\\n", GetNodeTypeString(node->type));
    
    if (node->value.pos < arr->size) {
        fprintf(file, "Value: %s\\n", arr->var_array[node->value.pos].variable_name);
    } else {
        fprintf(file, "Value: INVALID_POS[%zu]\\n", node->value.pos);
    }
    
    fprintf(file, "Left: %p | Right: %p\"", (void *)node->left, (void *)node->right);
    fprintf(file, " shape=octagon color=black fillcolor=%s", color);
    fprintf(file, " style=filled width=4 height=1.5 fixedsize=true];\n");
}

static void WriteOperationNode(FILE *file, const LangNode_t *node) {
    assert(file);
    assert(node);
    
    GraphOperation op_info = PrintExpressionType(node);
    
    fprintf(file, DOT_INDENT "\"%p\" [label=\"{Parent: %p \\n | Addr: %p \\n | Type: %s", 
            (void *)node, (void *)node->parent, (void *)node, GetNodeTypeString(node->type));
    fprintf(file, " | Value: %s | {Left: %p | Right: %p}}\" shape=Mrecord color=black fillcolor=%s, style=filled];\n", 
            PrintExpressionType(node).operation_name, (void *)node->left, (void *)node->right, PrintExpressionType(node).color);
}


static void WriteUnknownNode(FILE *file, const LangNode_t *node) {
    assert(file);
    assert(node);

    fprintf(file, DOT_INDENT "\"%p\" [label=\"UNKNOWN NODE\\n", (void *)node);
    fprintf(file, "Type: %d\\n", node->type);
    fprintf(file, "Addr: %p\"", (void *)node);
    fprintf(file, " shape=ellipse color=red fillcolor=red style=filled];\n");
}

static void WriteNodeConnection(FILE *file, const LangNode_t *from_node, const LangNode_t *to_node) {
    assert(file);
    assert(from_node);
    assert(to_node);

    fprintf(file, DOT_INDENT "\"%p\" -> \"%p\";\n", (void *)from_node, (void *)to_node);
}

static const char *GetNodeTypeString(DifTypes type) {
    switch(type) {
        case kNumber:    return "NUM";
        case kVariable:  return "VAR";
        case kOperation: return "OP";
        default:         return "UNKNOWN";
    }
}

static void GenerateGraphImage(DumpInfo *info) {
    assert(info);

    snprintf(info->image_file, sizeof(info->image_file), "Images/graph_%zu.svg", info->graph_counter);
    info->graph_counter++;
    
    char cmd[MAX_COMMAND_SIZE] = {};
    snprintf(cmd, sizeof(cmd), "dot " FILE_OUT " -T svg -o %s", info->image_file);
    
    system(cmd);
}

static GraphOperation PrintExpressionType(const LangNode_t *node) {
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
        case (kOperationSQRT):
            return {"SQRT", "khaki3"};
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
        case (kOperationElse):
            return {"else", "navajowhite1"};
        case (kOperationThen):
            return {";", "pink"};
        case (kOperationWhile):
            return {"while", "rosybrown"};
        case (kOperationFunction):
            return {"func declare", "skyblue"};
        case (kOperationCall):
            return {"call func", "slategray1"};
        case (kOperationComma):
            return {",", "thistle"};
        case (kOperationWrite):
            return {"write", "	tan"};
        case (kOperationRead):
            return {"read", " tan"};
        case (kOperationReturn):
            return {"return", "tan"};
        case (kOperationB):
            return {"B", "lightgoldenrod"};
        case (kOperationBE):
            return {"BEQ", "lightgoldenrod"};
        case (kOperationA):
            return {"A", "lightgoldenrod"};
        case (kOperationAE):
            return {"AEQ", "lightgoldenrod"};
        case (kOperationE):
            return {"E", "lightgoldenrod"};
        case (kOperationNE):
            return {"NE", "lightgoldenrod"};
        case (kOperationHLT):
        return {"HLT", "lightpink"};

        case (kOperationParOpen):
        case (kOperationParClose):
        case (kOperationBraceOpen):
        case (kOperationBraceClose):
        case (kOperationNone):
        default: return {"red", "red"};
    }
}