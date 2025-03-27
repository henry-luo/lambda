zig cc -o test_strbuf.exe test_strbuf.c ../lib/strbuf.c \
-lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
-fms-extensions

zig cc -o test_strview.exe test_strview.c ../lib/strview.c \
-lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
-fms-extensions

zig cc -o test_layout_flex.exe test_layout_flex.c \
-lcriterion -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib \
-I/usr/local/include /usr/local/lib/libzlog.a

./test_strbuf.exe --verbose
./test_strview.exe --verbose
./test_layout_flex.exe --verbose