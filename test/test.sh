zig cc -o test_strbuf test_strbuf.c ../lib/strbuf.c \
-lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include 
./test_strbuf

zig cc -o test_strview test_strview.c ../lib/strview.c \
-lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include 
./test_strview