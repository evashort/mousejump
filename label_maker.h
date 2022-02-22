#ifndef LABEL_MAKER_H
#define LABEL_MAKER_H
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// https://en.wikipedia.org/wiki/Universal_hashing#Hashing_vectors
#define HASH_VECTOR_LENGTH 8
#define HASH_VECTOR_WCHAR_LENGTH ( \
    HASH_VECTOR_LENGTH * sizeof(uint32_t) / sizeof(wchar_t) \
)
uint64_t hashConstants1[1 + HASH_VECTOR_LENGTH];
uint64_t hashConstants2[1 + HASH_VECTOR_LENGTH];
int hashConstantsInitialized = 0;
uint32_t hashVector[HASH_VECTOR_LENGTH];
uint32_t getVectorHash(uint32_t *vector, uint64_t *constants, int length) {
    if (!hashConstantsInitialized) {
        hashConstantsInitialized = 1;
        uint64_t *constantLists[] = { hashConstants1, hashConstants2 };
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 1 + HASH_VECTOR_LENGTH; j++) {
                uint64_t constant = 0; constant += rand() & 0x7fff;
                constant <<= 15; constant += rand() & 0x7fff;
                constant <<= 15; constant += rand() & 0x7fff;
                constant <<= 15; constant += rand() & 0x7fff;
                constant <<= 15; constant += rand() & 0x7fff;
                constantLists[i][j] = constant;
            }
        }
    }

    uint64_t hash = constants[0];
    for (int i = 0; i < length; i++) {
        hash += constants[i + 1] * (uint64_t)vector[i];
    }

    return (uint32_t)(hash >> 32);
}

// https://en.wikipedia.org/wiki/Universal_hashing#Hashing_strings
uint32_t hashHelp(wchar_t *string, uint64_t *hashConstants) {
    int hash = 0;
    do {
        wcsncpy((wchar_t*)hashVector, string, HASH_VECTOR_WCHAR_LENGTH);
        hash *= 31;
        hash += getVectorHash(hashVector, hashConstants, HASH_VECTOR_LENGTH);
        string += HASH_VECTOR_WCHAR_LENGTH;
    } while (((wchar_t*)hashVector)[HASH_VECTOR_WCHAR_LENGTH - 1] > 0);

    return hash;
}

uint32_t hash1(wchar_t *string) { return hashHelp(string, hashConstants1); }
uint32_t hash2(wchar_t *string) { return hashHelp(string, hashConstants2); }

// https://xlinux.nist.gov/dads/HTML/twoLeftHashing.html

int gcd(a, b) {
    while (b != 0)  {
        int remainder = a % b;
        a = b;
        b = remainder;
    }

    return a;
}

// https://en.wikipedia.org/wiki/Pollard's_rho_algorithm#Variants
// https://en.wikipedia.org/wiki/Cycle_detection#Brent's_algorithm
int brentPollardRho(n, hare, constant) {
    if (n == 1) {
        return 1;
    }

    int tortoise = hare;
    int cycleLength = 0;
    int maxLength = 1;
    int divisor = 1;
    while (divisor == 1) {
        if (cycleLength >= maxLength) {
            tortoise = hare;
            maxLength += maxLength;
            cycleLength = 0;
        }

        hare = (hare * hare + constant) % n;
        cycleLength++;
        divisor = gcd(abs(tortoise - hare), n);
    }

    return divisor;
}

int findDivisor(n) {
    if (n == 1) {
        return 1;
    }

    if (n % 2 == 0) {
        return 2;
    }

    if (
        n == 3 || n == 5 || n == 7 || n == 11 || n == 13 || n == 17 || n == 19
            || n == 23 || n == 29 || n == 31 || n == 37 || n == 41 || n == 43
    ) {
        return n;
    }

    int maxTries = 10; // non-critical application, don't try too hard
    int hare = 2;
    int constant = 1;
    int divisor = brentPollardRho(n, hare, constant);
    for (int i = 0; divisor >= n && i < maxTries; i++) {
        hare = rand() % n;
        constant = rand() % (n - 1);
        // don't use n-2, see
        // https://mathworld.wolfram.com/PollardRhoFactorizationMethod.html
        constant += constant >= n - 2;
        divisor = brentPollardRho(n, hare, constant);
    }

    return divisor;
}

typedef struct {
    int factor;
    int copies;
} RepeatedFactor;

typedef struct {
    RepeatedFactor *factors;
    int count;
    int capacity;
} Factorization;

int compareFactors(const void *a, const void *b) {
    return ((RepeatedFactor*)a)->factor - ((RepeatedFactor*)b)->factor;
}

