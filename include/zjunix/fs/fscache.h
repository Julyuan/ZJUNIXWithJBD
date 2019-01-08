#ifndef _ZJUNIX_FSCACHE_H
#define _ZJUNIX_FSCACHE_H

#include <zjunix/type.h>
#include <zjunix/fs/jbd.h>

typedef struct handle_s handle_t;

// 这个宏定义用来指示h_signal_bit的值
#define STATE_ZERO  0
#define STATE_ONE   1
#define STATE_TWO   2
#define STATE_THREE 3
#define STATE_FOUR  4

/* 4k byte buffer */
typedef struct buf_4k {
    unsigned char buf[4096];
    unsigned long cur;  //  这里的cur表示这个buf在SD卡中的地址
    unsigned long state;
    // 用于日志系统的结构
    handle_t* handle;
    unsigned long h_signal_bit;
} BUF_4K;

/* 512 byte buffer */
typedef struct buf_512 {
    unsigned char buf[512];
    unsigned long cur;

    unsigned long state;
    // 用于日志系统的结构
    handle_t* handle;
    unsigned long h_signal_bit;
} BUF_512;

u32 fs_victim_4k(BUF_4K *buf, u32 *clock_head, u32 size, handle_t*handle);
u32 fs_write_4k(BUF_4K *f);
u32 fs_read_4k(BUF_4K *f, u32 FirstSectorOfCluster, u32 *clock_head, u32 size);
u32 fs_clr_4k(BUF_4K *buf, u32 *clock_head, u32 size, u32 cur, handle_t* handle);

u32 fs_victim_512(BUF_512 *buf, u32 *clock_head, u32 size);
u32 fs_write_512(BUF_512 *f);
u32 fs_read_512(BUF_512 *f, u32 FirstSectorOfCluster, u32 *clock_head, u32 size);
u32 fs_clr_512(BUF_512 *buf, u32 *clock_head, u32 size, u32 cur);


#endif // ! _ZJUNIX_FSCACHE_H