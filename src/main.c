#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "%s: invalid number of arguments\n", argv[0]);
        return 1;
    }

    printf("define i32 @main() {\n");
    printf("entry:\n");
    printf("  ret i32 %d\n", atoi(argv[1]));
    printf("}\n");
    return 0;
}
