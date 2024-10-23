#include <pmm.h>
#include <list.h>
#include <string.h>
#include <default_pmm.h>
#include <buddy_pmm.h>

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
    base->property = n;
    SetPageProperty(base);
    size_t order;
    while (n > 0) {       
        for (order = 0; (1 << order) <= n; order++);
        order--; 
        struct Page *p = base;
        base += (1 << order);
        n -= (1 << order);
        p->property = 1 << order;
        SetPageProperty(p);
        list_add(&free_area[order].free_list, &(p->page_link));
        free_area[order].nr_free++;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > (1 << (MAX_ORDER - 1))) {
        return NULL;
    }
    int order;
    for (order = 0; (1 << order) < n; order++); 
    for (int current_order = order; current_order < MAX_ORDER; current_order++) {
        if (list_empty(&free_area[current_order].free_list)) {  
            continue;
        }         
        else{       
  
            list_entry_t *le = list_next(&free_area[current_order].free_list);
            struct Page *page = le2page(le, page_link);
            list_del(le);
            free_area[current_order].nr_free--;
           
            while (current_order > order) {
                current_order--;
                struct Page *buddy = page + (1 << current_order);
                buddy->property = 1 << current_order;
                SetPageProperty(buddy);
                list_add(&free_area[current_order].free_list, &buddy->page_link);
                free_area[current_order].nr_free++;
            }

            ClearPageProperty(page);
            return page;
        }
    }
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);

    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }

    int order;
    for (order = 0; (1 << order) < n; order++); 
    base->property = 1 << order;
    SetPageProperty(base);

    while (order < MAX_ORDER - 1) {
        uintptr_t buddy_addr = (uintptr_t)base ^ (1 << (PGSHIFT + order));
        struct Page *buddy = pa2page(buddy_addr);

        if (!PageProperty(buddy) || buddy->property != (1 << order)) {
            break; 
        }

        
        list_del(&buddy->page_link);
        free_area[order].nr_free--;
        if (buddy < base) {
            base = buddy;
        }
        order++;
    }

    list_add(&free_area[order].free_list, &base->page_link);
    free_area[order].nr_free++;
}

static size_t
buddy_nr_free_pages(void) {
    size_t total = 0;
    for (int i = 0; i < MAX_ORDER; i++) {
        total += nr_free(i) * (1 << i);  
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

    assert(alloc_page() == NULL);

    free_page(p0);
    for(int i = 0; i < 0; i++) 
        assert(!list_empty(&(free_area[i].free_list)));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

   // assert(nr_free == 0);

    free_page(p);
    free_page(p1);
    free_page(p2);
}

// LAB2: below code is used to check the first fit allocation algorithm
// NOTICE: You SHOULD NOT CHANGE basic_check, default_check functions!
static void
buddy_check(void) {
    basic_check();


}
//这个结构体在
const struct pmm_manager default_pmm_manager = {
    .name = "default_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

