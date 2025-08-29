// test_dir.cpp
// Criterion unit test for directory listing via input_from_directory

#include <criterion/criterion.h>
#include "../lambda/input/input.h"
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

Test(input_dir, list_current_directory) {
    const char* dir = ".";
    Input* input = input_from_directory(dir, false, 1);
    cr_assert_not_null(input, "input_from_directory returned NULL");
    cr_assert(input->root.type_id == LMD_TYPE_ELEMENT, "Root is not an element");
    Element* root = (Element*)input->root.raw_pointer;
    cr_assert(root != NULL, "Root element is NULL");
    // Check that at least one child exists (should have files/dirs)
    int found = 0;
    TypeElmt* type = (TypeElmt*)root->type;
    ShapeEntry* entry = type->shape;
    while (entry) {
        found = 1;
        break;
    }
    cr_assert(found, "No entries found in directory listing");
}
