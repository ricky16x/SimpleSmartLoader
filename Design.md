# SimpleSmartLoader Design Document

## Project Overview
The SimpleSmartLoader is an enhancement of the previous SimpleLoader, designed to load and execute 32-bit ELF executables. Unlike the original loader, which allocated all required segments upfront, the SimpleSmartLoader introduces lazy loading by handling page faults to dynamically load only the necessary memory segments during program execution. This smart approach is inspired by Linux's memory management, allowing efficient memory use and execution without preloading segments.

## Repository Link
The complete implementation can be found on GitHub: [SimpleLoader-OS](https://github.com/WhiteR9se/SimpleLoader-OS)

*Note*: The GitHub contribution details are the same as in SimpleLoader. Future assignments will reflect equal contribution with SSH access resolved.

## Team Members && Contribution Summary
- **Karan Singh (2023270)**: Implemented ELF parsing, memory allocation via lazy loading, and page fault handling logic.
- **Rounak Dey (2023449)**: Integrated error handling, segmentation fault interception, and implemented reporting mechanisms for page faults, page allocations, and internal fragmentation statistics.

#### File Descriptions
___
#### Root Directory

- **DESIGN.md**: Design documentation for the project.
- **Makefile**: Build instructions for compiling the project.
- **README.md**: Overview and instructions for using the repository.
- **fib.c**: C program file for generating the Fibonacci sequence, used for testing the loader's functionality.
- **.gitignore**: Specifies files and directories for Git to ignore, keeping the repository clean.
- **loader.h**: Header file for the SimpleSmartLoader, containing declarations and data structures used in the implementation.
- **prime.c**: C program file to calculate prime numbers, used as an additional test case.
- **smloader.c**: The main implementation of the SimpleSmartLoader, responsible for ELF parsing, lazy loading, and page fault handling.
- **sum.c**: C program file to compute the sum of numbers, provided as an additional test case for the loader.

## Design and Implementation Details

### 1. ELF File Parsing
The SimpleSmartLoader initializes by parsing the ELF header to retrieve key information such as the entry point and program header offset. Unlike SimpleLoader, it does not allocate memory for program segments upfront, enabling lazy loading during execution.

### 2. Lazy Loading with Segmentation Fault Handling
   - The loader initiates execution by attempting to call the `_start` function at the entry point address.
   - Accessing unallocated memory segments triggers a segmentation fault, which the loader treats as a page fault, prompting the loading of the relevant segment dynamically.
   - The segmentation fault handler uses `mmap` to allocate memory in page-sized chunks (4KB) for the segment that caused the fault, ensuring efficient and lazy loading as the program accesses new memory regions.

### 3. Page-by-Page Memory Allocation
   - Segments are loaded page-by-page to optimize memory use and align with physical memory page allocation. For example, if a segment like `.text` requires 5KB, the loader allocates 4KB initially and allocates an additional page when needed, reducing upfront memory use.
   - This approach ensures that virtual memory within a segment is contiguous while physical memory may be non-contiguous.

### 4. Execution Resumption and Segmentation Fault Recovery
   - After allocating and loading the required memory, the program resumes execution from where it left off.
   - The segmentation fault handler ensures minimal program disruption by allocating necessary memory without program termination, enabling seamless execution despite initial unavailability of memory pages.

### 5. Reporting and Statistics
   - Upon execution completion, SimpleSmartLoader reports:
     1. Total number of page faults encountered.
     2. Total page allocations performed.
     3. Total internal fragmentation in KB.
   - These metrics help gauge the loader's efficiency and memory management accuracy, providing insights into the lazy loading behavior.

### 6. Compilation and Testing
   - The Makefile from SimpleLoader is reused to compile the SimpleSmartLoader and associated test cases (`fib.c` and `sum.c`).
   - The loader is compiled with flags `-m32`, `-no-pie`, and `-nostdlib` to ensure compatibility with 32-bit ELF binaries.

## Conclusion
The SimpleSmartLoader advances the functionality of SimpleLoader by implementing on-demand, lazy loading of segments. This upgrade significantly optimizes memory usage through dynamic memory allocation triggered by segmentation faults, reducing overhead and internal fragmentation, and improving overall loader efficiency for 32-bit ELF executables.
