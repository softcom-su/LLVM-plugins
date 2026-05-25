//FOR: LibName libCFGFlattening.so libCFG.so
  //FOR: OptArg -O0 -O1 -O2 -O3 -Ofast -Os
    //RUN: clang OptArg -fpass-plugin=build/libs/CFG/LibName -c -o %file%.o %file%
    //RUN: clang -o %file%.res %file%.o
    //RUN: %file%.res
  //ENDFOR
//ENDFOR

#define QROUND(a, b, c, d, k) do { \
    (a) += (b) * (k);              \
    (b) ^= (a) >> 16;              \
    (c) = (c) * (k) + (b);         \
    (d) ^= (c) >> 11;              \
    (a) += (d) * ((k) >> 8);       \
} while(0)

int heavy_compute(int a, int b, int c, int d) {
    unsigned int r0 = (unsigned int)a + 0x9e3779b9u;
    unsigned int r1 = (unsigned int)b + 0x517cc1b7u;
    unsigned int r2 = (unsigned int)c + 0x6a09e667u;
    unsigned int r3 = (unsigned int)d + 0xbb67ae85u;

    QROUND(r0, r1, r2, r3, 0xcc9e2d51u);
    QROUND(r1, r2, r3, r0, 0x1b873593u);
    QROUND(r2, r3, r0, r1, 0x85ebca6bu);
    QROUND(r3, r0, r1, r2, 0xc2b2ae35u);

    if (r0 > r1) {
        QROUND(r0, r1, r2, r3, 0x41c64e6du);
        QROUND(r1, r2, r3, r0, 0x27bb2ee7u);
        QROUND(r2, r3, r0, r1, 0x045d9f3bu);
        QROUND(r3, r0, r1, r2, 0x27d4eb2du);
    } else {
        QROUND(r0, r1, r2, r3, 0x119de1f3u);
        QROUND(r1, r2, r3, r0, 0x165667b1u);
        QROUND(r2, r3, r0, r1, 0x16a6b036u);
        QROUND(r3, r0, r1, r2, 0x159a55e5u);
    }

    if (r2 > r3) {
        QROUND(r0, r1, r2, r3, 0x01000193u);
        QROUND(r1, r2, r3, r0, 0xcbf29ce4u);
        QROUND(r2, r3, r0, r1, 0x735a2d97u);
        QROUND(r3, r0, r1, r2, 0x1f123bb5u);
    } else {
        QROUND(r0, r1, r2, r3, 0x3c6ef372u);
        QROUND(r1, r2, r3, r0, 0xa54ff53au);
        QROUND(r2, r3, r0, r1, 0x510e527fu);
        QROUND(r3, r0, r1, r2, 0x9b05688cu);
    }

    for (int i = 0; i < 5; i++) {
        QROUND(r0, r1, r2, r3, 0xcc9e2d51u + (unsigned int)i * 111u);
        QROUND(r1, r2, r3, r0, 0x85ebca6bu + (unsigned int)i * 222u);
        QROUND(r2, r3, r0, r1, 0x27d4eb2du + (unsigned int)i * 333u);
        QROUND(r3, r0, r1, r2, 0xc2b2ae35u + (unsigned int)i * 444u);

        if (r0 & (1u << ((unsigned int)i + 3u))) {
            QROUND(r0, r2, r1, r3, 0x41c64e6du);
            QROUND(r1, r3, r0, r2, 0x5deece66u);
        } else {
            QROUND(r0, r3, r2, r1, 0x27bb2ee7u);
            QROUND(r1, r2, r3, r0, 0x119de1f3u);
        }
    }

    if (r0 > r2) {
        if (r1 > r3) {
            QROUND(r0, r1, r2, r3, 0x6a09e667u);
            QROUND(r1, r2, r3, r0, 0xbb67ae85u);
            QROUND(r2, r3, r0, r1, 0x3c6ef372u);
            QROUND(r3, r0, r1, r2, 0xa54ff53au);
        } else {
            QROUND(r0, r1, r2, r3, 0x510e527fu);
            QROUND(r1, r2, r3, r0, 0x9b05688cu);
            QROUND(r2, r3, r0, r1, 0x1f83d9abu);
            QROUND(r3, r0, r1, r2, 0x5be0cd19u);
        }
    } else {
        QROUND(r0, r1, r2, r3, 0x428a2f98u);
        QROUND(r1, r2, r3, r0, 0x71374491u);
        QROUND(r2, r3, r0, r1, 0xb5c0fbcfu);
        QROUND(r3, r0, r1, r2, 0xe9b5dba5u);
    }

    unsigned int sel = r0 & 3u;
    if (sel == 0u) {
        QROUND(r0, r1, r2, r3, 0x3956c25bu);
        QROUND(r1, r2, r3, r0, 0x59f111f1u);
        QROUND(r2, r3, r0, r1, 0x923f82a4u);
    } else if (sel == 1u) {
        QROUND(r0, r1, r2, r3, 0xab1c5ed5u);
        QROUND(r1, r2, r3, r0, 0xd807aa98u);
        QROUND(r2, r3, r0, r1, 0x12835b01u);
    } else if (sel == 2u) {
        QROUND(r0, r1, r2, r3, 0x243185beu);
        QROUND(r1, r2, r3, r0, 0x550c7dc3u);
        QROUND(r2, r3, r0, r1, 0x72be5d74u);
    } else {
        QROUND(r0, r1, r2, r3, 0x80deb1feu);
        QROUND(r1, r2, r3, r0, 0x9bdc06a7u);
        QROUND(r2, r3, r0, r1, 0xc19bf174u);
    }

    for (int j = 0; j < 4; j++) {
        QROUND(r0, r1, r2, r3, 0xe49b69c1u + (unsigned int)j * 555u);
        QROUND(r1, r2, r3, r0, 0xefbe4786u + (unsigned int)j * 666u);
        QROUND(r2, r3, r0, r1, 0x0fc19dc6u + (unsigned int)j * 777u);
        QROUND(r3, r0, r1, r2, 0x240ca1ccu + (unsigned int)j * 888u);
    }

    r0 ^= r1 ^ r2 ^ r3;
    r0 = (r0 ^ (r0 >> 16)) * 0x45d9f3bu;
    r0 = (r0 ^ (r0 >> 16)) * 0xc2b2ae35u;
    r0 = r0 ^ (r0 >> 16);

    return (int)(r0 % 10000u);
}

int main(int argc, char **argv) {
    int r = heavy_compute(5, 3, 4, 7);
    return (r == heavy_compute(5, 3, 4, 7)) ? 0 : 1;
}
