/**
 * @file processes.c
 * @brief Huginn: the Processes tab: each process with its CPU, memory,
 *        user and pid, a filter, the order, and End process.
 *
 * the list is filled again each second, in place; the chosen process
 * stays chosen (by its pid). End process sends SIGTERM after a question; Force end
 * (in the menu of a right click) sends SIGKILL.
 */

#include "huginn.h"

#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/input.h>

#define SHOWN_MAX 256

typedef enum
{
    ORDER_CPU,
    ORDER_MEMORY,
    ORDER_NAME,
    ORDER_PID,
    ORDER_USER,
} order_t;

#define ORDERS 5

static struct
{
    idunn_widget_t *list;
    idunn_widget_t *filter;
    idunn_widget_t *end_button;
    idunn_widget_t *count;
    idunn_widget_t *heads;
    idunn_menu_t *order_menu;
    idunn_menu_t *item_menu;
    order_t order;
    bool reverse; // the other way from the order's own (a second click on a head)
    uint32_t shown[SHOWN_MAX]; // the pids in the list, in its order
    int shown_count;
    uint32_t chosen; // the chosen pid, or 0
    uint32_t asked;  // the pid a question is about
    int asked_signal;
} P;

static const proc_t *proc_of_pid(uint32_t pid)
{
    return pid ? sample_find(&H.now, pid) : NULL;
}

static int compare(const void *pa, const void *pb)
{
    const proc_t *a = proc_of_pid(*(const uint32_t *)pa);
    const proc_t *b = proc_of_pid(*(const uint32_t *)pb);
    switch (P.order)
    {
    case ORDER_CPU:
        if (a->cpu != b->cpu)
            return a->cpu > b->cpu ? -1 : 1;
        if (a->rss_kb != b->rss_kb)
            return a->rss_kb > b->rss_kb ? -1 : 1;
        break;
    case ORDER_MEMORY:
        if (a->rss_kb != b->rss_kb)
            return a->rss_kb > b->rss_kb ? -1 : 1;
        break;
    case ORDER_NAME:
    {
        int d = strcasecmp(a->name, b->name);
        if (d)
            return d;
        break;
    }
    case ORDER_USER:
        if (a->uid != b->uid)
            return a->uid < b->uid ? -1 : 1;
        break;
    default:
        break;
    }
    return a->pid < b->pid ? -1 : a->pid > b->pid;
}

static int compare_ordered(const void *pa, const void *pb)
{
    return P.reverse ? compare(pb, pa) : compare(pa, pb);
}

/** true when the filter text is in the name, or is the pid */
static bool matches(const proc_t *p, const char *filter)
{
    if (!filter[0])
        return true;
    char pid[16];
    snprintf(pid, sizeof(pid), "%u", p->pid);
    if (strcmp(pid, filter) == 0)
        return true;
    size_t n = strlen(filter);
    for (const char *s = p->name; *s; s++)
        if (strncasecmp(s, filter, n) == 0)
            return true;
    return false;
}

static const char *user_name(uint32_t uid)
{
    static uint32_t last_uid = (uint32_t)-1;
    static char last[32];
    if (uid != last_uid)
    {
        struct passwd *pw = getpwuid(uid);
        if (pw)
            snprintf(last, sizeof(last), "%s", pw->pw_name);
        else
            snprintf(last, sizeof(last), "%u", uid);
        last_uid = uid;
    }
    return last;
}

/** the picture of a process: its program's, when it has a .app file */
static const char *picture_of(const proc_t *p)
{
    const app_t *app = H.apps ? apps_find(H.apps, p->name) : NULL;
    return app && app->icon[0] ? app->icon : "application-x-executable";
}

static void show_chosen(void)
{
    idunn_set_disabled(P.end_button, proc_of_pid(P.chosen) == NULL);
}

