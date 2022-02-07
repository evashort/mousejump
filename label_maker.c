#include <limits.h>
#include <stdio.h>
#include <string.h>

int countSortedCombinations(int *limits, int length, int maxCount) {
    // returns the number of possible nonincreasing lists of positive integers
    // with the given length such that each element is not higher than the
    // corresponding limit. limits must be nonincreasing and have the given
    // length.
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

int main() {
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
        printf("%d (%d): %d %d %d\n", i, count, heap[0], heap[1], heap[2]);
        count = popLowestProduct(heap, limits2, length, count, capacity);
        i++;
    }

    free(heap);
    return 0;
}
