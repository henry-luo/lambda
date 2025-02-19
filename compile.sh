# anonymous structs are a Microsoft extension [-Wmicrosoft-anon-tag], and needs flag -fms-extensions to compile

zig cc -fms-extensions -o window \
html_window.c parse_html.c layout_html.c layout_dom_tree.c render.c view_pool.c event.c \
lib/string_buffer/string_buffer.c lib/mem-pool/src/variable.c lib/mem-pool/src/buffer.c lib/mem-pool/src/utils.c \
-I/opt/homebrew/opt/lexbor/include -L/opt/homebrew/opt/lexbor/lib -llexbor_static \
-I/opt/homebrew/Cellar/freetype/2.13.3/include/freetype2 /opt/homebrew/Cellar/freetype/2.13.3/lib/libfreetype.a \
/opt/homebrew/lib/libpng.a /opt/homebrew/opt/bzip2/lib/libbz2.a \
-I/opt/homebrew/include/fontconfig /opt/homebrew/lib/libfontconfig.a /opt/homebrew/opt/expat/lib/libexpat.a \
-L/opt/homebrew/lib /opt/homebrew/lib/libSDL2.a -lm -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox -Wl,-weak_framework,CoreHaptics -Wl,-weak_framework,GameController -Wl,-framework,ForceFeedback -lobjc -Wl,-framework,CoreVideo -Wl,-framework,Cocoa -Wl,-framework,Carbon -Wl,-framework,IOKit -Wl,-weak_framework,QuartzCore -Wl,-weak_framework,Metal \
-I/opt/homebrew/opt/sdl2/include/SDL2 -I/opt/homebrew/opt/sdl2_image/include -L/opt/homebrew/opt/sdl2_image/lib -lSDL2_image \
-I/opt/homebrew/include -L/opt/homebrew/lib -lThorVG \
/opt/homebrew/opt/zlib/lib/libz.a \
-Werror=incompatible-pointer-types \
-Werror=undef -Werror=no-common -Werror=uninitialized -Werror=sign-compare -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type \
-Werror=format -Werror=free-nonheap-object -Werror=shadow -Werror=array-bounds -Werror=null-dereference \
-Werror=pointer-arith -Werror=pointer-compare -Werror=pointer-sign -Werror=pointer-to-int-cast -Werror=pointer-truncate