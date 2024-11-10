#include "loader.h"
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>

#define PAGE_SIZE 4096

Elf32_Ehdr *ehdr;
Elf32_Phdr *phdr;
int fd;
int page_faults = 0, page_allocations = 0;
size_t total_fragmentation = 0;

void *page_allocated;
void *address[50];
int address_index = 0;

void sigsegv_handler(int signum, siginfo_t *info, void *context) {
    page_faults++;
    page_allocations++;
    void *fault_addr = info->si_addr;

    int fault_seg_index = -1;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf32_Addr seg_start = phdr[i].p_vaddr;
        Elf32_Addr seg_end = seg_start + phdr[i].p_memsz;
        if ((uintptr_t)fault_addr >= seg_start && (uintptr_t)fault_addr < seg_end) {
            fault_seg_index = i;
            break;
        }
    }

    if (fault_seg_index == -1) {
        fprintf(stderr, "Attempted to map outside the valid program segments. Fault address: %p\n", fault_addr);
        exit(1);
    }

    int page_index = ((uintptr_t)fault_addr - phdr[fault_seg_index].p_vaddr) / PAGE_SIZE;
    void *seg_page_addr = (void *)(phdr[fault_seg_index].p_vaddr + page_index * PAGE_SIZE);

    size_t overshoot = (uintptr_t)seg_page_addr + PAGE_SIZE - (phdr[fault_seg_index].p_vaddr + phdr[fault_seg_index].p_memsz);
    if (overshoot > 0) {
        total_fragmentation += overshoot;
    }

    page_allocated = mmap(seg_page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, fd, phdr[fault_seg_index].p_offset + (page_index * PAGE_SIZE));

    if (page_allocated == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    address[address_index++] = page_allocated;

    if (lseek(fd, phdr[fault_seg_index].p_offset + page_index * PAGE_SIZE, SEEK_SET) == -1) {
        perror("lseek");
        exit(1);
    }

    ssize_t reading = (uintptr_t)seg_page_addr + PAGE_SIZE >= phdr[fault_seg_index].p_vaddr + phdr[fault_seg_index].p_memsz
                          ? read(fd, page_allocated, PAGE_SIZE)
                          : read(fd, page_allocated, phdr[fault_seg_index].p_vaddr + phdr[fault_seg_index].p_memsz - (uintptr_t)seg_page_addr);

    if (reading == -1) {
        perror("read");
        exit(1);
    }
}


void mmap_cleanup() {
    for (int i = 0; i < address_index; i++) {
        if (munmap(address[i], PAGE_SIZE) == -1) {
            perror("munmap");
        }
    }
}

void load_and_run_elf(char **exe) {
    fd = open(exe[0], O_RDONLY);
    if (fd == -1) {
        perror("Couldn't open ELF file");
        exit(1);
    }

    ehdr = (Elf32_Ehdr *)malloc(sizeof(Elf32_Ehdr));
    if (ehdr == NULL) {
        perror("malloc");
        exit(1);
    }
    read(fd, ehdr, sizeof(Elf32_Ehdr));

    lseek(fd, ehdr->e_phoff, SEEK_SET);
    phdr = (Elf32_Phdr *)malloc(sizeof(Elf32_Phdr) * ehdr->e_phnum);
    if (phdr == NULL) {
        perror("malloc");
        exit(1);
    }
    read(fd, phdr, sizeof(Elf32_Phdr) * ehdr->e_phnum);

    int entrypoint = -1;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_vaddr <= ehdr->e_entry && ehdr->e_entry < phdr[i].p_vaddr + phdr[i].p_memsz) {
            entrypoint = i;
            break;
        }
    }

    if (entrypoint == -1) {
        fprintf(stderr, "Entry point not found in any segment\n");
        exit(1);
    }

    int (*_start)() = (int (*)())(ehdr->e_entry);
    int result = _start();
    printf("User _start return value = %d\n", result);
    printf("Total page faults: %d\n", page_faults);
    printf("Pages Allocated: %d\n", page_allocations);
    printf("Total fragmentation (in KB): %.4fKB\n", total_fragmentation / 1024.0);
}

void loader_cleanup() {
    free(ehdr);
    free(phdr);
    mmap_cleanup();
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ELF file>\n", argv[0]);
        exit(1);
    }

    struct sigaction sa;
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    Elf32_Ehdr elf32Ehdr;
    FILE *elf_file = fopen(argv[1], "rb");
    if (elf_file != NULL) {
        fread(&elf32Ehdr, sizeof(elf32Ehdr), 1, elf_file);
        if (memcmp(elf32Ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
            fprintf(stderr, "Invalid ELF file\n");
            fclose(elf_file);
            exit(1);
        }
        fclose(elf_file);
    } else {
        perror("Error opening ELF file");
        exit(1);
    }

    load_and_run_elf(&argv[1]);

    loader_cleanup();
    return 0;
}
