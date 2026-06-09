import numpy as np
A = np.array([[4., 1., 0., 1.],
              [1., 5., 1., 0.],
              [0., 1., 6., 1.],
              [1., 0., 1., 7.]])
b = np.array([1., 2., 3., 4.])
print(np.linalg.solve(A, b))
