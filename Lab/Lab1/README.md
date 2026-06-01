# Boolean Function Minimization using Sum-of-Products
The Boolean Minimizer is a logic synthesis tool designed to solve Boolean function minimization problems via Sum-of-Products (SOP) representation. The system takes a Boolean function defined by its on-set and don't-care set and produces a minimal SOP cover that balances correctness, compactness, and runtime efficiency.

## Problem Formulation
Given a Boolean function over n input variables (0 ≤ n ≤ 24) specified by a set of on-set minterms (f = 1) and don't-care minterms, the goal is to compute a valid SOP cover satisfying: (1) all on-set minterms are covered, (2) no off-set minterm is covered, and (3) the total literal count is strictly less than |on-set| × n, meaning the trivial one-minterm-per-term expansion is explicitly forbidden. Each implicant is represented as a string of length n over {0, 1, −} in MSB-to-LSB order, where "−" denotes a don't-care bit position.

## Features
- **Heuristic SOP Minimizer**: Implements the core Espresso loop (Reduce-Expand-Irredundant) paired with a QMC consensus pre-processing stage to seed optimized initial covers.
- **Stateful Incremental Tracking**: Maintains an active `sub_count` map per ON-minterm, completely avoiding full cover rebuilds and reducing per-iteration overhead to a minimum.
- **Workload Filtering**: Skips re-expansion of static cubes untouched during the Reduce phase, drastically cutting redundant computations in later iterations.
- **Adaptive Execution Budget**: Employs structural fixed-point detection, rigid short-circuiting, and periodic `restore_best` recovery to escape local optima within a strict 3-minute wall-clock limit.

## Processing Pipeline
1. **Parse Input**: Read the bit-width, ON-set minterms, and don't-care minterms from the specification file; build the OFF oracle via a safe hash of ON ∪ DC.
2. **QMC Pre-processing**: Iteratively merge unit cubes via distance-1 consensus until convergence, seeding the cover with larger initial cubes.
3. **Seed Cover**: Initialize the incremental CoverState and patch any ON-minterms left uncovered by QMC with unit cubes.
4. **Initial Expand**: Maximally expand all cubes against the OFF oracle, then prune subsumed cubes via subsume pass.
5. **Main Loop (Reduce -> Irredundant -> Expand)**:
   - _Reduce_: Shrink each cube toward its exclusively covered minterms; mark changed cubes dirty.
   - _Irredundant_: Remove redundant cubes whose minterms are fully covered by others.
   - _Expand_: Re-expand dirty cubes only; deduplicate and subsume after each pass.
6. **Convergence & Early Stop**: Halt on structural fixed point, rigid-structure detection, or stall timeout; periodically restore best cover to escape local optima.
7. **Save Best**: Write the lowest literal-count cover seen across all iterations to the output SOP file.

## Parameters
- **Time Limit**: 175.0s (hard wall-clock budget per case)
- **Early Stop**: 30.0s (stall guard; halt if no literal improvement within this window)
- **QMC Timeout**: 30.0s (stops earlier on convergence)
- **Expand Slice**: `time_limit / 30.0` seconds per expand pass in the main loop
- **MAX_ENUM_DC**: 16 (threshold between forward enumeration and reverse hash scan in CoverState and OFF oracle)

## Input / Output Format

### Input
**Input.txt**
```
<n_bit>
<on_minterm_1> <on_minterm_2> ... <on_minterm_k>
<dc_minterm_1> <dc_minterm_2> ... <dc_minterm_m>
```

**Example**
```
3
1 2 5 7
0 4 6
```

### Output
**Output.sop**
```
<implicant_1>
<implicant_2>
...
<implicant_k>
# string of length n_bit over {0, 1, -} (MSB to LSB; '-' = don't-care)
```

**Example**
```
−01
1−1
010
```

## Environment:
|  Operating System  |    Compiler Version     | C++ Standard |
|--------------------|-------------------------|--------------|
| Rocky Linux 8.10   |        gcc 9.5.0        |     C++17    |
| Ubuntu 22.04       |        gcc 9.5.0        |     C++17    |
| Windows 11         |    gcc 15.1.0 (UCRT64)  |     C++17    |

## Directory Structure
```
Lab1/
  ├── 2025_CAD_HW1.zip
  │   ├── 2025_CAD_PA1.pdf
  │   ├── case1.txt        // Public case1
  │   ├── case2.txt        // Public case2
  │   ├── checker.py       // Checker provided by TA
  │   └── Makefile         // Sample Makefile
  ├── testcase
  │   └── case#.txt        // Official cases
  │
  ├── Makefile             // Build script to compile the project
  ├── include
  │   └── espresso.h
  ├── src
  │   ├── main.cpp         // Main entry point
  │   └── espresso.cpp
  │
  ├── build/               // Object (.o) and dependency (.d) files created during build
  ├── bin/                 // Final executable, e.g., bin/sop
  ├── run.sh               // Shell script to manage testcases
  ├── gencase.cpp          // Synth case generator
  │
  └── README.md
```

## Usage Guide
### How to compile
To generate the executable `bin/sop`, simply run
```
make
make VERBOSE=1 // Verbose logging
```
### How to execute
Run the program with
```
./bin/sop <input>.txt <output>.sop
```
### How to verify
To verify the output with provided verifier
```
python3 checker.py <input>.txt <output>.sop  // Check for correctness
```
### Utility Scripts
To quickly run, clean, or verify testcases, use `run.sh`.
```
./run.sh <case|all> [check|clean|valgrind]
```
