from contextlib import contextmanager
import csv
import io
import math
from pathlib import Path
import requests
import shutil
from zipfile import ZipFile

def getSequenceCosts(
    weights={'part': 1, 'start': 0.6, 'end': 0.3, 'full': 1, 'short': 2},
    path=Path("sequenceCosts.csv")
):
    canUseFile = False
    if path.exists():
        with path.open(encoding='utf-8') as f:
            reader = csv.reader(f)
            weightHeader = next(reader)
            weightRow = next(reader)
            loadedWeights = dict(zip(weightHeader, map(float, weightRow)))
            costHeader = next(reader)
            canUseFile = loadedWeights == weights \
                and costHeader == ['sequence', 'cost']

    if not canUseFile:
        weightItems = list(weights.items())
        weightNames, weightVector = zip(
            *(
                (name, weight) for name, weight in weightItems
                if name != 'short'
            )
        )
        sequenceCountVectors = getSequenceCountVectors(weightNames)
        totalCountVector = [0 for _ in weightVector]
        for countVector in sequenceCountVectors.values():
            for i, count in enumerate(countVector):
                totalCountVector[i] += count

        normWeightVector = [
            weight / totalCount for weight, totalCount
            in zip(weightVector, totalCountVector)
        ]
        charCost = math.log(weights['short'])
        sequenceCosts = sequenceCountVectors
        for sequence, countVector in sequenceCountVectors.items():
            sequenceCosts[sequence] = -math.log(
                sum(
                    weight * count for weight, count
                    in zip(normWeightVector, countVector)
                )
            ) + charCost * len(sequence)

        with path.open('w', encoding='utf-8', newline='') as f:
            writer = csv.writer(f, lineterminator='\n')
            writer.writerows(zip(*weightItems))
            writer.writerow(['sequence', 'cost'])
            writer.writerows(
                sorted(sequenceCosts.items(), key=lambda item:item[1])
            )

    with path.open(encoding='utf-8') as f:
        reader = csv.reader(f)
        weightHeader = next(reader)
        weightRow = next(reader)
        assert dict(zip(weightHeader, map(float, weightRow))) == weights
        costHeader = next(reader)
        assert costHeader == ['sequence', 'cost']
        for sequence, cost in reader:
            yield sequence, float(cost)

def getSequenceCountVectors(
    countNames,
    alphabet=set('abcdefghijklmnopqrstuvwxyz'),
    lengths=[1, 2, 3]
):
    PART = countNames.index('part')
    START = countNames.index('start')
    END = countNames.index('end')
    FULL = countNames.index('full')
    sequenceCountVectors = {}
    for word, count in getWordCounts():
        for length in lengths:
            for i in range(len(word) - length + 1):
                part = word[i:i+length]
                if alphabet.issuperset(part):
                    countVector = sequenceCountVectors.setdefault(
                        part,
                        [0 for _ in countNames]
                    )
                    countVector[PART] += count
                    if i == 0:
                        countVector[START] += count

                    if i + length == len(word):
                        countVector[END] += count

                    if length == len(word):
                        countVector[FULL] += count

    return sequenceCountVectors

def getWordCounts():
    archivePath, innerPath = getWordCountPaths()
    with ZipFile(archivePath) as archive:
        with archive.open(str(innerPath)) as binaryFile:
            textFile = io.TextIOWrapper(binaryFile, 'utf-8', errors='replace')
            reader = csv.reader(textFile, delimiter='\t')
            for row in reader:
                if len(row) != 1: # skip "Total words" line
                    word, count, _ = row
                    yield word, int(count)

def getWordCountPaths(
    url='http://www.anc.org/SecondRelease/data/ANC-token-count.zip',
    archivePath=Path('ANC-token-count.zip'),
    innerPath=Path('ANC-token-count.txt')
):
    if not archivePath.exists():
        # https://stackoverflow.com/a/39217788
        with requests.get(url, stream=True) as response:
            response.raise_for_status()
            with archivePath.open('wb') as f:
                shutil.copyfileobj(response.raw, f)

    return archivePath, innerPath

if __name__ == '__main__':
    import itertools
    print(*itertools.islice(getSequenceCosts(), 200), sep='\n')
