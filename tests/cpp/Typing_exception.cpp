//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang++ OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang++ -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

#include <iostream>

int main(int argc, char** argv){
  try {
    if(argc>1)
      throw (42); // Throw an exception when a problem arise
    else
      throw('c');
  }
  catch (char myChar) {
    std::cout<<myChar;
  }
  catch (int myNum) {
    std::cout<<myNum;
  }
  return 0;
}