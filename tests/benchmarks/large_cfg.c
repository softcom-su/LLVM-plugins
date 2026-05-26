//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

int compute(int a, int b, int c, int d) {
    int result = 0;
    int tmp;

    if (a > 0) {
        result = a * 3 + 7;
    } else {
        result = -a + 13;
    }

    if (b > result) {
        tmp = b - result;
        if (tmp > 10) {
            result = tmp * 2;
        } else {
            result = tmp + result;
        }
    } else {
        result = result + b * 2;
    }

    for (int i = 0; i < c && i < 8; i++) {
        if (i % 2 == 0) {
            result += i * a;
        } else {
            result -= i;
        }
    }

    int selector = (result ^ d) & 0x7;
    if (selector < 2) {
        result = result + d;
    } else if (selector < 4) {
        result = result - d;
        if (result < 0)
            result = -result;
    } else if (selector < 6) {
        result = result * 3;
    } else {
        result = result / (d != 0 ? d : 1);
        result += 42;
    }

    int acc = 1;
    for (int j = 0; j < 4; j++) {
        acc = acc * (result & 0xFF) + j;
        if (acc > 10000)
            acc = acc % 997;
    }
    result ^= acc;

    if (a + b > c) {
        if (c + d > a) {
            result += (a ^ b ^ c ^ d);
        } else {
            result -= d;
        }
    } else {
        result += 100;
    }

    tmp = result & 0xF;
    if (tmp < 4) {
        result = result << 1;
    } else if (tmp < 8) {
        result = result >> 1;
    } else if (tmp < 12) {
        result = result + tmp * tmp;
    } else {
        result = result - tmp;
    }

    if (result > 0) {
        return result % 10000;
    } else {
        return (-result) % 10000;
    }
}

int main(int argc, char **argv) {
    int r = compute(5, 3, 4, 7);
    return (r == compute(5, 3, 4, 7)) ? 0 : 1;
}
