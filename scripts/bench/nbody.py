import time, math
N, steps = 256, 200
px = [math.sin((1.0+i)*1.3) for i in range(N)]
py = [math.cos((1.0+i)*0.7) for i in range(N)]
vx = [0.0]*N
vy = [0.0]*N
dt, eps = 0.001, 0.01
t0 = time.monotonic()
for s in range(steps):
    for i in range(N):
        ax = 0.0; ay = 0.0
        pxi = px[i]; pyi = py[i]
        for j in range(N):
            dx = px[j] - pxi
            dy = py[j] - pyi
            r2 = dx*dx + dy*dy + eps
            inv = 1.0 / (r2 * math.sqrt(r2))
            ax += dx * inv
            ay += dy * inv
        vx[i] += dt * ax
        vy[i] += dt * ay
    for i in range(N):
        px[i] += dt * vx[i]
        py[i] += dt * vy[i]
chk = 0.0
for i in range(N):
    chk += px[i] + py[i]
t1 = time.monotonic()
print(chk)
print(t1 - t0)
