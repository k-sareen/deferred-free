valgrind --leak-check=full --show-leak-kinds=all ./fork-malloc
valgrind --leak-check=full --show-leak-kinds=all ./pthread-malloc
gcc -O2 -I. -L. fork-malloc.c -l:quarantine.so -Wl,-rpath=`pwd` -o fork-malloc
gcc -O2 -I. -L. pthread-malloc.c -pthread -l:quarantine.so -Wl,-rpath=`pwd` -o pthread-malloc
gcc -O2 quarantine.c -o quarantine.so -shared -fPIC -pthread
