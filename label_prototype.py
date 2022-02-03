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

labels = getLabels(
    weights=[{'digits': 2, 'letters': 1}],
    joins={},
    unions={'alphanumeric': ['digits', 'letters']},
    lists={'digits': '0123456789', 'letters': 'abcdefghijklmnopqrstuvwxyz'},
    labels='alphanumeric',
    removeLabels=['5'],
)
print(labels)
