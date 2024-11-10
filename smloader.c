#include "loader.h"
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <string.h>

#define PAGE_SIZE 4096

Elf32_Ehdr *ehdr;
Elf32_Phdr *phdr;
int fd, entrypoint = -1;
int page_faults = 0, page_allocations = 0;
size_t total_fragmentation = 0;

void *page_allocated;
void *address[50];
int address_index = 0;
void *allocated_pages[50];
int allocated_page_count = 0;

void sigsegv_handler(int signum, siginfo_t *info, void *context) {
    page_faults++;
    void *fault_addr = info->si_addr;

    // Locate the segment causing the page fault
    int segment_index = -1;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Addr seg_start = phdr[i].p_vaddr;
        Elf32_Addr seg_end = seg_start + phdr[i].p_memsz;
        if ((uintptr_t)fault_addr >= seg_start && (uintptr_t)fault_addr < seg_end) {
            segment_index = i;
            break;
        }
    }

    if (segment_index == -1) {
        fprintf(stderr, "Segmentation fault at invalid address: %p\n", fault_addr);
        exit(1);
    }

    // Get the segment's base virtual address and its size
    Elf32_Addr seg_base = phdr[segment_index].p_vaddr;
    size_t seg_size = phdr[segment_index].p_memsz;
    size_t seg_offset = phdr[segment_index].p_offset;

    // Calculate the page-aligned base address for the faulted address
    uintptr_t fault_addr_u = (uintptr_t)fault_addr;
    uintptr_t page_base = (fault_addr_u / PAGE_SIZE) * PAGE_SIZE;

    // Calculate the file offset for the specific page being faulted
    size_t page_offset = page_base - seg_base;

    // Check if the page is within the segment's memory size
    if (page_base >= seg_base + seg_size) {
        fprintf(stderr, "Fault address outside of segment's allocated memory.\n");
        exit(1);
    }

    // Handle internal fragmentation by calculating overshoot
    size_t overshoot = (page_base + PAGE_SIZE) - (seg_base + seg_size);
    if (overshoot > 0) {
        total_fragmentation += overshoot;
    }

    // Allocate the page using mmap (one page at a time)
    void *mapped_page = mmap((void *)page_base, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                             MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
    
    if (mapped_page == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    page_allocations++;
    allocated_pages[allocated_page_count++] = mapped_page;

    // Read the appropriate page of the file into the allocated page
    if (lseek(fd, seg_offset + page_offset, SEEK_SET) == -1) {
        perror("lseek");
        exit(1);
    }

    size_t bytes_to_read = (page_base + PAGE_SIZE <= seg_base + seg_size) ? PAGE_SIZE : (seg_base + seg_size - page_base);
    if (read(fd, mapped_page, bytes_to_read) == -1) {
        perror("read");
        exit(1);
    }
}

void mmap_cleanup() {
    // unmapping the mapped pages
    for (int i = 0; i < allocated_page_count; i++) {
        munmap(allocated_pages[i], PAGE_SIZE);
    }
}

void load_and_run_elf(char **exe) {
    fd = open(exe[0], O_RDONLY);
    if (fd == -1) {
        perror("Couldn't open file");
        exit(1);
    }

    ehdr = (Elf32_Ehdr *)malloc(sizeof(Elf32_Ehdr));
    read(fd, ehdr, sizeof(Elf32_Ehdr));

    lseek(fd, ehdr->e_phoff, SEEK_SET);

    phdr = (Elf32_Phdr *)malloc(sizeof(Elf32_Phdr) * ehdr->e_phnum);
    read(fd, phdr, sizeof(Elf32_Phdr) * ehdr->e_phnum);

    // Find entry point
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_vaddr <= ehdr->e_entry && ehdr->e_entry <= phdr[i].p_vaddr + phdr[i].p_memsz) {
            entrypoint = i;
        }
    }

    // Typecast entry point to function pointer and call it
    int (*_start)() = (int (*)())(ehdr->e_entry);
    int result = _start();
    printf("User _start return value = %d\n", result);
    printf("Total page faults: %d\n", page_faults);
    printf("Pages Allocated: %d\n", page_allocations);
    printf("Total fragmentation (in KB): %0.4fKB\n", total_fragmentation / 1024.0);
}

void loader_cleanup() {
    free(ehdr);
    free(phdr);
    mmap_cleanup();
}

int main(int argc, char **argv) {
    struct sigaction sa;
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // 1. Carry out necessary checks on the input ELF file
    Elf32_Ehdr elf32Ehdr;
    FILE* elf_file = fopen(argv[1], "rb");
    if (elf_file != NULL) {
        fread(&elf32Ehdr, sizeof(elf32Ehdr), 1, elf_file);
        if (memcmp(elf32Ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
            printf("This is an invalid elf file\n");
            exit(1);
        }
        fclose(elf_file);
    } else {
        perror("Error:");
    }

    // 2. Passing the ELF file to the loader for loading/execution
    load_and_run_elf(&argv[1]);
    
    loader_cleanup();
    return 0;
}
