//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

int classify(int op, int a, int b) {
    int result;

    switch (op & 0x7) {
    case 0:
        result = a + b;
        break;
    case 1:
        result = a - b;
        if (result < 0)
            result = -result;
        break;
    case 2:
        result = a * b;
        break;
    case 3:
        result = (b != 0) ? a / b : a;
        break;
    case 4:
        result = a ^ b;
        break;
    case 5:
        result = (a > b) ? a : b;
        break;
    default:
        result = a | b;
        break;
    }

    switch ((result >> 4) & 0x3) {
    case 0:
        result = result + 100;
        break;
    case 1:
        result = result * 3;
        if (result > 10000)
            result = result % 997;
        break;
    case 2:
        result = result - 50;
        break;
    default:
        result = result ^ 0xFF;
        break;
    }

    if (result < 0)
        return -result % 10000;
    return result % 10000;
}

int main(int argc, char **argv) {
    int r = classify(5, 42, 17);
    return (r == classify(5, 42, 17)) ? 0 : 1;
}
