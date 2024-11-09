#include "loader.h"
#include <signal.h>
#include <errno.h>

#define PAGE_SIZE 4096

Elf32_Ehdr *ehdr;
Elf32_Phdr *phdr;
int fd;
int page_faults = 0, page_allocations = 0, total_fragmentation = 0;

void sigsegv_handler(int signum, siginfo_t *info, void *context) {
    page_faults++;
    void *fault_addr = info->si_addr;
    uintptr_t fault_page_start = (uintptr_t)fault_addr & ~(PAGE_SIZE - 1);

    // Find the segment that corresponds to the faulting address
    int segment_found = -1;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD &&
            (uintptr_t)fault_addr >= phdr[i].p_vaddr &&
            (uintptr_t)fault_addr < phdr[i].p_vaddr + phdr[i].p_memsz) {
            segment_found = i;
            break;
        }
    }

    if (segment_found == -1) {
        fprintf(stderr, "Segmentation fault at address %p: No valid segment found.\n", fault_addr);
        exit(1);
    }

    uintptr_t segment_vaddr = phdr[segment_found].p_vaddr;
    size_t segment_offset = phdr[segment_found].p_offset;
    size_t offset_in_segment = fault_page_start - segment_vaddr;

    if (offset_in_segment >= phdr[segment_found].p_filesz) {
        // If the fault is in the zero-initialized (bss) region
        offset_in_segment = phdr[segment_found].p_filesz;
    }

    int prot_flags = 0;
    if (phdr[segment_found].p_flags & PF_R) prot_flags |= PROT_READ;
    if (phdr[segment_found].p_flags & PF_W) prot_flags |= PROT_WRITE;
    if (phdr[segment_found].p_flags & PF_X) prot_flags |= PROT_EXEC;

    void *mapped_addr = mmap((void *)fault_page_start, PAGE_SIZE, prot_flags,
                             MAP_PRIVATE | MAP_FIXED, fd, segment_offset + offset_in_segment);

    if (mapped_addr == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    page_allocations++;
    if (phdr[segment_found].p_memsz % PAGE_SIZE != 0) {
        total_fragmentation += PAGE_SIZE - (phdr[segment_found].p_memsz % PAGE_SIZE);
    }
}

void cleanup_pages() {
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uintptr_t start_addr = phdr[i].p_vaddr;
            size_t length = (phdr[i].p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            munmap((void *)start_addr, length);
        }
    }
}

void load_and_run_elf(char **exe) {
    fd = open(exe[0], O_RDONLY);
    if (fd == -1) {
        perror("Couldn't open file");
        exit(1);
    }

    ehdr = (Elf32_Ehdr *)malloc(sizeof(Elf32_Ehdr));
    if (read(fd, ehdr, sizeof(Elf32_Ehdr)) != sizeof(Elf32_Ehdr)) {
        perror("Error reading ELF header");
        exit(1);
    }

    lseek(fd, ehdr->e_phoff, SEEK_SET);

    phdr = (Elf32_Phdr *)malloc(sizeof(Elf32_Phdr) * ehdr->e_phnum);
    if (read(fd, phdr, sizeof(Elf32_Phdr) * ehdr->e_phnum) != sizeof(Elf32_Phdr) * ehdr->e_phnum) {
        perror("Error reading program headers");
        exit(1);
    }

    int (*_start)() = (int (*)())(ehdr->e_entry);
    int result = _start();
    printf("User _start return value = %d\n", result);
    printf("Total page faults: %d\n", page_faults);
    printf("Pages Allocated: %d\n", page_allocations);
    printf("Total fragmentation (in KB): %0.4fKB\n", total_fragmentation / 1024.0);

    cleanup_pages();
    free(ehdr);
    free(phdr);
    close(fd);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ELF executable>\n", argv[0]);
        exit(1);
    }

    struct sigaction sa;
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    // Check if the file is a valid ELF executable
    Elf32_Ehdr elf32Ehdr;
    FILE *elf_file = fopen(argv[1], "rb");
    if (elf_file != NULL) {
        fread(&elf32Ehdr, sizeof(elf32Ehdr), 1, elf_file);
        if (memcmp(elf32Ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
            fprintf(stderr, "This is an invalid ELF file\n");
            fclose(elf_file);
            exit(1);
        }
        fclose(elf_file);
    } else {
        perror("Error opening file");
        exit(1);
    }

    // Pass the executable to the loader for loading and execution
    load_and_run_elf(&argv[1]);

    return 0;
}
