#ifndef __MAPS_H__
#define __MAPS_H__

#include "vmlinux_510.h"
#include "bpf/bpf_helpers.h"
#include "types.h"

#define BPF_MAP(_name, _type, _key_type, _value_type, _max_entries)                                \
    struct {                                                                                       \
        __uint(type, _type);                                                                       \
        __uint(max_entries, _max_entries);                                                         \
        __type(key, _key_type);                                                                    \
        __type(value, _value_type);                                                                \
    } _name SEC(".maps");

#define BPF_HASH(_name, _key_type, _value_type, _max_entries)                                      \
    BPF_MAP(_name, BPF_MAP_TYPE_HASH, _key_type, _value_type, _max_entries)

#define BPF_LRU_HASH(_name, _key_type, _value_type, _max_entries)                                  \
    BPF_MAP(_name, BPF_MAP_TYPE_LRU_HASH, _key_type, _value_type, _max_entries)

#define BPF_PERCPU_ARRAY(_name, _value_type, _max_entries)                                         \
    BPF_MAP(_name, BPF_MAP_TYPE_PERCPU_ARRAY, u32, _value_type, _max_entries)

#define BPF_PERF_OUTPUT(_name, _max_entries)                                                       \
    BPF_MAP(_name, BPF_MAP_TYPE_PERF_EVENT_ARRAY, int, __u32, _max_entries)

#define BPF_ARRAY(_name, _value_type, _max_entries)                                                \
    BPF_MAP(_name, BPF_MAP_TYPE_ARRAY, u32, _value_type, _max_entries)

BPF_PERCPU_ARRAY(bufs, buf_t, MAX_BUFFERS);                        // percpu global buffer variables
BPF_PERF_OUTPUT(events, 1024);      // events submission
BPF_HASH(args_map, u64, args_t, 1024);                             // persist args between function entry and return
BPF_HASH(child_parent_map, u32, u32, 512);
BPF_HASH(common_filter, u32, common_filter_t, 1);
BPF_HASH(sys_whitelist, u32, u32, 40);
BPF_HASH(sys_blacklist, u32, u32, 40);
BPF_HASH(thread_filter, thread_name_t, u32, 40);
BPF_HASH(rev_filter, rev_string_t, u32, 40);
BPF_PERCPU_ARRAY(event_data_map, event_data_t, 1);
BPF_ARRAY(config_map, config_entry_t, 1);

#endif /* __MAPS_H__ */