void processes_update(void)
{
    const char *filter = idunn_text_field_text(P.filter);
    P.shown_count = 0;
    for (int i = 0; i < H.now.count && P.shown_count < SHOWN_MAX; i++)
        if (matches(&H.now.procs[i], filter))
            P.shown[P.shown_count++] = H.now.procs[i].pid;
    qsort(P.shown, (size_t)P.shown_count, sizeof(uint32_t), compare_ordered);

    // the rows are changed in place: only a row that differs is drawn again
    int rows = idunn_list_view_count(P.list);
    for (int i = 0; i < P.shown_count; i++)
    {
        const proc_t *p = proc_of_pid(P.shown[i]);
        char memory[24];
        format_kb(p->rss_kb, memory, sizeof(memory));
        char detail[128];
        snprintf(detail, sizeof(detail), "%.1f %%\t%s\t%s\t%u", p->cpu, memory,
                 user_name(p->uid), p->pid);
        if (i < rows)
            idunn_list_view_set_item(P.list, i, picture_of(p), p->name, detail);
        else
            idunn_list_view_add(P.list, picture_of(p), p->name, detail);
        idunn_list_view_select(P.list, i, p->pid == P.chosen);
    }
    if (P.shown_count < rows)
        idunn_list_view_set_count(P.list, P.shown_count);
    char text[64];
    snprintf(text, sizeof(text), P.shown_count == 1 ? "1 process" : "%d processes",
             P.shown_count);
    idunn_label_set_text(P.count, text);
    show_chosen();
}

static void selected(idunn_widget_t *list, void *data)
{
    (void)data;
    P.chosen = 0;
    for (int i = 0; i < P.shown_count; i++)
        if (idunn_list_view_selected(list, i))
            P.chosen = P.shown[i];
    show_chosen();
}

static void send_signal(idunn_widget_t *w, void *data)
{
    (void)w;
    (void)data;
    const proc_t *p = proc_of_pid(P.asked);
    if (!p)
        return;
    if (kill((pid_t)p->pid, P.asked_signal) != 0)
    {
        char text[160];
        snprintf(text, sizeof(text), "%s (%u): %s.", p->name, p->pid, strerror(errno));
        idunn_widget_t *dialog = idunn_dialog("Could not end process", text);
        idunn_dialog_add_button(dialog, "OK", IDUNN_PRIMARY, NULL, NULL);
        idunn_dialog_open(P.list, dialog);
    }
}

/** ask, then send SIGTERM (or SIGKILL: force) to the chosen process */
static void ask_end(bool force)
{
    const proc_t *p = proc_of_pid(P.chosen);
    if (!p)
        return;
    P.asked = p->pid;
    P.asked_signal = force ? SIGKILL : SIGTERM;
    char title[96];
    snprintf(title, sizeof(title), force ? "Kill %s?" : "End %s?", p->name);
    idunn_widget_t *dialog = idunn_dialog(
        title, force ? "The process stops immediately. Unsaved work will be lost."
                     : "The process is asked to quit. Unsaved work may be lost.");
    idunn_dialog_add_button(dialog, "Cancel", IDUNN_OUTLINE, NULL, NULL);
    idunn_dialog_add_button(dialog, force ? "Kill process" : "End process", IDUNN_DESTRUCTIVE,
                            send_signal, NULL);
    idunn_dialog_open(P.list, dialog);
}

static void end_clicked(idunn_widget_t *b, void *data)
{
    (void)b;
    (void)data;
    ask_end(false);
}

static void menu_chosen(idunn_widget_t *w, void *data)
{
    (void)w;
    ask_end(data != NULL);
}

static void item_menu(idunn_widget_t *list, void *data)
{
    (void)data;
    selected(list, NULL);
    if (proc_of_pid(P.chosen))
        idunn_menu_popup_at_pointer(P.item_menu, list);
}

static void filter_typed(idunn_widget_t *field, void *data)
{
    (void)field;
    (void)data;
    processes_update();
}

/** the head of each order: Name, CPU, Memory, User, PID */
static const int head_of[ORDERS] = {1, 2, 0, 4, 3};
static const order_t order_of_head[ORDERS] = {ORDER_NAME, ORDER_CPU, ORDER_MEMORY, ORDER_USER,
                                              ORDER_PID};

