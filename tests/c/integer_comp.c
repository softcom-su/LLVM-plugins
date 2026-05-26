//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

int foo(int arg_1) { 
	int a = 43;
	int b = 45;
	int c = a + b;
	return c + arg_1; 
}

int main(int argc, char** argv){
  foo(42);
  return 0;
}