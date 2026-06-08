import time
W, H, maxit = 800, 600, 256
total = 0
t0 = time.monotonic()
for py in range(H):
    for px in range(W):
        x0 = -2.5 + 3.5 * px / W
        y0 = -1.0 + 2.0 * py / H
        x = 0.0; y = 0.0; it = 0
        while x * x + y * y <= 4.0 and it < maxit:
            xt = x * x - y * y + x0
            y = 2.0 * x * y + y0
            x = xt
            it += 1
        total += it
t1 = time.monotonic()
print(total)
print(t1 - t0)
