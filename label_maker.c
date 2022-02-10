#include "label_maker.h"

int main() {
    Factorization factorization = { .factors = NULL };
    for (int n = 1; n <= 100; n++) {
        factorize(n, &factorization);
        printf("%d =", n);
        for (int i = 0; i < factorization.count; i++) {
            printf(
                " %d^%d",
                factorization.factors[i].factor,
                factorization.factors[i].count
            );
        }

        printf("\n");
    }

    free(factorization.factors);

    int maxSize = 2000;
    int limits[] = {7, 6, 4};
    int combinationCount = countSortedCombinations(limits, 3, maxSize / 3);
    printf("combination count: %d\n", combinationCount);

    int limits2[] = {4, 3, 2};
    int length = sizeof(limits2) / sizeof(limits2[0]);
    int capacity = countSortedCombinations(
        limits2 + 1, length - 1, maxSize / length
    );
    printf("capacity: %d\n", capacity);
    int *heap = (int*)malloc(length * capacity * sizeof(int));
    int count = 0;
    if (capacity > 0) {
        count = 1;
        for (int i = 0; i < length; i++) { heap[i] = 1; }
    }

    int i = 0;
    while (count > 0) {
        int product = 1;
        for (int i = 0; i < length; i++) { product *= heap[i]; }
        printf(
            "%d (%d): %d %d %d = %d\n",
            i, count, heap[0], heap[1], heap[2], product
        );
        count = popLowestProduct(heap, limits2, length, count, capacity);
        i++;
    }

    free(heap);
    return 0;
}
