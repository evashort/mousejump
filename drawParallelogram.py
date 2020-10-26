import imageio
import numpy as np

class Point:
    pass

def dot(v1, v2):
    return v1.x * v2.x + v1.y * v2.y

def rotate90DegreesCCW(v):
    rotated = Point()
    rotated.x = -v.y
    rotated.y = v.x
    return rotated

def getNormal(v):
    return scale(
        rotate90DegreesCCW(v),
        1 / np.sqrt(dot(v, v))
    )

def intersect(point1, normal1, point2, normal2):
    denominator = dot(normal1, rotate90DegreesCCW(normal2))
    return rotate90DegreesCCW(
        add(
            scale(normal1, -dot(point2, normal2) / denominator),
            scale(normal2, dot(point1, normal1) / denominator)
        )
    )

def add(v1, v2):
    sum = Point()
    sum.x = v1.x + v2.x
    sum.y = v1.y + v2.y
    return sum

def scale(v, factor):
    scaled = Point()
    scaled.x = factor * v.x
    scaled.y = factor * v.y
    return scaled

origin = Point()
origin.x = 60
origin.y = 60
v1 = Point()
v1.x = 150
v1.y = -50
v2 = Point()
v2.x = -50
v2.y = 150
area = abs(v1.x * v2.y - v1.y * v2.x)
n1 = getNormal(v1)
n2 = scale(getNormal(v2), -1)
n3 = scale(n1, -1)
n4 = scale(n2, -1)
p1 = add(origin, scale(n1, -0.5))
p2 = add(origin, scale(n2, -0.5))
p3 = add(add(origin, v2), scale(n3, -0.5))
p4 = add(add(origin, v1), scale(n4, -0.5))

im = np.zeros((256, 256))
im[10, 10] = 1
for y in range(256):
    for x in range(256):
        point = Point()
        point.x = x
        point.y = y
        im[y, x] = 0.4*(min(
            dot(add(point, scale(p, -1)), n)
            for p, n in [(p1, n1), (p2, n2), (p3, n3), (p4, n4)]
        ) <= 0)

class EdgePixel:
    pass

class Rib:
    pass

spine = Rib()
spine.start = int(min(
    intersect(p1, n1, p2, n2).y,
    intersect(p2, n2, p3, n3).y,
    intersect(p3, n3, p4, n4).y,
    intersect(p4, n4, p1, n1).y
)) + 1
spine.stop = int(max(
    intersect(p1, n1, p2, n2).y,
    intersect(p2, n2, p3, n3).y,
    intersect(p3, n3, p4, n4).y,
    intersect(p4, n4, p1, n1).y
)) + 1
rowNormal = Point()
rowNormal.x = 0
rowNormal.y = 1
rowPoint = Point()
extraPixels = -round(area)
edgePixels = []
ribs = []
for y in range(spine.start, spine.stop):
    rowPoint.y = y
    rowPoint.x = 0
    xEntry = max(
        intersect(p, n, rowPoint, rowNormal).x
        for p, n in [(p1, n1), (p2, n2), (p3, n3), (p4, n4)]
        if n.x > 0
    )
    xExit = min(
        intersect(p, n, rowPoint, rowNormal).x
        for p, n in [(p1, n1), (p2, n2), (p3, n3), (p4, n4)]
        if n.x < 0
    )
    xCenter = int(0.5 * (xEntry + xExit)) + 1
    rib = Rib()
    rib.start = int(xEntry) + 1
    rib.stop = int(xExit) + 1
    ribs.append(rib)

    extraPixels += rib.stop - rib.start
    pixelPoint = Point()
    point.y = y
    for x in range(rib.start, xCenter):
        rowPoint.x = x
        distance = min(
            dot(add(rowPoint, scale(p, -1)), n)
            for p, n in [(p1, n1), (p2, n2), (p3, n3), (p4, n4)]
        )
        if distance >= 1:
            break

        im[y, x] = distance
        edgePixel = EdgePixel()
        edgePixel.y = y
        edgePixel.right = False
        edgePixel.value = distance
        edgePixels.append(edgePixel)

    for x in range(rib.stop - 1, xCenter - 1, -1):
        rowPoint.x = x
        distance = min(
            dot(add(rowPoint, scale(p, -1)), n)
            for p, n in [(p1, n1), (p2, n2), (p3, n3), (p4, n4)]
        )
        if distance >= 1:
            break

        im[y, x] = distance
        edgePixel = EdgePixel()
        edgePixel.y = y
        edgePixel.right = True
        edgePixel.value = distance
        edgePixels.append(edgePixel)

# https://en.wikipedia.org/wiki/Quickselect
def partitionByValue(a, cutoffIndex, lowerCount, stop):
    cutoff = a[cutoffIndex]
    a[cutoffIndex] = a[stop - 1]
    a[stop - 1] = cutoff

    # a[:stop] is divided into 3 regions: lower, higher, and unknown
    # or alternatively into 2 regions: known and unknown
    # initially the "higher" region is empty
    for knownCount in range(lowerCount, stop):
        unknown = a[knownCount]
        if le(unknown, cutoff):
            # leapfrog to shift "higher" region right
            a[knownCount] = a[lowerCount]

            # move unknown into "lower" region
            a[lowerCount] = unknown
            lowerCount += 1

    assert a[lowerCount - 1] == cutoff
    return lowerCount

import random
def partitionByCount(a, desiredLowerCount, start=0, stop=None):
    if stop is None:
        stop = len(a)

    if stop - start == 0:
        return

    cutoffIndex = random.randrange(start, stop)
    lowerCount = partitionByValue(a, cutoffIndex, start, stop)
    if desiredLowerCount < lowerCount:
        partitionByCount(a, desiredLowerCount, start, lowerCount - 1)
    elif desiredLowerCount > lowerCount:
        partitionByCount(a, desiredLowerCount, lowerCount, stop)

def le(a, b):
    return a <= b

for i in range(100):
    a = [random.random() for i in range(20)]
    sortedA = sorted(a)
    k = random.randrange(21)
    partitionByCount(a, k)
    assert sorted(a) == sortedA
    if k > 0 and k < len(a):
        assert max(a[:k]) <= min(a[k:])

def le(a, b): # pylint: disable=function-redefined
    return (a.value, hash((a.right, a.y))) <= (b.value, hash((b.right, b.y)))

partitionByCount(edgePixels, extraPixels)
for i in range(extraPixels):
    extraPixel = edgePixels[i]
    ribIndex = extraPixel.y - spine.start
    if extraPixel.right:
        ribs[ribIndex].stop -= 1
    else:
        ribs[ribIndex].start += 1

for i, rib in enumerate(ribs):
    y = i + spine.start
    im[y, rib.start:rib.stop] = 1

imageio.imwrite('parallelogram.png', np.clip(im[::-1], 0, 1))
