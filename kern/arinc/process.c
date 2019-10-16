#include <process.h>
#include <pmm.h>
#include <string.h>
#include <bitmap.h>
#include <assert.h>

static list_t   all_proc_set;

static task_t *init_proc;

static bitmap_t   pid_map;

static void pid_map_init(void) {
    size_t bmap_buff_sz = (SYSTEM_LIMIT_NUMBER_OF_PARTITIONS * 
        SYSTEM_LIMIT_NUMBER_OF_PROCESSES) >> 3;
    
    pid_map.len = bmap_buff_sz;
    if ((pid_map.buff = kmalloc(bmap_buff_sz)) == NULL) {
        panic("pid map init alloc buff failed.\n");
    }

    bitmap_init(&pid_map);
}

static pid_t alloc_pid(void) {
    int pid;
    if ((pid = bitmap_scan(&pid_map, 1)) < 0) {
        warn("alloc pid fail.\n");
    }
    return pid;
} 

inline static void free_pid(pid_t pid) {
    bitmap_remove(&pid_map, pid);
}

static task_t *alloc_proc(void) {
    task_t *task;
    page_t *page;
    if ((page = kalloc_pages(KSTACKPAGE)) == NULL) {
        return NULL;
    }
    task = (task_t*)page2kvaddr(page);

    // empty context
    memset(&task->ctxt, 0, sizeof(context_t));

    uintptr_t stack_ed = (uintptr_t)task + KSTACKSIZE;
    task->kstack = (uint8_t*)stack_ed;
    stack_ed -= sizeof(trapframe_t);

    task->tf = (trapframe_t*)stack_ed;
    task->pid = -1;
    task->mm = NULL;
    task->ticks = 5;

    proc_period(task) = 0;
    proc_base_prio(task) = 1;
    proc_cur_prio(task) = proc_base_prio(task);
    proc_stack_size(task) = KSTACKSIZE;
    proc_state(task) = DORMANT;

    return task;
}



static void make_init_thread(void) {
    init_proc = current_thread;

    process_attribute_t attr = DEFAULT_THREAD_ATTR(NULL, "init");
    init_proc->status.attributes = attr;
    init_proc->pid = 0;
    // mark pid 0 used
    bitmap_set(&pid_map, 0);

    init_proc->mm = NULL;
    init_proc->kstack = (uintptr_t)init_proc + KSTACKSIZE;
    init_proc->ticks = proc_time_capa(init_proc);

    proc_state(init_proc) = RUNNING;
    proc_cur_prio(init_proc) = proc_base_prio(init_proc);

    list_push_back(&all_proc_set, &init_proc->all_tag);

}


void process_init(void) {
    list_init(&all_proc_set);

    // pid bitmap init
    check_bitmap();
    pid_map_init();    
    //make_init_thread();

    cprintf("process init done.\n");
}