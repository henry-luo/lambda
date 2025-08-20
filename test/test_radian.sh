clang -o test_layout_flex.exe test_layout_flex.c \
-lcriterion -I/opt/homebrew/Cellar/criterion/2.4.2_2/include -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib \
-I/usr/local/include -fms-extensions

./test_layout_flex.exe --verbose