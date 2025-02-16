#include "utils.h"
#include <stdbool.h>

#include "types.h"
#include "common/arguments.h"
#include "common/common.h"
#include "common/consts.h"
#include "common/context.h"
#include "common/filtering.h"

// syscall过滤配置
struct syscall_filter_t {
    u32 is_32bit;
    u32 syscall_all;
    u32 whitelist_mode;
    u32 blacklist_mode;
};

typedef struct syscall_point_args_t {
    // u32 nr;
    u32 count;
    point_arg point_args[MAX_POINT_ARG_COUNT];
    point_arg point_arg_ret;
} syscall_point_args;

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, u32);
    __type(value, u32);
    __uint(max_entries, MAX_WATCH_PROC_COUNT);
} watch_proc_map SEC(".maps");

// syscall_point_args_map 的 key 就是 nr
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, u32);
    __type(value, struct syscall_point_args_t);
    __uint(max_entries, 512);
} syscall_point_args_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, u32);
    __type(value, struct syscall_filter_t);
    __uint(max_entries, 1);
} syscall_filter SEC(".maps");

SEC("raw_tracepoint/sched_process_fork")
int tracepoint__sched__sched_process_fork(struct bpf_raw_tracepoint_args *ctx)
{
    long ret = 0;
    program_data_t p = {};
    if (!init_program_data(&p, ctx))
        return 0;

    struct task_struct *parent = (struct task_struct *) ctx->args[0];
    struct task_struct *child = (struct task_struct *) ctx->args[1];

    // 为了实现仅指定单个pid时 能追踪其产生的子进程的相关系统调用 设计如下
    // 维护一个 map
    // - 其 key 为进程 pid 
    // - 其 value 为其父进程 pid
    // 逻辑如下
    // 当进入此处后，先获取进程本身信息，然后通过自己的父进程 pid 去 map 中取出对应的value
    // 如果没有取到则说明这个进程不是要追踪的进程
    // 取到了，则说明这个是之前产生的进程，然后向map存入进程信息 key 就是进程本身 pid 而 value则是父进程pid
    // 那么最开始的 pid 从哪里来呢 答案是从首次通过 sys_enter 的过滤之后 向该map存放第一个key value
    // 1. child_parent_map => {}
    // 2. 出现第一个通过 sys_enter 处的过滤的进程，则更新map -> child_parent_map => {12345: 12345}
    // 3. sched_process_fork 获取进程的父进程信息，检查map，发现父进程存在其中，则更新map -> child_parent_map => {12345: 12345, 22222: 12345}
    // 4. sys_enter/sys_exit 有限次遍历 child_parent_map 取出key逐个比较当前进程的pid
    // 待实现...

    u32 parent_ns_pid = get_task_ns_pid(parent);
    u32 parent_ns_tgid = get_task_ns_tgid(parent);
    u32 child_ns_pid = get_task_ns_pid(child);
    u32 child_ns_tgid = get_task_ns_tgid(child);

    // bpf_printk("[syscall] parent_ns_pid:%d child_ns_pid:%d\n", parent_ns_pid, child_ns_pid);
    u32* pid = bpf_map_lookup_elem(&child_parent_map, &parent_ns_pid);
    if (pid == NULL) {
        return 0;
    }
    if (*pid == parent_ns_pid){
        // map中取出的父进程pid 这里fork产生子进程的pid相同
        // 说明这个进程是我们自己添加的
        // 那么现在把新产生的这个子进程 pid 放入 map
        ret = bpf_map_update_elem(&child_parent_map, &child_ns_pid, &parent_ns_pid, BPF_ANY);
        // bpf_printk("[syscall] parent_ns_pid:%d child_ns_pid:%d ret:%ld\n", parent_ns_pid, child_ns_pid, ret);
    } else {
        // 理论上不应该走到这个分支
        // 因为我们用当前函数这里的 parent 期望的就是其之前map中的
        bpf_printk("[syscall] parent pid from map:%d\n", *pid);
    }

    return 0;
}

