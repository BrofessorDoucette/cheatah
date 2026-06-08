import time, math
a, b, n = 0.0, 50.0, 20000000
h = (b - a) / n
s = 0.5 * (math.sin(a) * math.exp(-0.01 * a) + math.sin(b) * math.exp(-0.01 * b))
t0 = time.monotonic()
for i in range(1, n):
    x = a + h * i
    s = s + math.sin(x) * math.exp(-0.01 * x)
t1 = time.monotonic()
print(s * h)
print(t1 - t0)
