#!/usr/bin/env python3

import heapq

def get_labels(name, definitions):
    definition = definitions[name]
    if isinstance(definition, list):
        return len(definition), iter(definition)
    else:
        operation = definition['operation']
        operands = definition['operands']
        if operation == 'split':
            combined, separator = operands
            labels = combined.split(separator) if separator else combined
            return len(labels), iter(labels)
        elif operation == 'union':
            return union(operands, definitions)

def union(operands, definitions):
    total = 0
    stages = []
    for operand in operands:
        if isinstance(operand, dict):
            sequences = []
            sparsities = []
            zero_weight_sequences = []
            zero_weight_sparsities = []
            for name, weight in operand.items():
                count, labels = get_labels(name, definitions)
                total += count
                if weight > 0:
                    sequences.append(labels)
                    sparsities.append(1 / weight)
                else:
                    zero_weight_sequences.append(labels)
                    zero_weight_sparsities.append(1)

            stages.append(union_help(sequences, sparsities))
            stages.append(
                union_help(zero_weight_sequences, zero_weight_sparsities)
            )
        elif isinstance(operand, list):
            total += len(operand)
            stages.append(iter(operand))
        else:
            count, labels = get_labels(operand, definitions)
            total += count
            stages.append(labels)

    return total, chain(*stages)

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

def chain(*args):
    return (item for arg in args for item in arg)

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

def get_product_heap_size(shape, dimensions=None):
    if dimensions is None:
        dimensions = len(shape)

    if dimensions <= 0:
        return int(not shape or shape[0] > 0)
    else:
        index = dimensions - 1
        result = 0
        for i in range(index - 1, -1, -1):
            result *= -shape[i]
            result += n_choose_k(shape[i], dimensions - i)

        if index % 2 > 0:
            result = -result

        previous = get_product_heap_size(shape, index)
        result += shape[index] * previous
        return result

def n_choose_k(n, k):
    if 2 * k > n:
        k = n - k

    result = 1
    for i in range(k):
        result *= n - i
        result //= i + 1

    return result

assert get_product_heap_size([4, 6, 7]) == sum([
    # top view. imagine these are all aligned with z=7 so only the cell at the
    # origin with height 7 is touching the ground:
    # 2222
    # 3333
    # 4444
    # 555
    # 66
    # 7
    #
    # z * x
    7 * 1,
    6 * 2,
    5 * 3,
    4 * 4,
    3 * 4,
    2 * 4,
])

label_count, labels = get_labels(
    'alphanumeric',
    {
        'digits': {
            'operation': 'split',
            'operands': ['0123456789', ''],
        },
        'letters': {
            'operation': 'split',
            'operands': ['abcdefghijklmnopqrstuvwxyz', ''],
        },
        'alphanumeric': {
            'operation': 'union',
            'operands': [
                {
                    'test': 0,
                    'digits': 2,
                    'letters': 1,
                },
                ['world'],
            ],
        },
        'test': ['hello'],
        'positive_digits': {
            'operation': 'difference',
            'operands': ['digits', ['0']],
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
