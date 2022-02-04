#!/usr/bin/env python3

import heapq

def get_labels(name, definitions):
    definition = definitions[name]
    operation = definition['operation']
    operands = definition['operands']
    if operation == 'literal':
        return len(operands), iter(operands)
    elif operation == 'union':
        total = 0
        sequences = []
        sparsities = []
        for operand, weight in operands[0].items():
            count, labels = get_labels(operand, definitions)
            total += count
            sequences.append(labels)
            sparsities.append(1 / weight)

        return total, union_help(sequences, sparsities)

def union_help(sequences, sparsities):
    '''
    merge generators.
    sequences: list or tuple of generators.
    sparsities: 1/density, one for each sequence. controls the relative
    frequency of items from each list in the resulting generator.
    '''
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

def get_low_product_combinations(shape, dimensions=None):
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
        combinations = get_low_product_combinations(shape, index)
        try:
            peek_product, peek = next(combinations)
        except StopIteration:
            peek_product = peek = peek_value = None
        else:
            peek_value = peek[index]
            peek_product *= peek_value

        while peek or heap:
            if not heap or (peek and peek_product < heap[0][0]):
                yield peek_product, peek
                if peek_value < limit:
                    heapq.heappush(
                        heap,
                        (
                            peek_product + peek_product // peek_value,
                            peek[:index] + (peek_value + 1,) * copies,
                        )
                    )

                try:
                    peek_product, peek = next(combinations)
                except StopIteration:
                    peek_product = peek = peek_value = None
                else:
                    peek_value = peek[index]
                    peek_product *= peek_value
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

label_count, labels = get_labels(
    'alphanumeric',
    {
        'digits': {
            'operation': 'literal',
            'operands': '0123456789',
        },
        'letters': {
            'operation': 'literal',
            'operands': 'abcdefghijklmnopqrstuvwxyz',
        },
        'alphanumeric': {
            'operation': 'union',
            'operands': [
                {
                    'digits': 2,
                    'letters': 1,
                },
            ],
        },
        'zero': {
            'operands': ['0'],
        },
        'positive_digits': {
            'operation': 'difference',
            'operands': ['digits', 'zero'],
        },
        'two_digit_numbers': {
            'operation': 'join',
            'operands': ['positive_digits', 'digits'],
        },
    },
)
labels = list(labels)
assert label_count == len(labels)
print(labels)

for i, (product, combination) in enumerate(
    get_low_product_combinations((2, 3, 4))
):
    print(i, product, combination)