SEC("raw_tracepoint/sys_enter")
int raw_syscalls_sys_enter(struct bpf_raw_tracepoint_args* ctx) {

    // 除了实现对指定进程的系统调用跟踪 也要将其产生的子进程 加入追踪范围
    // 为了实现这个目的 fork 系统调用结束之后 应当检查其 父进程是否归属于当前被追踪的进程

    program_data_t p = {};
    if (!init_program_data(&p, ctx))
        return 0;

    if (!should_trace(&p))
        return 0;

    struct pt_regs *regs = (struct pt_regs *)(ctx->args[0]);
    u64 syscallno = READ_KERN(regs->syscallno);
    u32 sysno = (u32)syscallno;
    // 先根据调用号确定有没有对应的参数获取方案 没有直接结束
    struct syscall_point_args_t* syscall_point_args = bpf_map_lookup_elem(&syscall_point_args_map, &sysno);
    if (syscall_point_args == NULL) {
        bpf_printk("[syscall] unsupport nr:%d\n", sysno);
        return 0;
    }

    u32 filter_key = 0;
    struct syscall_filter_t* filter = bpf_map_lookup_elem(&syscall_filter, &filter_key);
    if (filter == NULL) {
        return 0;
    }

    if (filter->syscall_all == 0) {
        // 非 追踪全部syscall模式
        if (filter->whitelist_mode == 1) {
            u32 *flag = bpf_map_lookup_elem(&sys_whitelist, &sysno);
            if (flag == NULL) {
                return 0;
            }
        }
    }

    // 黑名单同样对 追踪全部syscall模式 有效
    if (filter->blacklist_mode == 1) {
        u32 *flag = bpf_map_lookup_elem(&sys_blacklist, &sysno);
        if (flag != NULL) {
            return 0;
        }
    }

    // 保存寄存器应该放到所有过滤完成之后
    args_t args = {};
    args.args[0] = READ_KERN(regs->regs[0]);
    args.args[1] = READ_KERN(regs->regs[1]);
    args.args[2] = READ_KERN(regs->regs[2]);
    args.args[3] = READ_KERN(regs->regs[3]);
    args.args[4] = READ_KERN(regs->regs[4]);
    save_args(&args, SYSCALL_ENTER);

    // event->context 已经有进程的信息了
    save_to_submit_buf(p.event, (void *) &sysno, sizeof(u32), 0);

    // 先获取 lr sp pc 并发送 这样可以尽早计算调用来源情况
    // READ_KERN 好像有问题
    u64 lr = 0;
    if(filter->is_32bit) {
        bpf_probe_read_kernel(&lr, sizeof(lr), &regs->regs[14]);
        save_to_submit_buf(p.event, (void *) &lr, sizeof(u64), 1);
    }
    else {
        bpf_probe_read_kernel(&lr, sizeof(lr), &regs->regs[30]);
        save_to_submit_buf(p.event, (void *) &lr, sizeof(u64), 1);
    }
    u64 pc = 0;
    u64 sp = 0;
    bpf_probe_read_kernel(&pc, sizeof(pc), &regs->pc);
    bpf_probe_read_kernel(&sp, sizeof(sp), &regs->sp);
    save_to_submit_buf(p.event, (void *) &pc, sizeof(u64), 2);
    save_to_submit_buf(p.event, (void *) &sp, sizeof(u64), 3);

    u32 point_arg_count = MAX_POINT_ARG_COUNT;
    if (syscall_point_args->count <= point_arg_count) {
        point_arg_count = syscall_point_args->count;
    }

    int next_arg_index = 4;

    for (int i = 0; i < point_arg_count; i++) {
        struct point_arg_t* point_arg = (struct point_arg_t*) &syscall_point_args->point_args[i];

        u64 arg_ptr = 0;
        if (point_arg->read_index == READ_INDEX_SKIP) {
            continue;
        }
        if (point_arg->read_index == READ_INDEX_REG) {
            // 未来可能允许读取很多个参数...
            if (i > REG_ARM64_PC) {
                continue;
            }
            arg_ptr = READ_KERN(regs->regs[i]);
        } else if (point_arg->read_index > READ_INDEX_SKIP) {
            if (point_arg->read_index <= REG_ARM64_LR) {
                arg_ptr = READ_KERN(regs->regs[point_arg->read_index]);
            } else if (point_arg->read_index == REG_ARM64_SP) {
                arg_ptr = READ_KERN(regs->sp);
            } else if (point_arg->read_index == REG_ARM64_PC) {
                arg_ptr = READ_KERN(regs->pc);
            } else {
                continue;
            }
        } else {
            continue;
        }

        // 先保存参数值本身
        save_to_submit_buf(p.event, (void *)&arg_ptr, sizeof(u64), next_arg_index);
        next_arg_index += 1;

        if (point_arg->read_flag != SYS_ENTER) {
            continue;
        }
        if (arg_ptr == 0) {
            continue;
        }
        u32 read_count = MAX_BUF_READ_SIZE;
        if (point_arg->item_countindex != READ_INDEX_SKIP) {
            u32 item_count = 0;
            // 以寄存器值作为索引 只包含 x0-x28
            // x29 是fp寄存器 所以不包含在内
            if (point_arg->item_countindex < REG_ARM64_X29) {
                item_count = READ_KERN(regs->regs[point_arg->item_countindex]);
            }
            if (item_count != 0 && item_count <= read_count) {
                read_count = item_count;
            }
        } else if (point_arg->size <= read_count) {
            read_count = point_arg->size;
        }
        next_arg_index = read_arg(p, point_arg, arg_ptr, read_count, next_arg_index);
    }

    u32 out_size = sizeof(event_context_t) + p.event->buf_off;
    save_to_submit_buf(p.event, (void *) &out_size, sizeof(u32), next_arg_index);
    events_perf_submit(&p, SYSCALL_ENTER);
    common_filter_t* c_filter = bpf_map_lookup_elem(&common_filter, &filter_key);
    if (c_filter == NULL) {
        return 0;
    }
    if (c_filter->signal > 0) {
        bpf_send_signal(c_filter->signal);
    }
    return 0;
}

