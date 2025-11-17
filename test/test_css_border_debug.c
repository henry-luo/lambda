#include <criterion/criterion.h>
#include "../lambda/input/input-css.cpp"

Test(CssBorderDebug, ParseBorderProperty) {
    const char* css = "input[type=\"text\"] { border: 1px solid #ccc; }";

    // Parse the CSS
    Element* root = parse_css_string(css, nullptr);

    // Verify we got something
    cr_assert_not_null(root, "Should parse CSS");

    // Print the structure to debug
    printf("\n=== Parsed CSS ===\n");
    printf("Root element: %s\n", root->type->atom);
    printf("Number of children: %d\n", root->num);

    // Look for the input rule
    for (int i = 0; i < root->num; i++) {
        Element* child = root->children[i];
        printf("Child %d: %s (num=%d)\n", i, child->type->atom, child->num);

        // If this is a rule, print its properties
        if (strcmp(child->type->atom, "rule") == 0) {
            // First child should be selector
            if (child->num > 0) {
                Element* selector = child->children[0];
                printf("  Selector: %s\n", selector->type->atom);
            }

            // Second child should be declarations block
            if (child->num > 1) {
                Element* block = child->children[1];
                printf("  Declarations block: %s (num=%d)\n", block->type->atom, block->num);

                // Print each declaration
                for (int j = 0; j < block->num; j++) {
                    Element* decl = block->children[j];
                    printf("    Declaration %d: %s (num=%d)\n", j, decl->type->atom, decl->num);

                    // If this is a declaration, print property name
                    if (strcmp(decl->type->atom, "declaration") == 0 && decl->num >= 2) {
                        Element* prop_name = decl->children[0];
                        Element* prop_values = decl->children[1];
                        printf("      Property: %s\n", prop_name->atom);
                        printf("      Values: %s (num=%d)\n", prop_values->type->atom, prop_values->num);

                        // Print each value
                        for (int k = 0; k < prop_values->num; k++) {
                            Element* val = prop_values->children[k];
                            printf("        Value %d: %s = \"%s\"\n", k, val->type->atom, val->atom ? val->atom : "(null)");
                        }
                    }
                }
            }
        }
    }

    // Verify the border property has 3 values
    Element* rule = root->children[0];
    Element* block = rule->children[1];
    Element* border_decl = nullptr;

    for (int i = 0; i < block->num; i++) {
        Element* decl = block->children[i];
        if (strcmp(decl->type->atom, "declaration") == 0 && decl->num >= 2) {
            Element* prop_name = decl->children[0];
            if (strcmp(prop_name->atom, "border") == 0) {
                border_decl = decl;
                break;
            }
        }
    }

    cr_assert_not_null(border_decl, "Should find border declaration");
    cr_assert_eq(border_decl->num, 2, "Border declaration should have 2 children (name + values)");

    Element* border_values = border_decl->children[1];
    printf("\n=== Border values count: %d ===\n", border_values->num);
    cr_assert_eq(border_values->num, 3, "Border should have 3 values: '1px', 'solid', '#ccc'");

    // Clean up
    free_element(root);
}
