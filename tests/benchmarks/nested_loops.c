//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

int accumulate(int n, int seed, int step) {
    int result = seed;
    int i, j;

    for (i = 0; i < n && i < 10; i++) {
        if (i % 3 == 0) {
            result += step * i;
        } else if (i % 3 == 1) {
            result -= i;
        } else {
            result ^= (i * step);
        }

        for (j = 0; j < i && j < 5; j++) {
            result += j * 3 + 1;
            if (result > 50000)
                result = result % 997;
        }
    }

    int countdown = (result & 0xF) + 3;
    while (countdown > 0) {
        if (countdown % 2 == 0) {
            result = result + countdown;
        } else {
            result = result ^ countdown;
        }
        countdown--;
    }

    for (i = 1; i <= 4; i++) {
        int tmp = result * i;
        if (tmp < 0)
            tmp = -tmp;
        result = tmp % 10007;
    }

    if (result < 0)
        return -result;
    return result;
}

int main(int argc, char **argv) {
    int r = accumulate(8, 42, 7);
    return (r == accumulate(8, 42, 7)) ? 0 : 1;
}
