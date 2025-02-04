# anonymous structs are a Microsoft extension [-Wmicrosoft-anon-tag], and needs flag -fms-extensions to compile

zig cc -fms-extensions layout_html.c layout_style_tree.c render.c -o layout_html \
-I/opt/homebrew/opt/lexbor/include -L/opt/homebrew/opt/lexbor/lib -llexbor \
-I/opt/homebrew/Cellar/freetype/2.13.3/include/freetype2 -L/opt/homebrew/Cellar/freetype/2.13.3/lib -lfreetype