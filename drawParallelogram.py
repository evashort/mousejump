import imageio
import numpy as np

class Decider:
    def __init__(self, left, right):
        self.left = np.array(left, dtype=float)
        self.normal = np.array(
            [left[1] - right[1], right[0] - left[0]],
            dtype=float
        )
        self.normal /= np.sqrt(np.sum(np.square(self.normal)))

    def decide(self, p):
        return np.sum((p - self.left) * self.normal)

    def isXEntry(self):
        return self.normal[0] > 0

    def isXExit(self):
        return self.normal[0] < 0

    def getX(self, y):
        return np.sum((self.left - [0, y]) * self.normal) / self.normal[0]

    def intersect(self, other):
        denominator = self.normal[1] * other.normal[0] \
            - self.normal[0] * other.normal[1]
        intermediate = (
            other.normal * np.sum(self.left * self.normal) \
                - self.normal * np.sum(other.left * other.normal)
        ) / denominator
        return np.array([-intermediate[1], intermediate[0]], dtype=float)

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
n1 = getNormal(v1)
n2 = scale(getNormal(v2), -1)
n3 = scale(n1, -1)
n4 = scale(n2, -1)
p1 = add(origin, scale(n1, -0.5))
p2 = add(origin, scale(n2, -0.5))
p3 = add(add(origin, v2), scale(n3, -0.5))
p4 = add(add(origin, v1), scale(n4, -0.5))

#deciders = [Decider(p1, p2), Decider(p2, p3), Decider(p3, p4), Decider(p4, p1)]
im = np.zeros((256, 256))
im[10, 10] = 1
for y in range(256):
    for x in range(256):
        #im[y, x] = 1 - abs(min(decider.decide([x, y]) for decider in deciders))
        point = Point()
        point.x = x
        point.y = y
        im[y, x] = 1 - abs(min(
            dot(add(point, scale(p, -1)), n) - 0.5
            for p, n in [(p1, n1), (p2, n2), (p3, n3), (p4, n4)]
        ))

# print(deciders[3].intersect(deciders[0]))
# print(deciders[0].intersect(deciders[1]))
# print(deciders[1].intersect(deciders[2]))
# print(deciders[2].intersect(deciders[3]))

# for decider in deciders:
#     decider.left -= 0.5 * decider.normal # inflate

# yEntry = min(
#     decider.intersect(deciders[(i+1) % len(deciders)])[1]
#     for i, decider in enumerate(deciders)
# )
# yExit = max(
#     decider.intersect(deciders[(i+1) % len(deciders)])[1]
#     for i, decider in enumerate(deciders)
# )

yEntry = min(
    intersect(p1, n1, p2, n2).y,
    intersect(p2, n2, p3, n3).y,
    intersect(p3, n3, p4, n4).y,
    intersect(p4, n4, p1, n1).y
)
yExit = max(
    intersect(p1, n1, p2, n2).y,
    intersect(p2, n2, p3, n3).y,
    intersect(p3, n3, p4, n4).y,
    intersect(p4, n4, p1, n1).y
)
rowNormal = Point()
rowNormal.x = 0
rowNormal.y = 1
rowPoint = Point()
rowPoint.x = 0
for y in range(int(yEntry), int(yExit + 2)):
    # xEntry = max(
    #     decider.getX(y) for decider in deciders if decider.isXEntry()
    # )
    # xExit = min(
    #     decider.getX(y) for decider in deciders if decider.isXExit()
    # )
    rowPoint.y = y
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
    im[y, :int(xEntry)] = 0.5
    im[y, int(xExit + 2):] = 0.5
imageio.imwrite('parallelogram.png', np.clip(im[::-1], 0, 1))