static void set_order(order_t order, bool reverse)
{
    P.order = order;
    P.reverse = reverse;
    for (int i = 0; i < 4; i++)
        idunn_menu_set_checked(P.order_menu, i, i == (int)P.order);
    // CPU and memory start with the largest
    bool large_first = order == ORDER_CPU || order == ORDER_MEMORY;
    idunn_list_heads_set_order(P.heads, head_of[order], large_first != reverse);
    processes_update();
}

static void order_chosen(idunn_widget_t *w, void *data)
{
    (void)w;
    set_order((order_t)(intptr_t)data, false);
}

static void head_clicked(idunn_widget_t *heads, void *data)
{
    (void)data;
    int head = idunn_list_heads_clicked(heads);
    if (head < 0 || head >= ORDERS)
        return;
    order_t order = order_of_head[head];
    set_order(order, order == P.order ? !P.reverse : false);
}

static void order_clicked(idunn_widget_t *b, void *data)
{
    (void)data;
    idunn_menu_popup(P.order_menu, b);
}

bool processes_key(uint16_t code, uint32_t mods)
{
    if (code == KEY_DELETE && P.chosen)
    {
        ask_end(mods & IDUNN_MOD_SHIFT);
        return true;
    }
    if (code == KEY_F && (mods & IDUNN_MOD_CTRL))
    {
        idunn_set_focus(P.filter);
        return true;
    }
    return false;
}

idunn_widget_t *processes_page(void)
{
    static const int32_t columns[] = {64, 80, 72, 48}; // CPU, memory, user, pid

    idunn_widget_t *page = idunn_column(2, 0);
    idunn_box_set_padding(page, 3, 0);

    idunn_widget_t *bar = idunn_add(page, idunn_row(2, 0));
    idunn_box_set_align(bar, IDUNN_CENTER, IDUNN_START);
    P.filter = idunn_grow(idunn_add(bar, idunn_text_field("Filter by name or PID")), 1);
    idunn_text_field_set_icon(P.filter, "search");
    idunn_text_field_on_change(P.filter, filter_typed, NULL);
    P.order_menu = idunn_menu_new();
    idunn_menu_add_check(P.order_menu, "CPU", true, order_chosen, (void *)ORDER_CPU);
    idunn_menu_add_check(P.order_menu, "Memory", false, order_chosen, (void *)ORDER_MEMORY);
    idunn_menu_add_check(P.order_menu, "Name", false, order_chosen, (void *)ORDER_NAME);
    idunn_menu_add_check(P.order_menu, "PID", false, order_chosen, (void *)ORDER_PID);
    idunn_widget_t *order = idunn_add(bar, idunn_icon_button("arrow-down-wide-narrow",
                                                             IDUNN_OUTLINE, order_clicked, NULL));
    idunn_set_tooltip(order, "Sort");
    P.end_button = idunn_add(bar, idunn_button("End process", IDUNN_DESTRUCTIVE, end_clicked,
                                               NULL));
    idunn_set_tooltip(P.end_button, "End the selected process (Delete)");

    P.list = idunn_list_view();
    idunn_list_view_set_pictures(P.list, true);
    idunn_list_view_set_framed(P.list, true);
    idunn_list_view_set_columns(P.list, columns, 4);
    idunn_list_view_set_columns_left(P.list, 1u << 2); // the user
    idunn_list_view_on_select(P.list, selected, NULL);
    idunn_list_view_on_menu(P.list, item_menu, NULL);
    static const char *const names[ORDERS] = {"Name", "CPU", "Memory", "User", "PID"};
    P.heads = idunn_add(page, idunn_list_heads(P.list, names, ORDERS, head_clicked, NULL));
    idunn_list_heads_set_order(P.heads, head_of[ORDER_CPU], true);
    idunn_grow(idunn_add(page, idunn_scroll_view(P.list)), 1);

    P.item_menu = idunn_menu_new();
    idunn_menu_add(P.item_menu, "x", "End process", "Delete", menu_chosen, NULL);
    idunn_menu_add(P.item_menu, NULL, "Kill process", "Shift+Delete", menu_chosen, (void *)1);

    P.count = idunn_add(page, idunn_label(" ", IDUNN_TEXT_SMALL));
    idunn_label_set_color(P.count, IDUNN_COLOR_MUTED_FOREGROUND);
    return page;
}
