#include <stdio.h>

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

int main() {
    int limits[] = {4, 6, 7};
    int combinationCount = countSortedCombinations(limits, 3);
    printf("combination count: %d", combinationCount);
    return 0;
}
