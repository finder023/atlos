#include <process.h>
#include <pmm.h>
#include <string.h>
#include <bitmap.h>
#include <assert.h>
#include <mmu.h>
#include <error.h>
#include <x86.h>

static list_t   all_proc_set;

task_t *init_proc;

static bitmap_t   pid_map;

void kernel_thread_entry_asm(void) {
    // call func(args), call do_exit
    asm volatile("pushl %%edx\n\t"
                 "call *%%ebx\n\t"
                 "pushl %%eax\n\t"
                 "call do_exit\n":::"memory");
}

int do_exit(int);

void kernel_thread_entry(void);
void forkrets(trapframe_t *tf);
void switch_to(context_t *from, context_t *to);


static void forkret(void) {
    forkrets(current_thread->tf);
}

static void kthread_ret(void) {
    task_t *task = current_thread;
    asm volatile("movl %0, %%esp; jmp __trapret" :: 
                    "g" (task->tf) : "memory");
}

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
    if ((task = kmalloc(KSTACKSIZE)) == NULL) {
        return task;
    }

    // empty pages 
    memset(task, 0, KSTACKSIZE);

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

int do_exit(int eno) {

}

static int kernel_thread(int (*func)(void*), void *arg) {
    int eflag;

    task_t *task;
    if ((task = alloc_proc()) == NULL) {
        eflag = E_NO_MEM;
        goto ret;
    }

    if ((task->pid = alloc_pid()) < 0) {
        eflag = E_NO_FREE_PROC;
        goto alloc_pid_fail;
    }

    // set trap frame
    task->tf->tf_cs = KERNEL_CS;
    task->tf->tf_ds = task->tf->tf_es = task->tf->tf_ss = KERNEL_DS;
    task->tf->tf_regs.reg_ebx = (uintptr_t)func;
    task->tf->tf_regs.reg_edx = (uintptr_t)arg;
    task->tf->tf_eip = (uintptr_t)kernel_thread_entry_asm;

    task->tf->tf_regs.reg_eax = 0;
    // task->tf->tf_esp = 0;
    task->tf->tf_eflags |= FL_IF;

    // set context
    task->ctxt.eip = (uintptr_t)kthread_ret;
    task->ctxt.esp = (uintptr_t)(task->tf);

    list_push_back(&all_proc_set, &task->all_tag);
    eflag = 0;
    goto ret;

alloc_pid_fail:
    kfree(task);

ret:
    return eflag;
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
    init_proc->tf = init_proc->kstack - sizeof(trapframe_t);
    init_proc->ticks = proc_time_capa(init_proc);

    proc_state(init_proc) = RUNNING;
    proc_cur_prio(init_proc) = proc_base_prio(init_proc);

    // list_push_back(&all_proc_set, &init_proc->all_tag);

}

static void thread_func(void *arg) {
    cprintf("this is a new thread: %d.\n", *(int*)arg);
    while (1);
}

static void check_kernel_thread(void) {
    int *arg1 = kmalloc(sizeof(int));
    int *arg2 = kmalloc(sizeof(int));

    *arg1 = 1;
    *arg2 = 2;
    kernel_thread(thread_func, arg1);
    kernel_thread(thread_func, arg2);
}


void proc_run(task_t *task) {

}


void schedule(void) {
    task_t *cur = current_thread;
    if (cur != init_proc)
        list_push_back(&all_proc_set, &cur->all_tag);
    list_elem_t *nelem = list_pop_front(&all_proc_set);
    task_t *next = le2task(nelem);

    load_esp0(next->kstack);
    lcr3(boot_cr3);
    switch_to(&cur->ctxt, &next->ctxt);
}

void process_init(void) {
    list_init(&all_proc_set);

    // pid bitmap init
    check_bitmap();
    pid_map_init();    
    make_init_thread();
    check_kernel_thread();

    cprintf("process init done.\n");
}