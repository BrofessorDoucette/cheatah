<!-- cheatah-bench-stamp v1
     suite:        linalg-vs-numpy
     generated:    2026-08-20
     commit:       b97c491 (dirty)
     host:         12th Gen Intel(R) Core(TM) i7-12700H (governor=powersave), 20 CPUs
     cpu-scaling:  enabled
     build:        purrc -> -O3 -march=native
     competitors:  NumPy 1.26.4 on libblas.so.3 -> libblas.so.3.12.0, threads=unset (BLAS default), CPython 3.12.3
     harness:      rounds=7, striated (cheatah and NumPy adjacent in each round)
     statistic:    median of per-round PAIRED ratios; [lo-hi] is the range of those
     watch:        stdlib/linalg/, stdlib/ndarray/, scripts/numpy_compare.py
     publishable:  true

     PRODUCED BY:
       python3 scripts/numpy_compare.py --suite linalg --md docs/bench/linalg-vs-numpy.md
-->

| op | operand dimensions | cheatah | NumPy | winner | band |
|----|--------------------|--------:|------:|--------|------|
| `matmul` | 4 | 0.10 | 0.79 | **cheatah 8.1x** | 7.46-8.74 |
| `matmul` | 16 | 0.55 | 2.32 | **cheatah 4.2x** | 3.34-4.31 |
| `matmul` | 32 | 3.51 | 13.96 | **cheatah 3.9x** | 3.73-4.54 |
| `matmul` | 64 | 27.67 | 122.17 | **cheatah 4.5x** | 4.25-4.73 |
| `matmul` | 96 | 92.26 | 354.48 | **cheatah 3.9x** | 3.50-3.98 |
| `solve` | 4 | 0.22 | 2.46 | **cheatah 11.1x** | 9.82-11.66 |
| `solve` | 16 | 1.06 | 4.36 | **cheatah 4.1x** | 4.05-4.28 |
| `solve` | 32 | 3.83 | 10.69 | **cheatah 2.8x** | 2.45-2.87 |
| `solve` | 64 | 18.84 | 50.92 | **cheatah 2.7x** | 2.54-2.95 |
| `det` | 4 | 0.09 | 1.95 | **cheatah 22.2x** | 14.70-27.73 |
| `det` | 16 | 0.76 | 3.78 | **cheatah 5.0x** | 4.70-5.31 |
| `det` | 32 | 2.82 | 9.49 | **cheatah 3.4x** | 3.10-3.59 |
| `det` | 64 | 14.65 | 48.67 | **cheatah 3.3x** | 3.09-3.45 |
| `inv` | 4 | 0.25 | 2.14 | **cheatah 8.6x** | 8.38-8.97 |
| `inv` | 16 | 1.56 | 6.34 | **cheatah 4.0x** | 3.39-4.23 |
| `inv` | 32 | 7.28 | 25.43 | **cheatah 3.4x** | 3.28-3.69 |
| `inv` | 64 | 46.24 | 150.65 | **cheatah 3.2x** | 2.71-3.61 |
| `eigvalsh` | 2 | 0.21 | 2.09 | **cheatah 10.0x** | 9.45-10.28 |
| `eigvalsh` | 3 | 0.49 | 2.47 | **cheatah 5.0x** | 4.98-5.21 |
| `eigvalsh` | 4 | 0.64 | 2.76 | **cheatah 4.3x** | 4.14-4.58 |
| `eigvalsh` | 6 | 1.44 | 3.52 | **cheatah 2.4x** | 2.40-2.68 |
| `eigvalsh` | 8 | 1.96 | 4.29 | **cheatah 2.2x** | 2.02-2.33 |
| `eigvalsh` | 16 | 6.91 | 9.40 | **cheatah 1.4x** | 1.30-1.44 |
| `eigvalsh` | 32 | 27.14 | 27.12 | **cheatah 1.0x** | 1.00-1.06 |
| `eigvalsh` | 64 | 109.37 | 155.45 | **cheatah 1.4x** | 1.28-1.46 |
| `dot` | 64 | 0.02 | 0.68 | **cheatah 36.2x** | 29.77-47.38 |
| `dot` | 1024 | 0.08 | 1.16 | **cheatah 13.7x** | 8.24-22.11 |
| `dot` | 16384 | 2.69 | 8.57 | **cheatah 3.2x** | 2.34-3.76 |
| `ndarray.sqrt` | 64 | 0.19 | 0.93 | **cheatah 4.9x** | 4.51-5.32 |
| `ndarray.sqrt` | 1024 | 0.96 | 1.80 | **cheatah 1.9x** | 1.07-2.13 |
| `ndarray.sqrt` | 16384 | 15.15 | 15.70 | **cheatah 1.0x** | 0.86-1.05 |
| `ndarray.exp` | 64 | 0.22 | 1.08 | **cheatah 5.0x** | 4.26-5.43 |
| `ndarray.exp` | 1024 | 0.91 | 4.18 | **cheatah 4.7x** | 3.49-4.85 |
| `ndarray.exp` | 16384 | 13.47 | 52.63 | **cheatah 4.0x** | 3.14-4.11 |
| `ndarray.sin` | 64 | 0.20 | 1.15 | **cheatah 5.7x** | 5.16-5.77 |
| `ndarray.sin` | 1024 | 0.98 | 6.08 | **cheatah 6.2x** | 5.07-6.70 |
| `ndarray.sin` | 16384 | 15.30 | 94.64 | **cheatah 6.3x** | 5.08-6.97 |
| `ndarray.add` | 64 | 0.14 | 0.67 | **cheatah 4.8x** | 4.08-5.63 |
| `ndarray.add` | 16384 | 3.35 | 3.35 | NumPy 1.0x | 0.87-1.37 |
| `cholesky` | 8 | 0.31 | 2.49 | **cheatah 7.7x** | 6.12-8.53 |
| `cholesky` | 32 | 3.74 | 7.87 | **cheatah 2.1x** | 1.67-2.45 |
| `cholesky` | 64 | 17.67 | 27.69 | **cheatah 1.6x** | 1.23-1.70 |
| `qr` | 8 | 1.17 | 9.32 | **cheatah 7.8x** | 6.46-9.01 |
| `qr` | 32 | 15.61 | 29.94 | **cheatah 1.9x** | 1.80-2.06 |
| `qr` | 64 | 106.81 | 142.98 | **cheatah 1.4x** | 1.03-1.41 |
| `svdvals` | 8 | 3.53 | 6.29 | **cheatah 1.8x** | 1.61-2.05 |
| `svdvals` | 32 | 56.29 | 46.37 | NumPy 1.2x | 0.80-0.87 |
| `svdvals` | 64 | 249.77 | 221.91 | NumPy 1.1x | 0.83-0.95 |
| `svd (full)` | 8 | 4.27 | 10.94 | **cheatah 2.6x** | 2.43-2.74 |
| `svd (full)` | 32 | 78.73 | 125.31 | **cheatah 1.6x** | 1.40-1.74 |
| `svd (full)` | 64 | 473.88 | 758.85 | **cheatah 1.6x** | 1.52-1.75 |
| `pinv` | 8 | 4.49 | 18.46 | **cheatah 4.1x** | 3.79-4.42 |
| `pinv` | 32 | 103.86 | 142.81 | **cheatah 1.4x** | 1.27-1.47 |
| `pinv` | 64 | 702.40 | 885.11 | **cheatah 1.2x** | 1.21-1.37 |
| `cond` | 8 | 3.49 | 10.73 | **cheatah 3.1x** | 2.85-3.49 |
| `cond` | 32 | 55.36 | 51.70 | NumPy 1.1x | 0.87-0.96 |
| `cond` | 64 | 243.34 | 214.80 | NumPy 1.1x | 0.87-0.92 |
| `matrix_rank` | 8 | 3.29 | 12.76 | **cheatah 3.9x** | 3.30-4.07 |
| `matrix_rank` | 32 | 52.45 | 56.17 | **cheatah 1.1x** | 0.99-1.13 |
| `matrix_rank` | 64 | 253.39 | 219.57 | NumPy 1.1x | 0.83-0.95 |
| `slogdet` | 8 | 0.26 | 3.38 | **cheatah 14.1x** | 11.21-17.44 |
| `slogdet` | 32 | 3.17 | 11.43 | **cheatah 3.4x** | 3.27-3.69 |
| `slogdet` | 64 | 16.68 | 54.10 | **cheatah 3.4x** | 2.44-3.86 |
| `eigh` | 8 | 2.73 | 7.13 | **cheatah 2.6x** | 2.40-2.66 |
| `eigh` | 32 | 38.25 | 64.50 | **cheatah 1.7x** | 1.56-1.76 |
| `eigh` | 64 | 244.38 | 393.13 | **cheatah 1.6x** | 1.57-1.72 |
| `eigvals` | 8 | 7.21 | 13.68 | **cheatah 1.9x** | 1.28-1.94 |
| `matrix_power` | 8 | 0.98 | 2.56 | **cheatah 2.8x** | 2.32-3.10 |
| `matrix_power` | 32 | 12.89 | 27.45 | **cheatah 2.2x** | 2.08-2.31 |
| `matrix_power` | 64 | 104.97 | 218.60 | **cheatah 2.1x** | 1.73-2.30 |
| `trace` | 32 | 0.01 | 1.18 | **cheatah 159.0x** | 116.81-170.01 |
| `trace` | 256 | 0.06 | 1.19 | **cheatah 20.0x** | 18.23-21.95 |
| `norm(matrix)` | 32 | 0.07 | 1.53 | **cheatah 22.4x** | 13.63-26.47 |
| `norm(matrix)` | 256 | 7.80 | 30.01 | **cheatah 3.8x** | 3.58-4.12 |
| `outer` | 64 | 0.30 | 4.57 | **cheatah 14.8x** | 13.68-15.45 |
| `outer` | 256 | 9.07 | 46.83 | **cheatah 5.2x** | 4.36-5.40 |
| `kron` | 8 | 1.71 | 13.79 | **cheatah 8.2x** | 7.80-8.70 |
| `kron` | 16 | 16.76 | 68.04 | **cheatah 4.0x** | 3.19-4.84 |
| `kron` | 32 | 342.30 | 923.90 | **cheatah 2.7x** | 2.58-2.89 |
