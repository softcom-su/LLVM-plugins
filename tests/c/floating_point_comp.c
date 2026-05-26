//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

#include <math.h>

double sqrt_impl(double x, double hi, double lo) {
  // First direct floating-point equality comparison
  if (fabs(hi - lo) < 0.1) {
    return lo;
  }

  double midpoint = (lo + hi + 1.0) / 2.0;
  if (x / midpoint < midpoint) {
    return sqrt_impl(x, lo, midpoint - 1.0);
  }

  return sqrt_impl(x, midpoint, hi);
}

double sqrt(double x) { return sqrt_impl(x, 0, x / 2.0 + 1.0); }

int foo() {
  double a = 0.2;
  double b = 1.0 / sqrt(5.0) / sqrt(5.0);
  // Second direct floating-point equality comparison
  if (b == 1.0)
    return a == b ? 1 : 0;
  else
    return a == b ? 0 : 1;
}

int main(int argc, char** argv){
  foo();
  return 0;
}