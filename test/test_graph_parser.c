#include <criterion/criterion.h>
#include "../lambda/input/input.h"
#include "../lambda/input/input-graph.h"

Test(graph_parser, test_dot_graph_parsing) {
    const char* dot_content = 
        "digraph test {\n"
        "    A -> B [label=\"edge1\"];\n"
        "    B -> C;\n"
        "}";
    
    Input* input = input_from_source(dot_content, NULL, 
                                   &(String){.len=5, .chars="graph"}, 
                                   &(String){.len=3, .chars="dot"});
    
    cr_assert_not_null(input);
    cr_assert_eq(input->root.type_id, LMD_TYPE_ELEMENT);
    
    Element* graph = (Element*)input->root.container;
    cr_assert_not_null(graph);
    
    // Clean up
    if (input) {
        pool_destroy(input->pool);
        arraylist_free(input->type_list);
        free(input);
    }
}

Test(graph_parser, test_mermaid_graph_parsing) {
    const char* mermaid_content = 
        "flowchart TD\n"
        "    A[Start] --> B[End]\n";
    
    Input* input = input_from_source(mermaid_content, NULL, 
                                   &(String){.len=5, .chars="graph"}, 
                                   &(String){.len=7, .chars="mermaid"});
    
    cr_assert_not_null(input);
    cr_assert_eq(input->root.type_id, LMD_TYPE_ELEMENT);
    
    Element* graph = (Element*)input->root.container;
    cr_assert_not_null(graph);
    
    // Clean up
    if (input) {
        pool_destroy(input->pool);
        arraylist_free(input->type_list);
        free(input);
    }
}