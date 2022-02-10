#ifndef LABEL_MAKER_H
#define LABEL_MAKER_H

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int countSortedCombinations(int *limits, int length, int maxCount) {
    // returns the number of possible nonincreasing lists of positive integers
    // with the given length such that each element is not higher than the
    // corresponding limit. limits must be nonincreasing and have the given
    // length. if the result would be greater than maxCount or an intermediate
    // calculation would cause an integer overflow, returns maxCount.
    //
    // here's what the formula looks like on paper for length = 5, where a, b,
    // c, d, and e are the 5 elements of limits, and (n k) means n choose k:
    // abcde
    // - abc(e 2) - ab(d 2)e - a(c 2)de - (b 2)cde
    // + ab(e 3)  + a(d 3)e  + (c 3)de
    // - a(e 4)   - (d 4)e
    // + (e 5)
    //
    // in this code, (e 5) would be calculated from the previous term -a(e 4):
    // (e 5) = -a(e 4) * (4 - e) / 5a
    // this works because
    // (n k) = n! / (k! * (n - k)!)
    //       = n! * (n - k + 1) / (k * (k - 1)! * (n - k + 1)!)
    //       = (n k-1) * (n - k + 1) / k
    //
    // here's a specific example with limits = [7, 6, 4]. this illustration is
    // a top view. imagine each number is a column of that many blocks.
    //      44
    //     444
    //    4444
    //   33333
    //  222222
    // 1111111
    //
    // 7 * 6 * 4   // entire rectangular prism
    // - 7 * (4 2) // right triangular prism that fills the space over the
    //             // bottom 3 rows: 1111111
    //             //                2222222
    //             //                3333333
    // - 4 * (6 2) // right triangular prism that fills the upper left corner:
    //             // 44444
    //             // 4444
    //             // 444
    //             // 44
    //             // 4
    // + (4 3)     // tetrahedral intersection of the two right triangular
    //             // prisms: 11
    //             //         2
    // = 168 - 42 - 60 + 4 = 70

    if (length > 0 && limits[length - 1] < 1) { return 0; }

    int firstTerm = 1;
    for (int i = 0; i < length; i++) {
        if (firstTerm > INT_MAX / limits[i]) {
            return maxCount;
        }

        firstTerm *= limits[i];
    }

    int result = firstTerm;
    for (int i = length - 1; i > 0; i--) {
        int term = firstTerm;
        for (int j = i - 1; j >= 0; j--) {
            int factor = i - j - limits[i]; // non-positive
            if (
                (factor < 0 && term < INT_MAX / factor)
                    || (factor < -1 && term > INT_MIN / factor)
            ) {
                return maxCount;
            }

            term *= factor; // switch sign
            term /= (1 + i - j) * limits[j];
            result += term;
        }
    }

    if (result > maxCount) {
        return maxCount;
    }

    return result;
}

int popLowestProduct(
    int *heap, int *limits, int length, int count, int capacity
) {
    if (count <= 0) {
        return count;
    }

    // can we increment the first value?
    int canIncrement = length > 0 && heap[0] < limits[0];

    // can we increment another value to match the first one?
    int canExtend = 0;
    int extendIndex = 0;
    if (count < capacity || !canIncrement) { // incrementing takes priority
        while (extendIndex < length && heap[extendIndex] == heap[0]) {
            extendIndex++;
        }

        canExtend = extendIndex < length
            && heap[extendIndex] == heap[0] - 1
            && heap[0] <= limits[extendIndex];
    }

    // doing both means pushing a new element onto the heap. extend first.
    if (canIncrement && canExtend) {
        heap[extendIndex]++; // use heap root as temporary buffer
        int product = 1;
        for (int i = 0; i < length; i++) { product *= heap[i]; }

        int childIndex = count;
        count++;
        int *child = heap + childIndex * length;
        while (childIndex > 0) {
            int parentIndex = (childIndex - 1) / 2;
            int *parent = heap + parentIndex * length;
            int parentProduct = 1;
            for (int i = 0; i < length; i++) {
                parentProduct *= parent[i];
            }

            if (parentProduct <= product) {
                break;
            }

            memcpy(child, parent, length * sizeof(heap[0]));
            childIndex = parentIndex;
            child = parent;
        }

        memcpy(child, heap, length * sizeof(heap[0]));
        heap[extendIndex]--;
    }

    // do the remaining operation. this can be increment, extend, or pop.
    int *newItem = heap;
    if (canIncrement) {
        heap[0]++;
    } else if (canExtend) {
        heap[extendIndex]++;
    } else {
        count--;
        newItem = heap + count * length;
    }

    // use the remaining capacity as a temporary buffer if possible
    if (count < capacity && newItem == heap) {
        newItem = heap + count * length;
        memcpy(newItem, heap, length * sizeof(heap[0]));
    }

    int product = 1;
    for (int i = 0; i < length; i++) { product *= newItem[i]; }

    int parentIndex = 0;
    int *parent = heap;
    int childIndex = parentIndex * 2 + 1;
    int *child = heap + childIndex * length;
    while (childIndex < count) {
        int childProduct = 1;
        for (int i = 0; i < length; i++) { childProduct *= child[i]; }

        if (childIndex + 1 < count) {
            int *child2 = child + length;
            int childProduct2 = 1;
            for (int i = 0; i < length; i++) {
                childProduct2 *= child2[i];
            }

            if (childProduct2 < childProduct) {
                childIndex++;
                child = child2;
                childProduct = childProduct2;
            }
        }

        if (childProduct >= product) {
            break;
        }

        if (count < capacity) {
            memcpy(parent, child, length * sizeof(heap[0]));
        } else {
            for (int i = 0; i < length; i++) {
                int temp = child[i];
                child[i] = parent[i];
                parent[i] = temp;
            }
        }

        parentIndex = childIndex;
        parent = child;
        childIndex = parentIndex * 2 + 1;
        child = heap + childIndex * length;
    }

    if (count < capacity) {
        memcpy(parent, newItem, length * sizeof(heap[0]));
    }

    return count;
}

