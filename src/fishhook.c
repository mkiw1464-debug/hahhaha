// fishhook — Facebook (MIT license)
// Minimal port for FFNET iOS build — ARM64 dyld lazy binding rebind

#include "fishhook.h"
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/reloc.h>

#ifdef __LP64__
typedef struct mach_header_64 mach_header_t;
typedef struct segment_command_64 segment_command_t;
typedef struct section_64 section_t;
typedef struct nlist_64 nlist_t;
#define LC_SEGMENT_ARCH_DEPENDENT LC_SEGMENT_64
#else
typedef struct mach_header mach_header_t;
typedef struct segment_command segment_command_t;
typedef struct section section_t;
typedef struct nlist nlist_t;
#define LC_SEGMENT_ARCH_DEPENDENT LC_SEGMENT
#endif

static int get_protection(void *addr, vm_prot_t *prot, vm_prot_t *max_prot) {
    vm_address_t va = (vm_address_t)addr;
    vm_size_t sz = 0;
    mach_port_t obj;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
    return vm_region_64(mach_task_self(), &va, &sz, VM_REGION_BASIC_INFO_64,
                        (vm_region_info_t)&info, &cnt, &obj);
}

static void perform_rebinding_with_section(struct rebinding bindings[],
                                            size_t nb,
                                            intptr_t slide,
                                            nlist_t *symtab,
                                            char *strtab,
                                            uint32_t *indirect_symtab,
                                            section_t *sect) {
    uint32_t *indirect_sym_indices = indirect_symtab + sect->reserved1;
    void **indirect_sym_bindings = (void **)((uintptr_t)slide + sect->addr);

    for (uint i = 0; i < sect->size / sizeof(void *); i++) {
        uint32_t symtab_index = indirect_sym_indices[i];
        if (symtab_index == INDIRECT_SYMBOL_ABS || symtab_index == INDIRECT_SYMBOL_LOCAL ||
            symtab_index == (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS)) continue;
        uint32_t strtab_offset = symtab[symtab_index].n_un.n_strx;
        char *sym_name = strtab + strtab_offset;
        if (!sym_name || sym_name[0] != '_') continue;
        char *sym = sym_name + 1;

        for (size_t j = 0; j < nb; j++) {
            if (bindings[j].name && strcmp(sym, bindings[j].name) == 0) {
                if (bindings[j].replaced && *bindings[j].replaced == NULL)
                    *bindings[j].replaced = indirect_sym_bindings[i];
                // Make writable
                vm_prot_t cur, max;
                vm_address_t aligned = (vm_address_t)(&indirect_sym_bindings[i]) & ~(PAGE_SIZE-1);
                vm_protect(mach_task_self(), aligned, PAGE_SIZE, false,
                           VM_PROT_READ | VM_PROT_WRITE);
                indirect_sym_bindings[i] = bindings[j].replacement;
                vm_protect(mach_task_self(), aligned, PAGE_SIZE, false,
                           VM_PROT_READ | VM_PROT_EXECUTE);
            }
        }
    }
}

static void rebind_symbols_for_image(const struct mach_header *header, intptr_t slide,
                                      struct rebinding bindings[], size_t nb) {
    Dl_info info;
    if (!dladdr(header, &info)) return;

    segment_command_t *cur_seg_cmd;
    segment_command_t *linkedit = NULL;
    struct symtab_command *symtab_cmd = NULL;
    struct dysymtab_command *dysymtab_cmd = NULL;

    uintptr_t cur = (uintptr_t)header + sizeof(mach_header_t);
    for (uint i = 0; i < header->ncmds; i++, cur += cur_seg_cmd->cmdsize) {
        cur_seg_cmd = (segment_command_t *)cur;
        if (cur_seg_cmd->cmd == LC_SEGMENT_ARCH_DEPENDENT) {
            if (!strcmp(((segment_command_t*)cur)->segname, SEG_LINKEDIT))
                linkedit = (segment_command_t *)cur;
        } else if (cur_seg_cmd->cmd == LC_SYMTAB) {
            symtab_cmd = (struct symtab_command *)cur;
        } else if (cur_seg_cmd->cmd == LC_DYSYMTAB) {
            dysymtab_cmd = (struct dysymtab_command *)cur;
        }
    }
    if (!linkedit || !symtab_cmd || !dysymtab_cmd) return;

    uintptr_t linkedit_base = (uintptr_t)slide + linkedit->vmaddr - linkedit->fileoff;
    nlist_t *symtab = (nlist_t *)(linkedit_base + symtab_cmd->symoff);
    char    *strtab = (char *)   (linkedit_base + symtab_cmd->stroff);
    uint32_t *indirect_symtab = (uint32_t *)(linkedit_base + dysymtab_cmd->indirectsymoff);

    cur = (uintptr_t)header + sizeof(mach_header_t);
    for (uint i = 0; i < header->ncmds; i++, cur += cur_seg_cmd->cmdsize) {
        cur_seg_cmd = (segment_command_t *)cur;
        if (cur_seg_cmd->cmd != LC_SEGMENT_ARCH_DEPENDENT) continue;
        section_t *sect = (section_t *)((char *)cur_seg_cmd + sizeof(segment_command_t));
        for (uint j = 0; j < cur_seg_cmd->nsects; j++, sect++) {
            uint32_t type = sect->flags & SECTION_TYPE;
            if (type == S_LAZY_SYMBOL_POINTERS || type == S_NON_LAZY_SYMBOL_POINTERS)
                perform_rebinding_with_section(bindings, nb, slide, symtab, strtab,
                                               indirect_symtab, sect);
        }
    }
}

int rebind_symbols(struct rebinding bindings[], size_t nb) {
    for (uint32_t i = 0; i < _dyld_image_count(); i++)
        rebind_symbols_for_image(_dyld_get_image_header(i),
                                  _dyld_get_image_vmaddr_slide(i),
                                  bindings, nb);
    return 0;
}

int rebind_symbols_image(void *header, intptr_t slide,
                          struct rebinding bindings[], size_t nb) {
    rebind_symbols_for_image((const struct mach_header *)header, slide, bindings, nb);
    return 0;
}
