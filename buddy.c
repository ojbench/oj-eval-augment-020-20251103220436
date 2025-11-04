#include "buddy.h"
#define NULL ((void *)0)

#define MAX_RANK 16
#define PAGE_SIZE 4096

// Free list for each rank
typedef struct free_block {
    struct free_block *next;
} free_block_t;

static free_block_t *free_lists[MAX_RANK + 1];
static void *base_addr = NULL;
static int total_pages = 0;

// Helper function to get page index from address
static int get_page_index(void *p) {
    if (p < base_addr) return -1;
    long offset = (char *)p - (char *)base_addr;
    if (offset % PAGE_SIZE != 0) return -1;
    int page_idx = offset / PAGE_SIZE;
    if (page_idx >= total_pages) return -1;
    return page_idx;
}

// Helper function to get address from page index
static void *get_page_addr(int page_idx) {
    return (char *)base_addr + page_idx * PAGE_SIZE;
}

// Helper function to get buddy page index
static int get_buddy_index(int page_idx, int rank) {
    int block_size = 1 << (rank - 1); // 2^(rank-1) pages
    return page_idx ^ block_size;
}

// Helper function to check if a page is aligned for a given rank
static int is_aligned(int page_idx, int rank) {
    int block_size = 1 << (rank - 1);
    return (page_idx % block_size) == 0;
}

// Metadata to track allocated blocks
typedef struct {
    int rank;  // 0 means free/unallocated, >0 means allocated with this rank
} page_meta_t;

static page_meta_t *page_metadata = NULL;

int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;

    // Initialize free lists
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }

    // Allocate metadata array (we'll use a simple array)
    // For simplicity, we'll use the end of the memory pool for metadata
    // But actually, we should allocate it separately or use a static array
    static page_meta_t metadata[128 * 1024 / 4]; // Max pages
    page_metadata = metadata;

    for (int i = 0; i < total_pages; i++) {
        page_metadata[i].rank = 0;
    }

    // Add all memory to the highest possible rank
    int current_page = 0;
    while (current_page < total_pages) {
        // Find the largest rank that fits
        int rank = MAX_RANK;
        int block_size = 1 << (rank - 1);

        while (rank > 0 && (current_page + block_size > total_pages || !is_aligned(current_page, rank))) {
            rank--;
            block_size = 1 << (rank - 1);
        }

        if (rank == 0) break;

        // Add this block to free list
        free_block_t *block = (free_block_t *)get_page_addr(current_page);
        block->next = free_lists[rank];
        free_lists[rank] = block;

        current_page += block_size;
    }

    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }

    // Find a free block of the requested rank or larger
    int current_rank = rank;
    while (current_rank <= MAX_RANK && free_lists[current_rank] == NULL) {
        current_rank++;
    }

    if (current_rank > MAX_RANK) {
        return ERR_PTR(-ENOSPC);
    }

    // Remove block from free list
    free_block_t *block = free_lists[current_rank];
    free_lists[current_rank] = block->next;

    // Split the block if necessary
    while (current_rank > rank) {
        current_rank--;
        int block_size = 1 << (current_rank - 1);

        int page_idx = get_page_index(block);
        int buddy_idx = page_idx + block_size;

        // Add buddy to free list
        free_block_t *buddy = (free_block_t *)get_page_addr(buddy_idx);
        buddy->next = free_lists[current_rank];
        free_lists[current_rank] = buddy;
    }

    // Mark pages as allocated
    int page_idx = get_page_index(block);
    int block_size = 1 << (rank - 1);
    for (int i = 0; i < block_size; i++) {
        page_metadata[page_idx + i].rank = rank;
    }

    return (void *)block;
}

int return_pages(void *p) {
    if (p == NULL) {
        return -EINVAL;
    }

    int page_idx = get_page_index(p);
    if (page_idx < 0) {
        return -EINVAL;
    }

    int rank = page_metadata[page_idx].rank;
    if (rank == 0) {
        return -EINVAL;
    }

    // Mark pages as free
    int block_size = 1 << (rank - 1);
    for (int i = 0; i < block_size; i++) {
        page_metadata[page_idx + i].rank = 0;
    }

    // Try to merge with buddy
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_index(page_idx, rank);

        // Check if buddy is free and has the same rank
        if (buddy_idx < 0 || buddy_idx >= total_pages) break;

        // Check if buddy is in the free list
        free_block_t **prev_ptr = &free_lists[rank];
        free_block_t *curr = free_lists[rank];
        int found = 0;

        while (curr != NULL) {
            int curr_idx = get_page_index(curr);
            if (curr_idx == buddy_idx) {
                // Check if all pages in buddy block are free
                int buddy_block_size = 1 << (rank - 1);
                int all_free = 1;
                for (int i = 0; i < buddy_block_size; i++) {
                    if (page_metadata[buddy_idx + i].rank != 0) {
                        all_free = 0;
                        break;
                    }
                }

                if (all_free) {
                    // Remove buddy from free list
                    *prev_ptr = curr->next;
                    found = 1;
                    break;
                }
            }
            prev_ptr = &curr->next;
            curr = curr->next;
        }

        if (!found) break;

        // Merge with buddy
        if (page_idx > buddy_idx) {
            page_idx = buddy_idx;
        }
        rank++;
    }

    // Add merged block to free list
    free_block_t *block = (free_block_t *)get_page_addr(page_idx);
    block->next = free_lists[rank];
    free_lists[rank] = block;

    return OK;
}

int query_ranks(void *p) {
    int page_idx = get_page_index(p);
    if (page_idx < 0) {
        return -EINVAL;
    }

    // If allocated, return the rank
    if (page_metadata[page_idx].rank > 0) {
        return page_metadata[page_idx].rank;
    }

    // If unallocated, find the maximum rank this page belongs to
    for (int rank = MAX_RANK; rank >= 1; rank--) {
        if (!is_aligned(page_idx, rank)) continue;

        int block_size = 1 << (rank - 1);
        if (page_idx + block_size > total_pages) continue;

        // Check if this block is in the free list
        free_block_t *curr = free_lists[rank];
        while (curr != NULL) {
            if (get_page_index(curr) == page_idx) {
                return rank;
            }
            curr = curr->next;
        }
    }

    return 1; // Default to rank 1
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }

    int count = 0;
    free_block_t *curr = free_lists[rank];
    while (curr != NULL) {
        count++;
        curr = curr->next;
    }

    return count;
}
