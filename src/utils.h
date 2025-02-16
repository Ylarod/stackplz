#ifndef STACKPLZ_UTILS_H
#define STACKPLZ_UTILS_H

#include "vmlinux_510.h"

#include "bpf_helpers.h"
#include "bpf_core_read.h"
#include "bpf_tracing.h"

#include "common/consts.h"
#include "common/buffer.h"

#define MAX_POINT_ARG_COUNT 10
#define READ_INDEX_SKIP 100
#define READ_INDEX_REG 101

typedef struct point_arg_t {
    u32 read_flag;
    u32 read_index;
    u32 alias_type;
    u32 type;
    u32 size;
	u32 item_persize;
	u32 item_countindex;
	u32 read_offset;
} point_arg;

static __always_inline u32 save_bytes_with_len(program_data_t p, u64 ptr, u32 read_len, u32 next_arg_index) {
    if (read_len > MAX_BUF_READ_SIZE) {
        read_len = MAX_BUF_READ_SIZE;
    }
    int status = save_bytes_to_buf(p.event, (void *)(ptr & 0xffffffffff), read_len, next_arg_index);
    if (status == 0) {
        buf_t *zero_p = get_buf(ZERO_BUF_IDX);
        if (zero_p == NULL) {
            return next_arg_index;
        }
        save_bytes_to_buf(p.event, &zero_p->buf[0], read_len, next_arg_index);
    }
    next_arg_index += 1;
    return next_arg_index;
}

static __always_inline u32 read_ptr_arg(program_data_t p, struct point_arg_t* point_arg, u64 ptr, u32 read_count, u32 next_arg_index) {
    // 这些都是常规的 指针 + 结构体 按照读取结构体的方式读取即可
    if (point_arg->alias_type == TYPE_PTHREAD_ATTR) {
        // 结构体类型 直接读取对应大小的数据 具体转换交给前端
        u32 struct_size = MAX_BYTES_ARR_SIZE;
        if (point_arg->size <= struct_size) {
            struct_size = point_arg->size;
        }
        // 修复 MTE 读取可能不正常的情况
        int status = save_bytes_to_buf(p.event, (void *)(ptr & 0xffffffffff), struct_size, next_arg_index);
        if (status == 0) {
            // 保存失败的情况 比如 ptr 是一个非法的地址 ...
            buf_t *zero_p = get_buf(ZERO_BUF_IDX);
            if (zero_p == NULL) {
                return next_arg_index;
            }
            // 这个时候填充一个全0的内容进去 不然前端不好解析
            save_bytes_to_buf(p.event, &zero_p->buf[0], struct_size, next_arg_index);
            next_arg_index += 1;
        } else {
            next_arg_index += 1;
        }
        return next_arg_index;
    }
    // 比较复杂的 指针 + 结构体
    if (point_arg->alias_type == TYPE_IOVEC) {
        struct iovec iovec_ptr;
        int errno = bpf_probe_read_user(&iovec_ptr, sizeof(iovec_ptr), (void*) ptr);
        if (errno == 0) {
            save_to_submit_buf(p.event, (void *)&iovec_ptr, sizeof(iovec_ptr), next_arg_index);
            next_arg_index += 1;
            // 目前这样只是读取了第一个 iov 实际上要多次读取 数量是 iovcnt
            // 但是注意多个缓冲区并不是连续的
            u64 iov_base = (u64)iovec_ptr.iov_base;
            u32 iov_len = (u64)iovec_ptr.iov_len;
            // u32 read_len = read_count * iov_len;
            u32 read_len = iov_len;
            if (read_len > MAX_BUF_READ_SIZE) {
                read_len = MAX_BUF_READ_SIZE;
            }
            next_arg_index = save_bytes_with_len(p, iov_base, read_len, next_arg_index);
            return next_arg_index;
        }
    }
    return next_arg_index;
}

