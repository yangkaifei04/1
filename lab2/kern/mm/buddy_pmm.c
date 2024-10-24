
#include <pmm.h>
#include <list.h>
#include <string.h>
#include <buddy_pmm.h>
#include<stdio.h>
#define MAX_ORDER 11

free_area_t free_area[MAX_ORDER];

//#define free_list(i) free_area[i].free_list
//#define nr_free(i) free_area[i].nr_free



static void
buddy_init(void) {
    for(int i=0;i<MAX_ORDER;i++){
        list_init(& free_area[i].free_list);
        free_area[i].nr_free = 0;
    }
}


static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    size_t curr_size = n;
    uint32_t order = MAX_ORDER - 1;
    uint32_t order_size = 1 << order;
    p = base;
    while (curr_size != 0) {
        while(order > 0 && curr_size < order_size) {
            order_size >>= 1;
            order -= 1;
        }
        p->property = order_size;
        SetPageProperty(p);
        free_area[order].nr_free += 1;
        list_add_before(&(free_area[order].free_list), &(p->page_link));
        curr_size -= order_size;
        p += order_size;
    }
}

static struct Page *buddy_alloc_pages(size_t n) {
    if (n == 0) return NULL;

 //   cprintf("Allocating %zu pages\n", n); // 输出分配提示信息
    
    int order;
    for (order = 0; (1 << order) < n; order++); // 找到合适的order
    
 //   cprintf("Found order: %d for size: %zu\n", order, n); // 输出 order 信息


    for (int current_order = order; current_order < MAX_ORDER; current_order++) {
        if (!list_empty(&free_area[current_order].free_list)) {
            cprintf("Found free block at order %d\n", current_order);

            // 从更大的order中分配
            list_entry_t *le = list_next(&free_area[current_order].free_list);
            struct Page *page = le2page(le, page_link);
            list_del(&(page->page_link));
            free_area[current_order].nr_free--;
            
            // 输出调试信息
  //          cprintf("Allocating page at address: %p with size: %d\n", page, 1 << current_order);

            // 将大块分割成多个小块
            while (current_order > order) {
                current_order--;
                struct Page *buddy = page + (1 << current_order);
                buddy->property = 1 << current_order;
                SetPageProperty(buddy);
                cprintf("Split block, added buddy at address: %p with size: %d\n", buddy, 1 << current_order);
                list_add(&free_area[current_order].free_list, &buddy->page_link);
                free_area[current_order].nr_free++;
            }

            // 清除属性标志

            ClearPageProperty(page);

            return page;
        }
    }

    // 如果没有找到合适的块，返回 NULL
 //  cprintf("No suitable block found for size: %zu\n", n);
    return NULL;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    int order = 0;
    while ((1 << order) < n && order < MAX_ORDER) {
        order++;
    }

    base->property = n;
    SetPageProperty(base);

    // 插入到相应 order 的空闲链表中
    list_add(&free_area[order].free_list, &(base->page_link));


    // 开始合并相邻的 buddy 块
    while (order < MAX_ORDER - 1) {
        list_entry_t* le = list_prev(&(base->page_link));

        // 检查是否有相邻的前一个块可以合并
        if (le != &(free_area[order].free_list)) {
            struct Page *prev_page = le2page(le, page_link);

            // 如果前一个块和当前块是 buddy，则合并
            if (prev_page + (1 << order) == base) {
               
                prev_page->property += base->property;
                ClearPageProperty(base);
                list_del(&(base->page_link));  // 从当前 order 的列表中删除
                base = prev_page;  // 更新 base 为合并后的块
            }
            list_del(&(base->page_link));
            order++;
            list_add(&free_area[order].free_list, &(base->page_link));
            continue;
        }

        // 再检查后一个块是否可以合并
        le = list_next(&(base->page_link));
        if (le != &(free_area[order].free_list)) {
            struct Page *next_page = le2page(le, page_link);

            // 如果后一个块和当前块是 buddy，则合并
            if (base + (1 << order) == next_page) {
                cprintf("Merging with next buddy at address: %p (size: %d)\n", next_page, 1 << order);
                base->property += next_page->property;
                ClearPageProperty(next_page);
                list_del(&(next_page->page_link));  // 从当前 order 的列表中删除
            }
            list_del(&(base->page_link));
            order++;
            list_add(&free_area[order].free_list, &(base->page_link));
            continue;
        }

        // 如果没有更多 buddy 可以合并，退出循环
        if (list_next(&(base->page_link)) == &(free_area[order].free_list) &&
            list_prev(&(base->page_link)) == &(free_area[order].free_list)) {
            break;
        }
        break;


    }
}


static size_t
buddy_nr_free_pages(void) {
    size_t total = 0;
    for (int i = 0; i < MAX_ORDER; i++) {
        total += free_area[i].nr_free * (1 << i);  
    }
    return total;
}

static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    for(int i = 0; i < MAX_ORDER; i++) {
        list_init(&(free_area[i].free_list));
        assert(list_empty(&(free_area[i].free_list)));
    }

    for(int i = 0; i < MAX_ORDER; i++) free_area[i].nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
  //  assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

 //   assert(alloc_page() == NULL);

    free_page(p0);
    for(int i = 0; i < 0; i++) 
        assert(!list_empty(&(free_area[i].free_list)));

    struct Page *p;
    assert((p = alloc_page()) == p0);
 //   assert(alloc_page() == NULL);

   // assert(nr_free == 0);

    free_page(p);
    free_page(p1);
    free_page(p2);
}

// LAB2: below code is used to check the first fit allocation algorithm
// NOTICE: You SHOULD NOT CHANGE basic_check, default_check functions!
static void
buddy_check(void) {

    cprintf("Starting buddy system check...\n");

    // 尝试分配一个较大的块
    cprintf("Allocating 4 pages...\n");
    struct Page *p0 = buddy_alloc_pages(4);  // 分配4页
    assert(p0 != NULL);
    cprintf("Successfully allocated 4 pages at %p.\n", p0);


   
    // 分配尽可能大的块
    cprintf("Allocating 32 pages...\n");
    struct Page *p2 = buddy_alloc_pages(32);  // 分配32页
    assert(p2 != NULL);
    cprintf("Successfully allocated 8 pages at %p.\n", p2);

    cprintf("Allocating 1024 pages...\n");
    struct Page *p3 = buddy_alloc_pages(1024);  // 分配1024
    assert(p3 != NULL);
    cprintf("Successfully allocated 1024 pages at %p.\n", p3);

    cprintf("Allocating 1024 pages...\n");
    struct Page *p1 = buddy_alloc_pages(1024);  // 分配1024
    assert(p3 != NULL);
    cprintf("Successfully allocated 1024 pages at %p.\n", p1);

    
  //  assert(p0 != p1 && p0!= p2 && p1 != p2);

    // 释放p0，并检查是否正确合并
    cprintf("Freeing 4 pages at %p...\n", p0);
    buddy_free_pages(p0, 4);  // 释放4页
    assert(free_area[2].nr_free > 0);  // 检查free_list
    cprintf("Successfully freed and merged 4 pages.\n");



    // 释放所有分配的页面
    cprintf("Freeing all allocated pages...\n");
    buddy_free_pages(p3, 1024);  // 释放1页
    buddy_free_pages(p2, 32);  // 释放8页
    buddy_free_pages(p1, 4);  // 释放4页
    cprintf("Successfully freed all pages.\n");



    cprintf("buddy_check() succeeded!\n");
}


//这个结构体在
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

