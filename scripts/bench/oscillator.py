import time
def fx(x, v): return v
def fv(x, v): return -x
x = 1.0
v = 0.0
dt = 0.0005
n = 4000000
t0 = time.monotonic()
for i in range(n):
    k1x = fx(x, v); k1v = fv(x, v)
    k2x = fx(x + 0.5*dt*k1x, v + 0.5*dt*k1v)
    k2v = fv(x + 0.5*dt*k1x, v + 0.5*dt*k1v)
    k3x = fx(x + 0.5*dt*k2x, v + 0.5*dt*k2v)
    k3v = fv(x + 0.5*dt*k2x, v + 0.5*dt*k2v)
    k4x = fx(x + dt*k3x, v + dt*k3v)
    k4v = fv(x + dt*k3x, v + dt*k3v)
    x = x + dt * (k1x + 2.0*k2x + 2.0*k3x + k4x) / 6.0
    v = v + dt * (k1v + 2.0*k2v + 2.0*k3v + k4v) / 6.0
t1 = time.monotonic()
print(0.5 * (x * x + v * v))
print(t1 - t0)
