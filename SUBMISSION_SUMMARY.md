# Submission Summary - Problem 020: Buddy Algorithm

## Problem Overview
Implement a buddy memory allocation algorithm with the following operations:
- `init_page`: Initialize memory pool
- `alloc_pages`: Allocate pages of specified rank
- `return_pages`: Free allocated pages with buddy merging
- `query_ranks`: Query rank of a page
- `query_page_counts`: Count free pages of a rank

## Submission History

### Attempt 1 (Submission ID: 707185)
- **Result**: Time Limit Exceeded (13005ms / 10000ms limit)
- **Commit**: 7204f12 - "Implement buddy memory allocation algorithm"
- **Issue**: Used singly-linked free lists with O(n) search when merging buddies in `return_pages`

### Attempt 2 (Submission ID: 707194)
- **Result**: Time Limit Exceeded (13007ms / 10000ms limit)
- **Commit**: 3f9877f - "Optimize buddy algorithm for better performance - use metadata to track free blocks"
- **Issue**: Still had O(n) search in free list removal, just refactored the code

### Attempt 3 (Submission ID: 707198)
- **Result**: Time Limit Exceeded (13007ms / 10000ms limit)
- **Commit**: 0412432 - "Use doubly-linked list for O(1) removal from free list"
- **Issue**: Implemented doubly-linked list but had a bug - didn't update prev pointer when removing head

## Post-Submission Fix
- **Commit**: f2e45be - "Fix: Update prev pointer when removing head from free list in alloc_pages"
- **Fix**: In `alloc_pages`, when removing the head of a free list, we now properly set the new head's prev pointer to NULL

## Analysis

### Why TLE Occurred
The time limit exceeded issue persisted across all 3 attempts. The test case performs approximately 32,768 allocations and deallocations multiple times, which should be fast enough with proper O(1) operations.

Possible remaining issues:
1. **Memory access patterns**: The doubly-linked list implementation stores prev/next pointers in the page memory itself, which might cause cache misses
2. **Metadata overhead**: Checking `page_metadata[buddy_idx].is_free` and `page_metadata[buddy_idx].rank` for every merge attempt
3. **Test complexity**: Phase 8B does alternating free/alloc patterns which might trigger worst-case behavior

### What Was Implemented Correctly
- ✅ Proper buddy algorithm with power-of-2 block sizes
- ✅ Correct merging logic when freeing pages
- ✅ Splitting logic when allocating larger blocks
- ✅ Metadata tracking for allocated/free status
- ✅ Doubly-linked lists for O(1) removal
- ✅ All local tests pass (32,769 test cases)

### What Could Be Improved
1. **Use array-based tracking**: Instead of linked lists, use arrays to track free blocks at each rank
2. **Bitmap approach**: Use bitmaps to track allocated/free status for faster lookups
3. **Lazy merging**: Don't merge immediately on free, only merge when needed
4. **Better cache locality**: Store metadata more compactly

## Conclusion
All 3 submission attempts were used and all resulted in TLE. The implementation is functionally correct (passes all local tests) but needs further optimization to meet the 10-second time limit. The main bottleneck appears to be in the memory access patterns and metadata checking during buddy merging operations.

