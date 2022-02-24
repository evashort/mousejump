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
uint32_t hashHelp(const wchar_t *string, uint64_t *hashConstants) {
    int hash = 0;
    do {
        wcsncpy((wchar_t*)hashVector, string, HASH_VECTOR_WCHAR_LENGTH);
        hash *= 31;
        hash += getVectorHash(hashVector, hashConstants, HASH_VECTOR_LENGTH);
        string += HASH_VECTOR_WCHAR_LENGTH;
    } while (((wchar_t*)hashVector)[HASH_VECTOR_WCHAR_LENGTH - 1] > 0);

    return hash;
}

uint32_t getHash1(const wchar_t *string) {
    return hashHelp(string, hashConstants1);
}

uint32_t getHash2(const wchar_t *string) {
    return hashHelp(string, hashConstants2);
}

// https://xlinux.nist.gov/dads/HTML/twoLeftHashing.html

typedef struct {
    void **table;
    uint32_t prime;
    void **tableStop;
    void **extraStart;
} HashTable;

// https://www.planetmath.org/goodhashtableprimes
const uint32_t goodHashTablePrimes[] = {
    // lots of small primes for testing automatic hash table resizing. the
    // smallest one I'd use in practice is 29
    1, 2, 3, 5, 7, 11, 17, 29, 53, 97, 193, 389, 769, 1543,
    3079, 6151, 12289, 24593, 49157,
    98317, 196613, 393241, 786433, 1572869,
    3145739, 6291469, 12582917, 25165843, 50331653,
    100663319, 201326611, 402653189, 805306457,
};

uint32_t getNextHashTablePrime(uint32_t lowerBound) {
    const uint32_t* prime = goodHashTablePrimes;
    const uint32_t *stop = goodHashTablePrimes + sizeof(goodHashTablePrimes);
    for (; prime < stop; prime++) {
        if (*prime >= lowerBound) { return *prime; }
    }

    return *(prime - 1);
}

int getHashTableCapacity(uint32_t prime) {
    // based on the output of collision_metrics.c, ignoring intercept
    // collisions = 0.3280992986 * prime + -0.8512436528
    // capacity = size + 2 * collisions
    return (int)((2 + 2 * 0.3280992986) * prime);
}

int getHashTablePrime(int expectedLoad) {
    // choose size of each table so that number of elements in both tables
    // combined equals expectedLoad when tables are 80% full (this relates to
    // the 80% load parameter in collision_metrics.c)
    int prime = getNextHashTablePrime((int)(expectedLoad * 0.625));
    return prime;
}

HashTable constructHashTable(int prime) {
    int capacity = getHashTableCapacity(prime);
    void **table = (void**)malloc(capacity * sizeof(void*));
    memset((void*)table, 0, capacity * sizeof(void*));
    HashTable result = {
        .table = table,
        .prime = prime,
        .tableStop = table + capacity,
        .extraStart = table + 2 * prime,
    };
    return result;
}

void destroyHashTable(HashTable hashTable) {
    free(hashTable.table);
}

void **hashTableGetNewParent(
    const HashTable this,
    const wchar_t *item,
    int comparator(const wchar_t *a, const wchar_t *b)
) {
    uint32_t hash1 = getHash1(item) % this.prime;
    void **entry1 = this.table + hash1;
    if (*entry1 == NULL) { return entry1; }

    uint32_t hash2 = getHash2(item) % this.prime;
    void **entry2 = this.table + this.prime + hash2;
    if (*entry2 == NULL) {
        if (*entry1 >= this.table && *entry1 < this.tableStop) {
            entry1 = (void**)(*entry1);
        }

        if (!comparator(item, *(wchar_t**)entry1)) { return NULL; }

        return entry2;
    }

    while (
        *entry1 >= this.table && *entry1 < this.tableStop
            && *entry2 >= this.table && *entry2 < this.tableStop
    ) {
        // both entries point to the overflow area.
        // entries in the overflow area are in pairs. the first entry in each
        // pair is always a string.
        entry1 = (void**)(*entry1);
        entry2 = (void**)(*entry2);
        if (
            !comparator(item, *(wchar_t**)entry1)
                || !comparator(item, *(wchar_t**)entry2)
        ) { return NULL; }

        // the second entry in each pair may be a string or a pointer to
        // another entry in the overflow area
        entry1++;
        entry2++;
    }

    void **insertAfter = entry1;
    void **otherBranch = entry2;
    if (*entry1 >= this.table && *entry1 < this.tableStop) {
        // insert on the second branch only if the first branch is strictly
        // longer
        insertAfter = entry2;
        otherBranch = entry1;
    }

    if (!comparator(item, *(wchar_t**)insertAfter)) { return NULL; }

    while (*otherBranch >= this.table && *otherBranch < this.tableStop) {
        otherBranch = (void**)(*otherBranch);
        if (!comparator(item, *(wchar_t**)otherBranch)) { return NULL; }

        otherBranch++;
    }

    if (!comparator(item, *(wchar_t**)otherBranch)) { return NULL; }

    return insertAfter;
}

int hashTableContains(const HashTable this, const wchar_t *item) {
    return !hashTableGetNewParent(this, item, wcscmp);
}

int alwaysGreaterStringCompare(const wchar_t *a, const wchar_t *b) {
    return 1;
}

int hashTableInsert(HashTable *this, wchar_t *item) {
    void **insertAfter = hashTableGetNewParent(*this, item, wcscmp);
    if (insertAfter == NULL) {
        return 0;
    } else if (*insertAfter == NULL) {
        *insertAfter = (void*)item;
        return 1;
    }

    int newPrime = this->prime;
    while (this->extraStart + 2 > this->tableStop) {
        // resize
        int lastPrime = newPrime;
        newPrime = getNextHashTablePrime(newPrime + 1);
        if (newPrime <= lastPrime) {
            return 1; // reached maximum size, lie and say we inserted it
        }

        HashTable replacement = constructHashTable(newPrime);
        void **entry = this->table;
        for (; entry < this->extraStart; entry++) {
            if (
                *entry != NULL
                    && (*entry < this->table || *entry >= this->extraStart)
            ) {
                void **newInsertAfter = hashTableGetNewParent(
                    replacement, *(wchar_t**)entry, alwaysGreaterStringCompare
                );
                if (*newInsertAfter == NULL) {
                    *newInsertAfter = *entry;
                } else if (
                    replacement.extraStart + 2 > replacement.tableStop
                ) {
                    break; // larger hash table somehow fits fewer items? hmm
                } else {
                    replacement.extraStart[0] = *newInsertAfter;
                    *newInsertAfter = (void*)replacement.extraStart;
                    replacement.extraStart[1] = *entry;
                    replacement.extraStart += 2;
                }
            }
        }

        if (entry < this->extraStart) {
            // larger hash table somehow fits fewer items? hmm
            destroyHashTable(replacement);
        } else {
            destroyHashTable(*this);
            *this = replacement;
            insertAfter = hashTableGetNewParent(*this, item, wcscmp);
            // assume item is not in table yet
            if (*insertAfter == NULL) {
                *insertAfter = (void*)item;
                return 1;
            }
        }
    }

    this->extraStart[0] = *insertAfter;
    *insertAfter = (void*)this->extraStart;
    this->extraStart[1] = item;
    this->extraStart += 2;
    return 1;
}

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
