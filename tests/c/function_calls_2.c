//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

int foo(int a) {
  return a * 2;
}

int bar(int a, int b) {
  return (a + foo(b) * 2);
}

int fez(int a, int b, int c) {
  return (a + bar(a, b) * 2 + c * 3);
}

int main(int argc, char *argv[]) {
  int a = 123;
  int ret = 0;

  ret += foo(a);
  ret += bar(a, ret);
  ret += fez(a, ret, 123);

  return 0;
}