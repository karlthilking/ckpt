/* proc_list.c */
#include "xnd/xnd.h"
#include "proc_list.h"
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>

struct proc_list *proc_list_init(void)
{
        struct proc_list *list;

        list = calloc(1, sizeof(struct proc_list));
        xnd_assert(list != NULL);

        return list;
}

void proc_list_destroy(struct proc_list *list)
{
        struct proc *p, *next;

        proc_foreach_safe(p, next, list) {
                if (p->cleanup) {
                        p->cleanup(p);
                }
                free(p);
        }

        free(list);
}

void proc_list_add(struct proc_list *list, struct proc *p)
{
        p->prev = NULL;
        p->next = list->head;
        list->head = p;

        if (p->next) {
                p->next->prev = p;
        }

        list->size++;
}

void proc_list_remove(struct proc_list *list, struct proc *p)
{
        if (p == list->head) {
                xnd_assert(p->prev == NULL);
                list->head = p->next;
        } else {
                xnd_assert(p->prev != NULL);
                p->prev->next = p->next;
        }

        if (p->next) {
                p->next->prev = p->prev;
        }

        if (p->cleanup) {
                p->cleanup(p);
        }

        free(p);
        list->size--;
}

void proc_list_filter(struct proc_list *list)
{
        int             err;
        struct proc     *p, *next;

        proc_foreach_safe(p, next, list) {
                err = kill(p->real_pid, 0);
                if (err != 0 && errno == ESRCH) {
                        proc_list_remove(list, p);
                }
        }
}

struct proc *proc_list_find_by_real_pid(struct proc_list *list, pid_t real)
{
        struct proc *p;

        proc_foreach(p, list) {
                if (p->real_pid == real) {
                        return p;
                }
        }

        return NULL;
}

struct proc *proc_list_find_by_virt_pid(struct proc_list *list, pid_t virt)
{
        struct proc *p;

        proc_foreach(p, list) {
                if (p->virt_pid == virt) {
                        return p;
                }
        }

        return NULL;
}

struct proc *proc_list_find_by_xnd_pid(struct proc_list *list, u32 xnd_pid)
{
        struct proc *p;

        proc_foreach(p, list) {
                if (p->xnd_pid == xnd_pid) {
                        return p;
                }
        }

        return NULL;
}

pid_t proc_list_real_to_virt(struct proc_list *list, pid_t real)
{
        struct proc *p;

        proc_foreach(p, list) {
                if (p->real_pid == real) {
                        return p->virt_pid;
                }
        }

        return -1;
}

pid_t proc_list_virt_to_real(struct proc_list *list, pid_t virt)
{
        struct proc *p;

        proc_foreach(p, list) {
                if (p->virt_pid == virt) {
                        return p->real_pid;
                }
        }

        return -1;
}
