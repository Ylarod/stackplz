#ifndef __STACKPLZ_CONSTS_H__
#define __STACKPLZ_CONSTS_H__

// stackplz => 737461636b706c7a
#define MAGIC_UID 0x73746163
#define MAGIC_PID 0x6b706c7a
#define MAGIC_TID 0x61636b70

#define TASK_COMM_LEN 16
#define MAX_COUNT 20
#define MAX_WATCH_PROC_COUNT 256
#define MAX_PATH_COMPONENTS   48

// clang-format off
#define MAX_PERCPU_BUFSIZE (1 << 15)  // set by the kernel as an upper bound
#define PATH_MAX    4096
#define MAX_STRING_SIZE    4096       // same as PATH_MAX
#define MAX_BYTES_ARR_SIZE    4096       // same as PATH_MAX
#define MAX_BUF_READ_SIZE    4096
#define ARGS_BUF_SIZE       32000

enum buf_idx_e
{
    STRING_BUF_IDX,
    ZERO_BUF_IDX,
    MAX_BUFFERS
};


#endif // __STACKPLZ_CONSTS_H__