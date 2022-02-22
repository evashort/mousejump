#include "label_maker.h"
#include <stdio.h>

int main() {
    Factorization factorization = { .factors = NULL };
    for (int n = 1; n <= 100; n++) {
        factorize(n, &factorization);
        printf("%d =", n);
        for (int i = 0; i < factorization.count; i++) {
            printf(
                " %d^%d",
                factorization.factors[i].factor,
                factorization.factors[i].copies
            );
        }

        printf("\n");
    }

    free(factorization.factors);

    int limits[] = {3, 2, 4};
    int length = sizeof(limits) / sizeof(limits[0]);
    JoinIndexGenerator generator = createJoinIndexGenerator(limits, length);
    for (int i = 0; nextJoinIndices(&generator); i++) {
        int product = 1;
        for (int j = 0; j < length; j++) { product *= generator.indices[j]; }
        printf(
            "%d: %d %d %d = %d\n",
            i + 1,
            generator.indices[0],
            generator.indices[1],
            generator.indices[2],
            product
        );
    }

    destroyJoinIndexGenerator(generator);
    printf("success\n");
    return 0;
}