SEC("raw_tracepoint/sys_exit")
int raw_syscalls_sys_exit(struct bpf_raw_tracepoint_args* ctx) {

    program_data_t p = {};
    if (!init_program_data(&p, ctx))
        return 0;

    if (!should_trace(&p))
        return 0;

    struct pt_regs *regs = (struct pt_regs *)(ctx->args[0]);
    u64 syscallno = READ_KERN(regs->syscallno);
    u32 sysno = (u32)syscallno;

    struct syscall_point_args_t* syscall_point_args = bpf_map_lookup_elem(&syscall_point_args_map, &sysno);
    if (syscall_point_args == NULL) {
        return 0;
    }

    u32 filter_key = 0;
    struct syscall_filter_t* filter = bpf_map_lookup_elem(&syscall_filter, &filter_key);
    if (filter == NULL) {
        return 0;
    }

    args_t saved_args;
    if (load_args(&saved_args, SYSCALL_ENTER) != 0) {
        return 0;
    }
    del_args(SYSCALL_ENTER);

    if (filter->syscall_all == 0) {
        // 非 追踪全部syscall模式
        if (filter->whitelist_mode == 1) {
            u32 *flag = bpf_map_lookup_elem(&sys_whitelist, &sysno);
            if (flag == NULL) {
                return 0;
            }
        }
    }

    // 黑名单同样对 追踪全部syscall模式 有效
    if (filter->blacklist_mode == 1) {
        u32 *flag = bpf_map_lookup_elem(&sys_blacklist, &sysno);
        if (flag != NULL) {
            return 0;
        }
    }

    int next_arg_index = 0;
    save_to_submit_buf(p.event, (void *) &sysno, sizeof(u32), next_arg_index);
    next_arg_index += 1;

    u32 point_arg_count = MAX_POINT_ARG_COUNT;
    if (syscall_point_args->count <= point_arg_count) {
        point_arg_count = syscall_point_args->count;
    }

    for (int i = 0; i < point_arg_count; i++) {
        struct point_arg_t* point_arg = (struct point_arg_t*) &syscall_point_args->point_args[i];

        u64 arg_ptr = 0;
        if (point_arg->read_index == READ_INDEX_SKIP) {
            continue;
        }
        if (point_arg->read_index == READ_INDEX_REG) {
            // 未来可能允许读取很多个参数...
            if (i > REG_ARM64_LR) {
                continue;
            }
            // 考虑到这里是syscall执行结束 尽可能优先采用之前保存的寄存器的值
            if (i < REG_ARM64_X6) {
                arg_ptr = saved_args.args[i];
            } else {
                arg_ptr = READ_KERN(regs->regs[i]);
            }
        } else if (point_arg->read_index == REG_ARM64_SP) {
            arg_ptr = READ_KERN(regs->sp);
        } else if (point_arg->read_index == REG_ARM64_PC) {
            arg_ptr = READ_KERN(regs->pc);
        } else if (point_arg->read_index <= REG_ARM64_LR) {
            arg_ptr = READ_KERN(regs->regs[point_arg->read_index]);
        } else {
            continue;
        }

        // 先保存参数值本身
        save_to_submit_buf(p.event, (void *)&arg_ptr, sizeof(u64), next_arg_index);
        next_arg_index += 1;

        if (point_arg->read_flag != SYS_EXIT) {
            continue;
        }
        if (arg_ptr == 0) {
            continue;
        }
        u32 read_count = MAX_BUF_READ_SIZE;
        if (point_arg->item_countindex != READ_INDEX_SKIP) {
            u32 item_count = 0;
            // 以寄存器值作为索引 只包含 x0-x28
            // x29 是fp寄存器 所以不包含在内
            if (point_arg->item_countindex < REG_ARM64_X29) {
                item_count = READ_KERN(regs->regs[point_arg->item_countindex]);
            }
            if (item_count != 0 && item_count <= read_count) {
                read_count = item_count;
            }
        } else if (point_arg->size <= read_count) {
            read_count = point_arg->size;
        }
        next_arg_index = read_arg(p, point_arg, arg_ptr, read_count, next_arg_index);
    }

    // 读取返回值
    u64 ret = READ_KERN(regs->regs[0]);
    // 保存之
    save_to_submit_buf(p.event, (void *) &ret, sizeof(ret), next_arg_index);
    next_arg_index += 1;
    // 取返回值的参数配置 并尝试进一步读取
    struct point_arg_t* point_arg = (struct point_arg_t*) &syscall_point_args->point_arg_ret;
    next_arg_index = read_arg(p, point_arg, ret, 0, next_arg_index);
    u32 out_size = sizeof(event_context_t) + p.event->buf_off;
    save_to_submit_buf(p.event, (void *) &out_size, sizeof(u32), next_arg_index);
    // 发送数据
    events_perf_submit(&p, SYSCALL_EXIT);
    return 0;
}

// bpf_printk debug use
// echo 1 > /sys/kernel/tracing/tracing_on
// cat /sys/kernel/tracing/trace_pipe