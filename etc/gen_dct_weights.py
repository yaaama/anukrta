#!/usr/bin/env python

import math

# Dimensions for pHash
N = 32  # Input size
M = 8  # Output frequencies we care about


# Bitshift amount (Q15 format)
# 15 bits is standard. 1 << 15 = 32,768
SHIFT = 15
MULTIPLIER = 1 << SHIFT


def generate_1d_int_array():
    print(f"// Pre-calculated 1D integer DCT weights for {N}x{N} to {M}x{M} pHash")
    print(f"// Format: Q15 fixed-point (shifted left by {SHIFT} bits)")
    print(f"// Total size: {M * N} elements.")
    print("#include <stdint.h>\n")
    print(f"const int32_t dct_weights[{M * N}] = {{")

    scale = math.sqrt(2.0 / N)
    weights = []

    for u in range(M):
        c = 1.0 / math.sqrt(2.0) if u == 0 else 1.0
        for x in range(N):
            val = scale * c * math.cos(((2 * x + 1) * u * math.pi) / (2.0 * N))

            # Multiply by our bitshift multiplier and round to nearest integer
            int_val = int(round(val * MULTIPLIER))
            weights.append(f"{int_val:6d}")

    for u in range(M):
        print(f"\n    // u = {u}")
        for i in range(0, N, 8):
            chunk = weights[u * N + i : u * N + i + 8]
            print("    " + ", ".join(chunk) + ",")

    print("};")


def generate_c_array_float_1d():
    print(f"// Pre-calculated 1D DCT weight array for {N}x{N} to {M}x{M} pHash")
    print(f"// Total size: {M * N} elements.")
    print(f"// Access using index: (u * {N}) + x")
    print(f"const float dct_weights[{M * N}] = {{")

    scale = math.sqrt(2.0 / N)

    weights = []

    # Calculate all weights and store them in a flat list
    for u in range(M):
        c = 1.0 / math.sqrt(2.0) if u == 0 else 1.0
        for x in range(N):
            val = scale * c * math.cos(((2 * x + 1) * u * math.pi) / (2.0 * N))
            weights.append(f"{val:9.6f}f")

    # Print the flat list formatted for C (8 numbers per line for readability)
    for u in range(M):
        print(f"\n    // u = {u}")
        for i in range(0, N, 8):
            # Grab a chunk of 8 values
            chunk = weights[u * N + i : u * N + i + 8]
            print("    " + ", ".join(chunk) + ",")

    print("};")


def generate_c_array_float_2d():
    print(f"// Pre-calculated 1D DCT weight matrix for {N}x{N} to {M}x{M} pHash")
    print(f"const float dct_matrix[{M}][{N}] = {{")

    scale = math.sqrt(2.0 / N)

    for u in range(M):
        print("    {", end="")

        # The normalization constant C(u)
        c = 1.0 / math.sqrt(2.0) if u == 0 else 1.0

        row_vals = []
        for x in range(N):
            # The core DCT cosine math
            val = scale * c * math.cos(((2 * x + 1) * u * math.pi) / (2.0 * N))

            # Format as a C float (e.g., " 0.176777f", "-0.176777f")
            row_vals.append(f"{val:9.6f}f")

        # Join the 32 numbers with commas
        print(", ".join(row_vals), end="")
        print("},")

    print("};")


if __name__ == "__main__":
    generate_c_array_float_2d()
    generate_c_array_float_1d()
    generate_1d_int_array()
