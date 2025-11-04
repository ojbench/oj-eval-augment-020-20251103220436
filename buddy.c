#include "buddy.h"
#define NULL ((void *)0)

#define MAX_RANK 16
#define PAGE_SIZE 4096
#define MAX_PAGES (128 * 1024 / 4)

// Free list for each rank - doubly linked
typedef struct free_block {
    struct free_block *next;
    struct free_block *prev;
} free_block_t;

static free_block_t *free_lists[MAX_RANK + 1];
static void *base_addr = NULL;
static int total_pages = 0;

// Metadata to track allocated blocks
typedef struct {
    int rank;  // 0 means free/unallocated, >0 means allocated with this rank
    int is_free; // 1 if this page is the start of a free block, 0 otherwise
} page_meta_t;

static page_meta_t page_metadata[MAX_PAGES];

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

int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;

    // Initialize free lists
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }

    // Initialize metadata
    for (int i = 0; i < total_pages; i++) {
        page_metadata[i].rank = 0;
        page_metadata[i].is_free = 0;
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
        block->prev = NULL;
        if (free_lists[rank] != NULL) {
            free_lists[rank]->prev = block;
        }
        free_lists[rank] = block;

        // Mark as free in metadata
        page_metadata[current_page].is_free = 1;
        page_metadata[current_page].rank = rank;

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
    if (block->next != NULL) {
        block->next->prev = NULL;
    }

    int page_idx = get_page_index(block);
    page_metadata[page_idx].is_free = 0;

    // Split the block if necessary
    while (current_rank > rank) {
        current_rank--;
        int block_size = 1 << (current_rank - 1);

        int buddy_idx = page_idx + block_size;

        // Add buddy to free list
        free_block_t *buddy = (free_block_t *)get_page_addr(buddy_idx);
        buddy->next = free_lists[current_rank];
        buddy->prev = NULL;
        if (free_lists[current_rank] != NULL) {
            free_lists[current_rank]->prev = buddy;
        }
        free_lists[current_rank] = buddy;

        // Mark buddy as free
        page_metadata[buddy_idx].is_free = 1;
        page_metadata[buddy_idx].rank = current_rank;
    }

    // Mark pages as allocated
    page_metadata[page_idx].rank = rank;

    return (void *)block;
}

// Helper to remove a specific block from free list - O(1) with doubly-linked list
static void remove_from_free_list(int page_idx, int rank) {
    free_block_t *block = (free_block_t *)get_page_addr(page_idx);

    if (block->prev != NULL) {
        block->prev->next = block->next;
    } else {
        // This is the head of the list
        free_lists[rank] = block->next;
    }

    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
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
    if (rank == 0 || page_metadata[page_idx].is_free) {
        return -EINVAL;
    }

    // Try to merge with buddy
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_index(page_idx, rank);

        // Check if buddy exists and is free with the same rank
        if (buddy_idx < 0 || buddy_idx >= total_pages) break;
        if (!page_metadata[buddy_idx].is_free || page_metadata[buddy_idx].rank != rank) break;

        // Remove buddy from free list
        remove_from_free_list(buddy_idx, rank);
        page_metadata[buddy_idx].is_free = 0;

        // Merge with buddy
        if (page_idx > buddy_idx) {
            page_idx = buddy_idx;
        }
        rank++;
    }

    // Add merged block to free list
    free_block_t *block = (free_block_t *)get_page_addr(page_idx);
    block->next = free_lists[rank];
    block->prev = NULL;
    if (free_lists[rank] != NULL) {
        free_lists[rank]->prev = block;
    }
    free_lists[rank] = block;

    // Mark as free
    page_metadata[page_idx].is_free = 1;
    page_metadata[page_idx].rank = rank;

    return OK;
}

int query_ranks(void *p) {
    int page_idx = get_page_index(p);
    if (page_idx < 0) {
        return -EINVAL;
    }

    // If allocated, return the rank
    if (!page_metadata[page_idx].is_free && page_metadata[page_idx].rank > 0) {
        return page_metadata[page_idx].rank;
    }

    // If unallocated, find the maximum rank this page belongs to
    for (int rank = MAX_RANK; rank >= 1; rank--) {
        if (!is_aligned(page_idx, rank)) continue;

        int block_size = 1 << (rank - 1);
        if (page_idx + block_size > total_pages) continue;

        // Check if this block is in the free list
        if (page_metadata[page_idx].is_free && page_metadata[page_idx].rank == rank) {
            return rank;
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
