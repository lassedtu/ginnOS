#include "vma.h"

#include "page.h"
#include "common/memory.h"

/**
 * index of the first area whose end is above addr (the area containing addr,
 * or the first one after it). equals table->count if there is none.
 */
static uint32_t first_ending_after(const vma_table_t *table, uintptr_t addr)
{
    uint32_t i = 0;
    while (i < table->count && table->areas[i].end <= addr)
    {
        i++;
    }
    return i;
}

/**
 * open a slot at index, shifting later areas up. the caller has checked
 * there is room.
 */
static void open_slot(vma_table_t *table, uint32_t index)
{
    memmove(&table->areas[index + 1], &table->areas[index],
            (table->count - index) * sizeof(vma_t));
    table->count++;
}

/**
 * remove the area at index, shifting later areas down.
 */
static void close_slot(vma_table_t *table, uint32_t index)
{
    memmove(&table->areas[index], &table->areas[index + 1],
            (table->count - index - 1) * sizeof(vma_t));
    table->count--;
}

/**
 * true if [start, end) is a non-empty, page-aligned range.
 */
static bool valid_range(uintptr_t start, uintptr_t end)
{
    return start < end && PAGE_IS_ALIGNED(start) && PAGE_IS_ALIGNED(end);
}

void vma_table_init(vma_table_t *table)
{
    table->count = 0;
}

kerr_t vma_insert(vma_table_t *table, uintptr_t start, uintptr_t end, uint32_t prot,
                  vma_kind_t kind)
{
    if (!valid_range(start, end))
    {
        return KERR_INVAL;
    }
    if (!vma_range_free(table, start, end))
    {
        return KERR_EXIST;
    }
    if (table->count == VMA_MAX)
    {
        return KERR_NOSPC;
    }

    uint32_t index = first_ending_after(table, start);
    open_slot(table, index);
    table->areas[index].start = start;
    table->areas[index].end = end;
    table->areas[index].prot = prot;
    table->areas[index].kind = kind;
    return KERR_OK;
}

vma_t *vma_find(vma_table_t *table, uintptr_t addr)
{
    uint32_t index = first_ending_after(table, addr);
    if (index < table->count && table->areas[index].start <= addr)
    {
        return &table->areas[index];
    }
    return NULL;
}

vma_t *vma_find_kind(vma_table_t *table, vma_kind_t kind)
{
    for (uint32_t i = 0; i < table->count; i++)
    {
        if (table->areas[i].kind == kind)
        {
            return &table->areas[i];
        }
    }
    return NULL;
}

bool vma_range_free(const vma_table_t *table, uintptr_t start, uintptr_t end)
{
    uint32_t index = first_ending_after(table, start);
    return index == table->count || table->areas[index].start >= end;
}

uintptr_t vma_find_free(const vma_table_t *table, size_t length, uintptr_t low, uintptr_t high)
{
    uintptr_t candidate = low;

    // walk the areas in address order, trying the gap before each
    for (uint32_t i = first_ending_after(table, low); i <= table->count; i++)
    {
        uintptr_t gap_end = i < table->count ? table->areas[i].start : high;
        if (gap_end > high)
        {
            gap_end = high;
        }

        if (gap_end > candidate && gap_end - candidate >= length)
        {
            return candidate;
        }

        if (i < table->count && table->areas[i].end > candidate)
        {
            candidate = table->areas[i].end;
        }
        if (candidate >= high)
        {
            break;
        }
    }

    return 0;
}

kerr_t vma_remove(vma_table_t *table, uintptr_t start, uintptr_t end)
{
    if (!valid_range(start, end))
    {
        return KERR_INVAL;
    }

    uint32_t i = first_ending_after(table, start);
    while (i < table->count && table->areas[i].start < end)
    {
        vma_t *area = &table->areas[i];

        if (area->start < start && area->end > end)
        {
            // the range is inside this area: split it into two
            if (table->count == VMA_MAX)
            {
                return KERR_NOSPC;
            }
            open_slot(table, i + 1);
            table->areas[i + 1] = table->areas[i];
            table->areas[i].end = start;
            table->areas[i + 1].start = end;
            return KERR_OK;
        }

        if (area->start < start)
        {
            // the range covers this area's tail
            area->end = start;
            i++;
        }
        else if (area->end > end)
        {
            // the range covers this area's head
            area->start = end;
            i++;
        }
        else
        {
            // the range covers the whole area
            close_slot(table, i);
        }
    }

    return KERR_OK;
}
