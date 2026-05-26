//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res 5
  //ENDFOR
//ENDFOR

#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    int i = atoi(argv[1]);
    switch (i) {
        case 1:
            printf("%d\n", i);
            break;
        case 2:
            printf("%d\n", i);
            break;
        case 3:
            printf("%d\n", i);
            break;
        default:
            printf("-1\n");
            break;
    }
}
