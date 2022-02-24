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

    printf("%u %u\n", getHash1(L"hello"), getHash2(L"hello"));

    // fuzz test the hash table
    wchar_t *testStrings[] = {
        L"",
        L"a", L"b", L"c", L"d", L"e", L"f", L"g", L"h", L"i", L"j",
        L"k", L"l", L"m", L"n", L"o", L"p", L"q", L"r", L"s", L"t",
        L"u", L"v", L"w", L"x", L"y", L"z",
        L"aw", L"bi", L"cg", L"dj", L"ew", L"fr", L"gg", L"hi", L"in", L"jk",
        L"km", L"ly", L"mm", L"np", L"ok", L"ps", L"qt", L"rn", L"sm", L"ty",
        L"um", L"vi", L"wp", L"xo", L"ya", L"zz",
        L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab",
        L"baaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    };
    int testStringCount = sizeof(testStrings) / sizeof(testStrings[0]);
    uint8_t mask[sizeof(testStrings) / sizeof(testStrings[0])] = { 0 };
    // make hash table really small so it has to resize a bunch of times
    HashTable hashTable = constructHashTable(1);
    for (int iteration = 0; iteration < 1000; iteration++) {
        int i = rand() % testStringCount;
        wchar_t *testString = testStrings[i];
        int inserted = hashTableInsert(&hashTable, testString);
        if (inserted != !mask[i]) {
            wprintf(
                L"testString: %s, inserted: %d, !mask[i]: %d\n",
                testString, inserted, !mask[i]
            );
        }

        mask[i] = 1;
        for (int j = 0; j < testStringCount; j++) {
            int found = hashTableContains(hashTable, testStrings[j]);
            if (found != mask[j]) {
                wprintf(
                    L"inserted: %s, checking: %s, found: %d, mask[j]: %d\n",
                    testString, testStrings[j], found, mask[j]
                );
            }
        }
    }

    destroyHashTable(hashTable);

    printf("success\n");
    return 0;
}
