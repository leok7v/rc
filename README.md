Copyright (c) 2024, "Leo" Dmitry Kuznetsov

# Fenwick Tree (Binary Indexed Tree) Implementation

[Fenwick Tree](https://en.wikipedia.org/wiki/Fenwick_tree) is 
amazing data structure that allows efficient calculation of
prefix sums in logarithmic times since 1989/1992/1994.

This repository strives to provides simple and clean C 
implementation of a Fenwick Tree. It offers efficient 
operations to update elements and calculate prefix sums 
in logarithmic time. The code is implemented in pure C, 
adhering to modern C standards (C99/C17/C23).

## Features Support

- **Element updates**: element values updates in O(log2(N)) time.
- **Prefix sum queries**: cumulative sum of all elements up to a given index.
- **Index retrieval**: finding the index corresponding to a given prefix sum.
- **Performance**: low-level bitwise operations used to compute 
                   the least significant bit (LSB).
- **Indexing**: Bounds checks are enforced in debug via assertions 
                for safety during updates and queries. Release behavior with
                incorrect indices is undefined (responsibility of the caller).
- **Limitations**: Only handles arrays with sizes that are powers of 2.

## Functions Overview

### `ft_lsb(int32_t i)`
Calculates the least significant bit (LSB) of an integer `i`. 
This is useful for Fenwick Tree traversal.
- **Input**: A non-zero positive integer `i`.
- **Returns**: The LSB of `i`.

### `ft_update(uint64_t tree[], size_t n, int32_t i, uint64_t inc)`
Updates the Fenwick Tree with an increment at a specific index.
- **Input**:
  - `tree[]`: The Fenwick Tree array.
  - `n`: Number of elements in the tree.
  - `i`: Index to update (0-based).
  - `inc`: The increment value.
- **Behavior**: Propagates the increment through the tree.

### `ft_init(uint64_t tree[], size_t n, uint64_t a[])`
Initializes the Fenwick Tree from an array `a[]`.
- **Input**:
  - `tree[]`: The Fenwick Tree array to be initialized.
  - `n`: Number of elements in the tree.
  - `a[]`: The original data array.
- **Behavior**: Sets up the tree based on the input array.

### `ft_query(const uint64_t tree[], size_t n, int32_t i)`
Queries the cumulative sum from the beginning of the 
array to index `i`.
- **Input**:
  - `tree[]`: The Fenwick Tree array.
  - `n`: Number of elements in the tree.
  - `i`: The index for which to calculate the sum (0-based).
- **Returns**: The cumulative sum of elements up to index `i`.

### `ft_index_of(uint64_t tree[], size_t n, uint64_t sum)`
Finds the largest index `i` such that the sum of elements 
up to `i` is less than or equal to a given `sum`.
- **Input**:
  - `tree[]`: The Fenwick Tree array.
  - `n`: Number of elements in the tree.
  - `sum`: The desired cumulative sum.
- **Returns**: The index of the element or `-1` if 
               no such element exists.

## Constraints

- **ft_max_bits**: The tree is designed to handle up to `2^31` 
  elements efficiently using 31 bits for indexing.
- **Assertions**: The implementation uses `assert()` to 
  ensure the correctness of inputs and bounds in debug build.

## Usage Example

```c
#include "ft.h"

int main(void) {
    const size_t n = 8;
    uint64_t a[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint64_t tree[8] = {0};
    ft_init(tree, n, a); // Initialize Fenwick Tree
    ft_update(tree, n, 3, 5); // Update the tree by adding 5 to index 3
    uint64_t sum = ft_query(tree, n, 3); // sum of elements up to index 3
    int32_t index = ft_index_of(tree, n, 15); // index corresponding to sum 15
    return 0;
}
```

## Compilation and Running

To compile the code, use any standard C compiler `cc`:

```bash
cc -o fenwick_tree main.c -std=c99
```

Replace `main.c` with your source file.

## License

This code is licensed under the BSD 3-Clause License. 
See the [LICENSE](LICENSE) or [BSD-3](https://opensource.org/license/bsd-3-clause) 
for more details.

## References

- Wikipedia article on [Fenwick Tree](https://en.wikipedia.org/wiki/Fenwick_tree).
  
## Author

- **"Leo" Dmitry Kuznetsov**  
  - Copyright (c) 2024

# Coding style

See the [STYLE](STYLE.md) for more details.
