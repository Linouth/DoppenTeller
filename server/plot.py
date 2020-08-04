import matplotlib.pyplot as plt
import sys
import os


if len(sys.argv) < 2:
    print(f"Usage: {sys.argv[0]} <file>")
    exit(1)

xs = []
ys = []

t0 = -1
with open(sys.argv[1], 'r') as f:
    for line in f:
        t, d = map(float, line.split(", "))
        if t0 == -1:
            t0 = t
        xs += [t - t0]
        ys += [d]

plt.plot(xs, ys)
plt.show()