int nextPermutation(int* values, int length){
    // like this except reversed (think little endian instead of big endian):
    // http://wordaligned.org/articles/next-permutation#whats-happening-here
    // so for example,
    // 1146662312
    // 114666 2312 (tailStart = 6)
    // 112666 4312 (swapIndex = 2)
    // 666211 4312 (reverse head)
    // however, let's say limits[6] = 3. this makes the swap illegal. but now
    // we know the subsequence consisting of the first 7 values cannot be
    // increased. we do the swap anyway and increment tailStart:
    // 1126664 312 (tailStart = 7)
    // assuming limits[7] = 3, swapping the 4 and the 3 is also illegal. in
    // fact, since limits is nonincreasing, the 4 and anything else past the
    // previous swapIndex will never be useful to swap. let's call that stuff
    // the "body":
    // 112 6664 312 (bodyStart = 3)
    // here's our new algorithm:
    // while tailStart < length:
    // 1. move any values greater than limits[tailStart] from the head to the
    //    body
    // 2. if all values in the head are less than or equal to
    //    values[tailStart], swap the values at bodyStart and tailStart, and
    //    increment both indices
    // 3. otherwise, swap values[tailStart] with the first greater value in
    //    the head and stop iteration
    // 112 6664 312
    // 112 3664 612 (swap the values at bodyStart and tailStart)
    // 1123 6646 12 (increment both indices)
    // 112 36646 12 (move values from head to body assuming limits[8] = 2)
    // 111 36646 22 (swap values[tailStart] with the first greater value in
    //               the head)
    // then we combine and sort the head and body:
    // 66643111 22
    // how do we know none of the sorted values is greater than the
    // corresponding limit? well, we know that's true for the very first
    // permutation, 6664322111
    // and if we remove the values in our tail from this permutation, the
    // value at each index can only decrease.
    int tailStart = 1;
    while (tailStart < length && values[tailStart] >= values[tailStart - 1]) {
        tailStart++;
    }

    if (tailStart >= length) {
        return 0; // no more permutations
    }

    if (tailStart < length) {
        int value = values[tailStart];
        int swapIndex = 0;
        while (values[swapIndex] <= value) {
            swapIndex++;
        }

        values[tailStart] = values[swapIndex];
        values[swapIndex] = value;
    }

    int *low = values;
    int *high = values + tailStart - 1;
    while (low < high) {
        int tmp = *low;
        *low = *high;
        *high = tmp;
        low++;
        high--;
    }

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
    int count;
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
            out->factors[out->count - 1].count++;
        } else {
            out->factors[out->count].factor = thisFactor;
            out->factors[out->count].count = 1;
            out->count++;
            lastFactor = thisFactor;
        }
    }
}

typedef struct {
    int *limits;
    int count;
    int product;
    Factorization factorization;
    int *state;
    int stateCapacity;
} JoinIndexGenerator;

void destroyJoinIndexGenerator(JoinIndexGenerator generator) {
    free(generator.factorization.factors);
    free(generator.state);
}

#endif
