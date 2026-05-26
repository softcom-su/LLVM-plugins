//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res 1 2 3 4
  //ENDFOR
//ENDFOR

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  int a = atoi(argv[1]), b = atoi(argv[2]), c = atoi(argv[3]),
      d = atoi(argv[4]);

  int e = a - b;
  int f = c - d;

  return e - f;
}