void factorize(int n, Factorization *out) {
    if (n == 1) {
        out->count = 0;
        return;
    }

    if (out->factors == NULL) {
        out->capacity = 16;
        out->factors = malloc(out->capacity * sizeof(RepeatedFactor));
    }

    out->factors[0].factor = n;
    int factorCount = 1;
    int primeCount = 0;
    while (primeCount < factorCount) {
        int n = out->factors[primeCount].factor;
        int divisor = findDivisor(n);
        if (divisor >= n) {
            primeCount++;
        } else {
            out->factors[primeCount].factor /= divisor;
            if (factorCount >= out->capacity) {
                out->capacity *= 2;
                out->factors = realloc(
                    out->factors,
                    out->capacity * sizeof(RepeatedFactor)
                );
            }

            out->factors[factorCount].factor = divisor;
            factorCount++;
        }
    }

    qsort(
        out->factors,
        factorCount,
        sizeof(RepeatedFactor),
        compareFactors
    );
    out->count = 0;
    int lastFactor = 1;
    for (int i = 0; i < factorCount; i++) {
        int thisFactor = out->factors[i].factor;
        if (thisFactor == lastFactor) {
            out->factors[out->count - 1].copies++;
        } else {
            out->factors[out->count].factor = thisFactor;
            out->factors[out->count].copies = 1;
            out->count++;
            lastFactor = thisFactor;
        }
    }
}

typedef struct {
    int *limits;
    int length;
    int product;
    int maxProduct;
    Factorization factorization;
    int *indices;
} JoinIndexGenerator;

JoinIndexGenerator createJoinIndexGenerator(int *limits, int length) {
    JoinIndexGenerator generator = {
        .limits = limits,
        .length = length,
        .product = 0,
        .maxProduct = 1,
        .factorization = {
            .factors = NULL,
            .count = 0,
            .capacity = 0,
        },
        .indices = malloc(length * sizeof(int)),
    };
    for (int i = 0; i < length; i++) {
        generator.maxProduct *= limits[i];
        generator.indices[i] = 1;
    }

    return generator;
}

void destroyJoinIndexGenerator(JoinIndexGenerator generator) {
    free(generator.factorization.factors);
    free(generator.indices);
}

int nextJoinIndices(JoinIndexGenerator *generator) {
    int length = generator->length;
    int *indices = generator->indices;
    int factorIndex = generator->factorization.count - 1;
    for (;;) {
        while (factorIndex >= 0) {
            int factor = generator->factorization.factors[factorIndex].factor;
            int *copies = &(
                generator->factorization.factors[factorIndex].copies
            );
            // first index to add more copies of the factor to
            int increaseStart = 0;
            // if all copies are used, steal a copy from the first index that
            // has one
            for (; increaseStart < length && *copies <= 0; increaseStart++) {
                int shrunkIndex = indices[increaseStart] / factor;
                if (indices[increaseStart] == shrunkIndex * factor) {
                    indices[increaseStart] = shrunkIndex;
                    *copies += 1;
                }
            }

            int src = -2;
            // add the copies as early as possible after the start
            for (; increaseStart < length && *copies > 0; increaseStart++) {
                int grownIndex = indices[increaseStart] * factor;
                while (
                    grownIndex <= generator->limits[increaseStart]
                        && *copies > 0
                ) {
                    indices[increaseStart] = grownIndex;
                    *copies -= 1;
                    grownIndex *= factor;
                    if (src < -1) {
                        src = increaseStart - 1;
                    }
                }
            }

            if (*copies > 0) {
                // ran out of ways to distribute this factor
                // remove all copies of it
                for (int i = 0; i < length; i++) {
                    int shrunkIndex = indices[i] / factor;
                    while (indices[i] == shrunkIndex * factor) {
                        indices[i] = shrunkIndex;
                        *copies += 1;
                        shrunkIndex /= factor;
                    }
                }

                factorIndex--; // backtrack
            } else {
                // move copies in the non-increased region to the beginning
                int dst = 0;
                while (src > dst) {
                    int shrunkIndex = indices[src] / factor;
                    int grownIndex = indices[dst] * factor;
                    if (shrunkIndex * factor < indices[src]) {
                        src--;
                    } else if (grownIndex > generator->limits[dst]) {
                        dst++;
                    } else {
                        indices[src] = shrunkIndex;
                        indices[dst] = grownIndex;
                    }
                }

                factorIndex++; // distribute the next factor
                if (factorIndex >= generator->factorization.count) {
                    return 1; // we did it
                }
            }
        }

        generator->product++;
        if (generator->product > generator->maxProduct) {
            return 0; // that's all
        }

        factorize(generator->product, &generator->factorization);
        if (generator->factorization.count <= 0) {
            return 1; // edge case when product = 1
        }

        factorIndex = 0;
    }
}

#endif
