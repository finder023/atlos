#ifndef __L_PROCESS_H
#define __L_PROCESS_H

#include <types.h>
#include <list.h>
#include <trap.h>
#include <memlayout.h>
#include <arinc_proc.h>
#include <vmm.h>

#define THREADMASK  0xffffe000

typedef struct context {
    uint32_t eip;
    uint32_t esp;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t esi;
    uint32_t edi;
    uint32_t ebp;
} context_t;

typedef int pid_t;

typedef struct task {
    trapframe_t         *tf;
    context_t           ctxt;
    uint8_t             *kstack;
    pid_t               pid;
    process_status_t    status;
    uint8_t             ticks; 
    vmm_t               *mm;
    list_elem_t         all_tag;
} task_t;

#define current_thread  ({              \
    uintptr_t   ctask;                  \
    asm("mov %%esp, %0": "=g" (ctask)); \
    (task_t*)(ctask & THREADMASK);      \
})

#define proc_cur_prio(_task) ((_task)->status.current_priority)

#define proc_state(_task)    ((_task)->status.process_state)

#define proc_name(_task)    ((_task)->status.attributes.name)

#define proc_entry(_task)   ((_task)->status.attributes.entry_point)

#define proc_base_prio(_task)   ((_task)->status.attributes.base_priority)

#define proc_stack_size(_task)  ((_task)->status.attributes.stack_size)

#define proc_deadline(_task)    ((_task)->status.attributes.deadline)

#define proc_time_capa(_task)   ((_task)->status.attributes.time_capacity)

#define proc_period(_task)  ((_task)->status.attributes.period)

#define DEFAULT_TIME_CAPA   5 

#define DEFAULT_THREAD_ATTR(func, name) \
{\
    0,\
    DEFAULT_TIME_CAPA,\
    (void*)(func),\
    (void*)NULL,\
    KSTACKSIZE,\
    1,\
    0,\
    name\
}

#define DEFAULT_THREAD_ATTR_ARG(func, argv, name) \
{\
    0,\
    DEFAULT_TIME_CAPA,\
    (void*)(func),\
    (void*)(argv),\
    KSTACKSIZE,\
    1,\
    0,\
    name\
}




void process_init(void);


#endif