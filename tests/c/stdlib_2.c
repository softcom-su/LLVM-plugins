//FOR: LibPath CFG/libCFGFlattening.so CFG/libCFG.so MBA/libMBA.so TimeIntervalDetection/libTimeIntervalDetection.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/LibPath -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res 1 2 3 4
  //ENDFOR
//ENDFOR

#include <stdint.h>
#include <stdlib.h>

uint8_t foo(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  uint8_t e = c + d;
  uint8_t f = a + b;

  return e + f;
}

int main(int argc, char *argv[]) {
  int a = atoi(argv[1]), b = atoi(argv[2]), c = atoi(argv[3]),
      d = atoi(argv[4]);

  uint8_t ret = foo(a, b, c, d);
  return 0;
}
