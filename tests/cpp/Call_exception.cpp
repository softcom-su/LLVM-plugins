//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang++ OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang++ -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

#include <iostream>

void throw_exception(){
  throw (42);
}

void uncatch_exception(){
  throw_exception();
}

int main(int argc, char** argv){
  try {
    uncatch_exception();
  }
  catch (int myNum) {
    std::cout<<myNum;
  }
  return 0;
}