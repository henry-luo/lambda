clang -o test_strbuf.exe test_strbuf.c ../lib/strbuf.c ../lib/mem-pool/src/variable.c ../lib/mem-pool/src/buffer.c ../lib/mem-pool/src/utils.c \
-I../lib/mem-pool/include -lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
-fms-extensions

clang -o test_strview.exe test_strview.c ../lib/strview.c \
-lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
-fms-extensions

clang -o test_variable_pool.exe test_variable_pool.c ../lib/mem-pool/src/variable.c ../lib/mem-pool/src/buffer.c ../lib/mem-pool/src/utils.c \
-I../lib/mem-pool/include -lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
-fms-extensions

clang -o test_num_stack.exe test_num_stack.c ../lib/num_stack.c \
-lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
-fms-extensions

clang -o test_mime_detect.exe test_mime_detect.c ../lambda/input/mime-detect.c ../lambda/input/mime-types.c \
-lcriterion -L/opt/homebrew/Cellar/criterion/2.4.2_2/lib -I/opt/homebrew/Cellar/criterion/2.4.2_2/include \
-fms-extensions

./test_strbuf.exe --verbose
./test_strview.exe --verbose
./test_variable_pool.exe --verbose
./test_num_stack.exe --verbose
./test_mime_detect.exe --verbose