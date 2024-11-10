#define _GNU_SOURCE
#define XOPEN_SOURCE 700
#include "loader.h"
#include <signal.h>
#include <errno.h>

#define PAGE_SIZE 4096

Elf32_Ehdr *ehdr = NULL; // ELF header
Elf32_Phdr *phdr = NULL; // Program header
int fd = -1; // File descriptor for the ELF file

int page_faults = 0, page_allocations = 0; // Counters for page faults and allocations
size_t total_fragmentation = 0; // Total memory fragmentation

void *page_allocated; // Pointer to the allocated page
void *address[50]; // Array to store allocated addresses
int address_index = 0; // Index for the address array

void *map_segment_page(int seg_index, uintptr_t fault_addr, int page_index) {
    Elf32_Addr segment_base = phdr[seg_index].p_vaddr; // Base address of the segment
    void *mapped_address = (void *)(segment_base + page_index * PAGE_SIZE); // Calculate the mapped address

    size_t overshoot = (uintptr_t)mapped_address + PAGE_SIZE - (phdr[seg_index].p_vaddr + phdr[seg_index].p_memsz);
    total_fragmentation += (overshoot > 0) ? overshoot : 0; // Calculate fragmentation

    // Map the page into memory
    page_allocated = mmap(mapped_address, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, 
                          MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, fd, phdr[seg_index].p_offset + page_index * PAGE_SIZE);
    
    if (page_allocated == MAP_FAILED) {
        perror("mmap failed");
        exit(1);
    }

    address[address_index++] = page_allocated; // Store the allocated address
    return mapped_address;
}

int find_segment_for_address(void *fault_addr) {
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Addr seg_start = phdr[i].p_vaddr; // Start address of the segment
        Elf32_Addr seg_end = seg_start + phdr[i].p_memsz; // End address of the segment

        if ((uintptr_t)fault_addr >= seg_start && (uintptr_t)fault_addr < seg_end) {
            return i; // Return the segment index if the fault address is within the segment
        }
    }
    return -1; // Return -1 if no segment is found
}

void sigsegv_handler(int signum, siginfo_t *info, void *context) {
    page_faults++; // Increment page fault counter
    page_allocations++; // Increment page allocation counter

    void *fault_addr = info->si_addr; // Get the fault address
    printf("Page fault at address: %p\n", fault_addr);

    // Locate which segment the fault address belongs to
    int seg_index = find_segment_for_address(fault_addr);
    if (seg_index == -1) {
        fprintf(stderr, "Error: Attempted to access invalid memory at address: %p\n", fault_addr);
        exit(1);
    }

    // Determine the page index and allocate it
    int page_index = ((uintptr_t)fault_addr - phdr[seg_index].p_vaddr) / PAGE_SIZE;
    map_segment_page(seg_index, (uintptr_t)fault_addr, page_index);

    // Read the content of the page from the ELF file
    if (lseek(fd, phdr[seg_index].p_offset + page_index * PAGE_SIZE, SEEK_SET) == -1) {
        perror("lseek failed");
        exit(1);
    }

    ssize_t read_result = read(fd, page_allocated, PAGE_SIZE);
    if (read_result == -1) {
        perror("read failed");
        exit(1);
    }
}

void load_elf_header(char *exe) {
    fd = open(exe, O_RDONLY); // Open the ELF file
    if (fd == -1) {
        perror("Error opening ELF file");
        exit(1);
    }

    ehdr = (Elf32_Ehdr *)malloc(sizeof(Elf32_Ehdr)); // Allocate memory for the ELF header
    if (!ehdr) {
        perror("malloc failed for ELF header");
        exit(1);
    }
    if (read(fd, ehdr, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr)) {
        perror("Error reading ELF header");
        exit(1);
    }

    // Validate ELF file magic number
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
        fprintf(stderr, "Invalid ELF file\n");
        exit(1);
    }

    // Read program headers
    lseek(fd, ehdr->e_phoff, SEEK_SET);
    phdr = (Elf32_Phdr *)malloc(sizeof(Elf32_Phdr) * ehdr->e_phnum); // Allocate memory for program headers
    if (!phdr) {
        perror("malloc failed for program headers");
        exit(1);
    }

    if (read(fd, phdr, sizeof(Elf32_Phdr) * ehdr->e_phnum) != sizeof(Elf32_Phdr) * ehdr->e_phnum) {
        perror("Error reading program headers");
        exit(1);
    }
}

int find_entrypoint() {
    int entrypoint_segment = -1;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_vaddr <= ehdr->e_entry && ehdr->e_entry < phdr[i].p_vaddr + phdr[i].p_memsz) {
            entrypoint_segment = i; // Find the segment containing the entry point
            break;
        }
    }
    return entrypoint_segment;
}

void load_and_execute() {
    // Find and execute entry point
    int entrypoint_segment = find_entrypoint();
    if (entrypoint_segment == -1) {
        fprintf(stderr, "Error: Entry point outside program segment\n");
        exit(1);
    }

    int (*_start)() = (int (*)())(ehdr->e_entry); // Cast entry point to function pointer
    int result = _start(); // Call the entry point function

    // Output the required values in the exact format
    printf("User _start return value = %d\n", result);
    printf("Total page faults: %d\n", page_faults);
    printf("Pages Allocated: %d\n", page_allocations);
    printf("Total fragmentation (in KB): %.4fKB\n", total_fragmentation / 1024.0);
}

void loader_cleanup() {
    free(ehdr); // Free the ELF header
    free(phdr); // Free the program headers
    for (int i = 0; i < address_index; i++) {
        if (munmap(address[i], PAGE_SIZE) == -1) {
            perror("munmap failed");
        }
    }
}

void setup_signal_handler(){
    struct sigaction sa;
    sa.sa_sigaction = sigsegv_handler; // Set the signal handler function
    sa.sa_flags = SA_SIGINFO; // Use the sa_sigaction field
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ELF file>\n", argv[0]);
        exit(1);
    }
    setup_signal_handler(); // Setup the signal handler for SIGSEGV
    load_elf_header(argv[1]); // Load the ELF header
    load_and_execute(); // Load and execute the ELF file
    loader_cleanup(); // Cleanup allocated resources

    return 0;
}
