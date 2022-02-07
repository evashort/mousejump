#include <stdio.h>
#include <string.h>

int countSortedCombinations(int *limits, int length) {
    // returns the number of possible nondecreasing lists of positive integers
    // with the given length such that each element is not higher than the
    // corresponding limit. limits must be nondecreasing and have the given
    // length.
    //
    // here's what the formula looks like on paper for length = 5, where a, b,
    // c, d, and e are the 5 elements of limits, and (n k) means n choose k:
    // abcde
    // - (a 2)cde - a(b 2)de - ab(c 2)e - abc(d 2)
    // + (a 3)de  + a(b 3)e  + ab(c 3)
    // - (a 4)e   - a(b 4)
    // + (a 5)
    //
    // in this code, (a 5) would be calculated from the previous term -(a 4)e:
    // (a 5) = -(a 4)e * (4 - a) / 5e
    // this works because
    // (n k) = n! / (k! * (n - k)!)
    //       = n! * (n - k + 1) / (k * (k - 1)! * (n - k + 1)!)
    //       = (n k-1) * (n - k + 1) / k
    //
    // here's a specific example with limits = [4, 6, 7]. this illustration is
    // a top view. imagine each number is a column of that many blocks aligned
    // with z=7 so only the column at the origin with height 7 is touching the
    // ground:
    // 2222
    // 3333
    // 4444
    // 555
    // 66
    // 7
    //
    // 4 * 6 * 7   // entire rectangular prism
    // - (4 2) * 7 // right triangular column (empty space in lower right)
    // - 4 * (6 2) // right triangular prism in the space under the top 6 rows
    // + (4 3)     // tetrahedral intersection of the two right triangular
    //             // prisms. looks like  2
    //             //                    11
    // = 168 - 42 - 60 + 4 = 70

    int firstTerm = 1;
    for (int i = 0; i < length; i++) {
        firstTerm *= limits[i];
    }

    int result = firstTerm;
    for (int i = 0; i < length; i++) {
        int term = firstTerm;
        for (int j = i + 1; j < length; j++) {
            term *= j - i - limits[i]; // switch sign
            term /= (1 + j - i) * limits[j];
            result += term;
        }
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
    int limits[] = {4, 6, 7};
    int combinationCount = countSortedCombinations(limits, 3);
    printf("combination count: %d\n", combinationCount);

    int limits2[] = {4, 3, 2};
    int count = 1;
    int capacity = 5; // 3 * 2 - (2 2)
    int heap[9 * 3] = {1, 1, 1};
    int i = 0;
    while (count > 0) {
        printf("%d (%d): %d %d %d\n", i, count, heap[0], heap[1], heap[2]);
        count = popLowestProduct(heap, limits2, 3, count, 9);
        i++;
    }

    return 0;
}
