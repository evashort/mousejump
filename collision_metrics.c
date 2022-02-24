#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int countCollisions(int length, int load, uint8_t *t1, uint8_t *t2) {
    memset((void*)t1, 0, sizeof(t1[0]) * length);
    memset((void*)t2, 0, sizeof(t2[0]) * length);
    int collisions = 0;
    for (int i = 0; i < load; i++) {
        int i = rand() % length;
        int j = rand() % length;
        if (t1[i]) {
            if (t2[i]) {
                collisions++;
            }

            t2[i] = 1;
        }

        t1[i] = 1;
    }
    return collisions;
}

int compareInts(const void *a, const void *b) {
    return (*(int*)a > *(int*)b) - (*(int*)a < *(int*)b);
}

int percentileCollisions(int length, int load, int tries, double percentile) {
    uint8_t *t1 = (uint8_t*)malloc(length * sizeof(uint8_t));
    uint8_t *t2 = (uint8_t*)malloc(length * sizeof(uint8_t));
    int *results = (int*)malloc(tries * sizeof(int));
    for (int i = 0; i < tries; i++) {
        results[i] = countCollisions(length, load, t1, t2);
    }

    free((void*)t1);
    free((void*)t2);
    qsort((void*)results, length, sizeof(results[0]), compareInts);
    int result = results[(int)(percentile * tries) - 1];
    free(results);
    return result;
}

int main() {
    int xs[] = {
        2, 4, 8, 0x10, 0x18, 0x20, 0x30, 0x40, 0x60, 0x80, 0xc0,
        0x100, 0x180, 0x200, 0x300, 0x400, 0x600, 0x800, 0xc00,
        0x1000, 0x1200, 0x1800, 0x1c00, 0x2000
    };
    int points = sizeof(xs) / sizeof(xs[0]);
    int ys[sizeof(xs) / sizeof(xs[0])];
    for (int i = 0; i < points; i++) {
        int length = xs[i];
        // hash table size is actually 2 * length under 2-left hashing
        // https://xlinux.nist.gov/dads/HTML/twoLeftHashing.html
        // count collisions when the hash table is 80% full
        // find an upper bound that works 90% of the time
        int load = (int)(2 * length * 0.8);
        int collisions = percentileCollisions(length, load, 10000, 0.9);
        ys[i] = collisions;
        printf( "%d %d\n", length, collisions);
    }

    // https://www.tutorialspoint.com/machine_learning_with_python/regression_algorithms_linear_regression.htm
    double mean_x = 0, mean_y = 0;
    for (int i = 0; i < points; i++) { mean_x += xs[i]; } mean_x /= points;
    for (int i = 0; i < points; i++) { mean_y += ys[i]; } mean_y /= points;
    double SS_xy = -points * mean_y * mean_x;
    for (int i = 0; i < points; i++) { SS_xy += ys[i] * xs[i]; }
    double SS_xx = -points * mean_x * mean_x;
    for (int i = 0; i < points; i++) { SS_xx += xs[i] * xs[i]; }
    double slope = SS_xy / SS_xx;
    double intercept = mean_y - slope*mean_x;
    printf("collisions = %.10f * length + %.10f", slope, intercept);
    // collisions = 0.3280992986 * length + -0.8512436528
    return 0;
}