static __always_inline u32 read_arg(program_data_t p, struct point_arg_t* point_arg, u64 ptr, u32 read_count, u32 next_arg_index) {
    ptr = ptr + point_arg->read_offset;
    if (point_arg->type == TYPE_NONE) {
        return next_arg_index;
    }
    if (point_arg->type == TYPE_NUM) {
        // 这种具体类型转换交给前端做
        return next_arg_index;
    }
    if (ptr == 0) {
        return next_arg_index;
    }
    if (point_arg->type == TYPE_STRING) {
        u32 buf_off = 0;
        buf_t *string_p = get_buf(STRING_BUF_IDX);
        if (string_p == NULL) {
            return next_arg_index;
        }
        int status = bpf_probe_read_user(&string_p->buf[buf_off], MAX_STRING_SIZE, (void *)ptr);
        if (status < 0) {
            // MTE 其实也正常读取到了
            bpf_probe_read_user_str(&string_p->buf[buf_off], MAX_STRING_SIZE, (void *)ptr);
        }
        save_str_to_buf(p.event, &string_p->buf[buf_off], next_arg_index);
        next_arg_index += 1;
        return next_arg_index;
    }
    if (point_arg->type == TYPE_STRING_ARR) {
        save_str_arr_to_buf(p.event, (const char *const *) ptr /*ptr*/, next_arg_index);
        next_arg_index += 1;
        return next_arg_index;
    }
    if (point_arg->type == TYPE_STRUCT) {
        // 结构体类型 直接读取对应大小的数据 具体转换交给前端
        u32 struct_size = MAX_BYTES_ARR_SIZE;
        if (point_arg->size <= struct_size) {
            struct_size = point_arg->size;
        }
        // 修复 MTE 读取可能不正常的情况
        int status = save_bytes_to_buf(p.event, (void *)(ptr & 0xffffffffff), struct_size, next_arg_index);
        if (status == 0) {
            // 保存失败的情况 比如 ptr 是一个非法的地址 ...
            buf_t *zero_p = get_buf(ZERO_BUF_IDX);
            if (zero_p == NULL) {
                return next_arg_index;
            }
            // 这个时候填充一个全0的内容进去 不然前端不好解析
            save_bytes_to_buf(p.event, &zero_p->buf[0], struct_size, next_arg_index);
            next_arg_index += 1;
        } else {
            next_arg_index += 1;
        }
        return next_arg_index;
    }
    // 这是像 write 这样的函数中的 buf 参数 直接读取对应长度的数据即可
    if (point_arg->alias_type == TYPE_BUFFER_T) {
        // buffer 的单个元素长度就是 1 所以这里就是 read_count
        // u32 read_len = read_count * 1;
        u32 read_len = read_count * point_arg->item_persize;
        int status = save_bytes_to_buf(p.event, (void *)(ptr & 0xffffffffff), read_len, next_arg_index);
        if (status == 0) {
            buf_t *zero_p = get_buf(ZERO_BUF_IDX);
            if (zero_p == NULL) {
                return next_arg_index;
            }
            save_bytes_to_buf(p.event, &zero_p->buf[0], read_len, next_arg_index);
            next_arg_index += 1;
        } else {
            next_arg_index += 1;
        }
        return next_arg_index;
    }
    if (point_arg->type == TYPE_POINTER) {
        // 指针类型 通常读一下对应指针的数据即可 后续记得考虑兼容下32位
        // 读取指针所指向位置的值 并且保存
        u64 addr = 0;
        bpf_probe_read_user(&addr, sizeof(addr), (void*) ptr);
        save_to_submit_buf(p.event, (void *) &addr, sizeof(u64), next_arg_index);
        next_arg_index += 1;
        // 如果指向的是一个结构体 那么我们就再进一步把结构体数据读取出来
        if (addr == 0) {
            return next_arg_index;
        }
        next_arg_index = read_ptr_arg(p, point_arg, addr, read_count, next_arg_index);
        return next_arg_index;
    }
    return next_arg_index;
}

char __license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 0xFFFFFFFE;

#endif
