#!/usr/bin/env python3

import heapq

def getLabels(weights, joins, unions, lists, labels, removeLabels):
    sequences, sparsities = zip(
        *(
            (lists[sequence], 1 / weights[0][sequence])
            for sequence in unions[labels]
        )
    )
    return list(unionHelp(sequences, sparsities))

def unionHelp(sequences, sparsities):
    sequences = list(map(iter, sequences))
    next_indices = [1 for _ in sequences]
    heap = [(sparsity, i) for i, sparsity in enumerate(sparsities)]
    heapq.heapify(heap)
    while heap:
        _, i = heap[0]
        try:
            yield next(sequences[i])
        except StopIteration:
            heapq.heappop(heap)
        else:
            next_indices[i] += 1
            heapq.heapreplace(heap, (sparsities[i] * next_indices[i], i))

def getLowProductCombinations(shape, dimensions=None):
    '''
    yields (int, sorted tuple of positive ints) pairs where the first item is
    the product of all values in the second item. all possible tuples are
    generated in order of increasing product.
    shape: specifies the maximum value (inclusive) for each tuple position.
    must be sorted.
    dimensions: internal, used for recursion
    '''
    if dimensions is None:
        dimensions = len(shape)

    if dimensions <= 0:
        if not shape or shape[0] > 0:
            yield 1, (1,) * len(shape)
    else:
        index = dimensions - 1
        copies = len(shape) - index
        limit = shape[index]
        heap = []
        combinations = getLowProductCombinations(shape, index)
        try:
            peekProduct, peek = next(combinations)
        except StopIteration:
            peekProduct = peek = peekValue = None
        else:
            peekValue = peek[index]
            peekProduct *= peekValue

        while peek or heap:
            if not heap or (peek and peekProduct < heap[0][0]):
                yield peekProduct, peek
                if peekValue < limit:
                    heapq.heappush(
                        heap,
                        (
                            peekProduct + peekProduct // peekValue,
                            peek[:index] + (peekValue + 1,) * copies,
                        )
                    )

                try:
                    peekProduct, peek = next(combinations)
                except StopIteration:
                    peekProduct = peek = peekValue = None
                else:
                    peekValue = peek[index]
                    peekProduct *= peekValue
            else:
                yield heap[0]
                product, combination = heap[0]
                value = combination[index]
                if value < limit:
                    heapq.heapreplace(
                        heap,
                        (
                            product + product // value,
                            combination[:index] + (value + 1,) * copies,
                        )
                    )
                else:
                    heapq.heappop(heap)

labels = getLabels(
    weights=[{'digits': 2, 'letters': 1}],
    joins={},
    unions={'alphanumeric': ['digits', 'letters']},
    lists={'digits': '0123456789', 'letters': 'abcdefghijklmnopqrstuvwxyz'},
    labels='alphanumeric',
    removeLabels=['5'],
)
print(labels)

for i, (product, combination) in enumerate(
    getLowProductCombinations((2, 3, 4))
):
    print(i, product, combination)
