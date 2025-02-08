# anonymous structs are a Microsoft extension [-Wmicrosoft-anon-tag], and needs flag -fms-extensions to compile

zig cc -fms-extensions -o html_window \
html_window.c layout_html.c layout_style_tree.c render.c compute_style.c lib/string_buffer/string_buffer.c -lz \
-I/opt/homebrew/opt/lexbor/include -L/opt/homebrew/opt/lexbor/lib -llexbor \
-I/opt/homebrew/Cellar/freetype/2.13.3/include/freetype2 -L/opt/homebrew/Cellar/freetype/2.13.3/lib -lfreetype \
-I/opt/homebrew/include/fontconfig -L/opt/homebrew/lib -lfontconfig \
-I/opt/homebrew/opt/sdl2/include -L/opt/homebrew/opt/sdl2/lib -lSDL2 \
-Werror=undef -Werror=no-common -Werror=uninitialized -Werror=sign-compare -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type \
-Werror=format -Werror=free-nonheap-object -Werror=shadow -Werror=array-bounds -Werror=null-dereference \
-Werror=pointer-arith -Werror=pointer-compare -Werror=pointer-sign -Werror=pointer-to-int-cast -Werror=pointer-truncate