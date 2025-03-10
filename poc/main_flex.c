#include <stdio.h>
#include "flexbox.h"

int main() {
    const char* html = 
        "<html><head><style>"
        ".container { display: flex; width: 500px; height: 300px; flex-direction: row; justify-content: space-around; align-items: center; flex-wrap: wrap; align-content: space-between; }"
        ".item { flex-basis: 200px; height: 50px; }"
        "</style></head><body>"
        "<div class=\"container\">"
            "<div class=\"item\">Item 1</div>"
            "<div class=\"item\">Item 2</div>"
        "</div></body></html>";

    FlexNode* root = parseHTMLandCSS(html);
    if (!root) {
        printf("Failed to parse HTML/CSS\n");
        return 1;
    }

    calculateFlexLayout(root, NULL);

    for (int i = 0; i < root->num_children; i++) {
        FlexNode* item = root->children[i];
        printf("Item %d: Main Pos=%d, Cross Pos=%d, Main Size=%d, Cross Size=%d\n",
               i, item->position_main, item->position_cross, item->main_size, item->cross_size);
    }

    destroyFlexNode(root);
    return 0;
}

// clang -o flexbox.exe main_flex.c layout_flex.c ../lib/strbuf.c  -I/usr/local/include /usr/local/lib/liblexbor_static.a