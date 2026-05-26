//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

int foo(int a, int b, int c) {
  int result = 123 + a;

  if (a > 0) {
    int d = a * b;
    int e = b / c;
    if (d == e) {
      int f = d * e;
      result = result - 2*f;
    } else {
      int g = 987;
      result = g * c * e;
    }
  } else {
    result = 321;
  }

  return result;
}

int main(int argc, char** argv){
  return foo(1, 2, 3);
}
