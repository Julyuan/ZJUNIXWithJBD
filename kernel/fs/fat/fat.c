#include "fat.h"
#include <driver/vga.h>
#include <zjunix/log.h>
#include "utils.h"

#ifdef FS_DEBUG
#include <intr.h>
#include <zjunix/log.h>
#include "debug.h"
#endif  // ! FS_DEBUG

/* fat buffer clock head */
u32 fat_clock_head = 0;
BUF_512 fat_buf[FAT_BUF_NUM];

u8 filename11[13];
u8 new_alloc_empty[PAGE_SIZE];

#define DIR_DATA_BUF_NUM 4
BUF_512 dir_data_buf[DIR_DATA_BUF_NUM];
u32 dir_data_clock_head = 0;

// 文件系统的journal
extern journal_t* journal;
u32 init_fat_info() {
    u8 meta_buf[512];
    /* Init bufs */
    kernel_memset(meta_buf, 0, sizeof(meta_buf));
    kernel_memset(&fat_info, 0, sizeof(struct fs_info));

    /* Get MBR sector */
    if (read_block(meta_buf, 0, 1) == 1) {
        goto init_fat_info_err;
    }
    log(LOG_OK, "Get MBR sector info");
    /* MBR partition 1 entry starts from +446, and LBA starts from +8 */
    fat_info.base_addr = get_u32(meta_buf + 446 + 8);

    /* Get FAT BPB */
    if (read_block(fat_info.BPB.data, fat_info.base_addr, 1) == 1)
        goto init_fat_info_err;
    log(LOG_OK, "Get FAT BPB");
#ifdef FS_DEBUG
    dump_bpb_info(&(fat_info.BPB.attr));
#endif

    /* Sector size (MBR[11]) must be SECTOR_SIZE bytes */
    if (fat_info.BPB.attr.sector_size != SECTOR_SIZE) {
        log(LOG_FAIL, "FAT32 Sector size must be %d bytes, but get %d bytes.", SECTOR_SIZE, fat_info.BPB.attr.sector_size);
        goto init_fat_info_err;
    }

    /* Determine FAT type */
    /* For FAT32, max root dir entries must be 0 */
    if (fat_info.BPB.attr.max_root_dir_entries != 0) {
        goto init_fat_info_err;
    }
    /* For FAT32, sectors per fat at BPB[0x16] is 0 */
    if (fat_info.BPB.attr.sectors_per_fat != 0) {
        goto init_fat_info_err;
    }
    /* For FAT32, total sectors at BPB[0x16] is 0 */
    if (fat_info.BPB.attr.num_of_small_sectors != 0) {
        goto init_fat_info_err;
    }
    /* If not FAT32, goto error state */
    u32 total_sectors = fat_info.BPB.attr.num_of_sectors;
    u32 reserved_sectors = fat_info.BPB.attr.reserved_sectors;
    u32 sectors_per_fat = fat_info.BPB.attr.num_of_sectors_per_fat;
    u32 total_data_sectors = total_sectors - reserved_sectors - sectors_per_fat * 2;
    u8 sectors_per_cluster = fat_info.BPB.attr.sectors_per_cluster;
    fat_info.total_data_clusters = total_data_sectors / sectors_per_cluster;
    if (fat_info.total_data_clusters < 65525) {
        goto init_fat_info_err;
    }
    /* Get root dir stctor */
    fat_info.first_data_sector = reserved_sectors + sectors_per_fat * 2;
    log(LOG_OK, "Partition type determined: FAT32");

    /* Keep FSInfo in buf */
    read_block(fat_info.fat_fs_info, 1 + fat_info.base_addr, 1);
    log(LOG_OK, "Get FSInfo sector");
    init_journal_info();
#ifdef FS_DEBUG
    dump_fat_info(&(fat_info));
#endif

    /* Init success */
    return 0;

init_fat_info_err:
    return 1;
}

void init_fat_buf() {
    int i = 0;
    for (i = 0; i < FAT_BUF_NUM; i++) {
        fat_buf[i].cur = 0xffffffff;
        fat_buf[i].state = 0;

        // 以下是我们根据日志操作新增的初始化操作
        fat_buf[i].h_signal_bit = 0;
        fat_buf[i].handle = NULL;
    }
}

void init_dir_buf() {
    int i = 0;
    for (i = 0; i < DIR_DATA_BUF_NUM; i++) {
        dir_data_buf[i].cur = 0xffffffff;
        dir_data_buf[i].state = 0;
    }
}

/* FAT Initialize */
u32 init_fs() {
    u32 succ = init_fat_info();
    if (0 != succ)
        goto fs_init_err;
    init_fat_buf();
    init_dir_buf();
    
    return 0;

fs_init_err:
    log(LOG_FAIL, "File system init fail.");
    return 1;
}

/* Write current fat sector */
u32 write_fat_sector(u32 index) {
    if ((fat_buf[index].cur != 0xffffffff) && (((fat_buf[index].state) & 0x02) != 0)) {
        /* Write FAT and FAT copy */
        if (write_block(fat_buf[index].buf, fat_buf[index].cur, 1) == 1)
            goto write_fat_sector_err;
        if (write_block(fat_buf[index].buf, fat_info.BPB.attr.num_of_sectors_per_fat + fat_buf[index].cur, 1) == 1)
            goto write_fat_sector_err;
        fat_buf[index].state &= 0x01;
    }
    return 0;
write_fat_sector_err:
    return 1;
}

/* Read fat sector */
u32 read_fat_sector(u32 ThisFATSecNum, handle_t* handle) {
    u32 index;
    /* try to find in buffer */
    for (index = 0; (index < FAT_BUF_NUM) && (fat_buf[index].cur != ThisFATSecNum); index++)
        ;

    /* if not in buffer, find victim & replace, otherwise set reference bit */
    if (index == FAT_BUF_NUM) {
        index = fs_victim_512(fat_buf, &fat_clock_head, FAT_BUF_NUM);
        
                // 日志处理
        // 处理buffer_head代码
        // 在这里首先判断handle是否为空
        // 有两个要考虑的问题，分别是handle
        // 的绑定和transaction的提交
        if(handle != NULL){
            // if(fat_buf[index].handle!=NULL){

            // }
            // else{
            //     fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
            //     fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
            //     fat_buf[index].handle = handle;
            //     fat_buf[index].h_signal_bit = 1;
            // }
            switch(fat_buf[index].h_signal_bit){
                // 代表的是最初的情况，在这里我们直接对handle赋值
                case STATE_ZERO: 
                    fat_buf[index].handle = handle;
                    fat_buf[index].h_signal_bit = STATE_ONE;
                    fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
                    fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
                    fat_buf[index].handle->bh->b_size = BUFFER_HEAD_SECTOR;
                break;
                // 代表transaction还在running
                // 没有被提交的情况
                case STATE_ONE: 
                    if(fat_buf[index].handle==handle){

                    }
                    else{
                        printk("STATE ONE ERROR!\n");
                        while(1){

                        }
                    }
                break;

                case STATE_TWO:     //这里要做write的操作，我们直接提交，又因为该handle要写回
                                    //我们还要调用erase_handle
                    journal_commit_transactions(fat_buf[index].handle->h_transaction->t_journal, fat_buf[index].handle->h_transaction);
                    journal_erase_handle(fat_buf[index].handle->h_transaction,fat_buf[index].handle);
                    fat_buf[index].handle = handle;
                    fat_buf[index].h_signal_bit = STATE_ONE;
                    fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
                    fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
                                        fat_buf[index].handle->bh->b_size = BUFFER_HEAD_SECTOR;

                break;
                // 代表handle所在的transaction被提交了
                // 但是handle没有被checkpoint的情况
                case STATE_THREE: 
                    journal_erase_handle(fat_buf[index].handle->h_transaction,fat_buf[index].handle);
                    fat_buf[index].handle = handle;
                    fat_buf[index].h_signal_bit = STATE_ONE;
                    fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
                    fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
                    fat_buf[index].handle->bh->b_size = BUFFER_HEAD_SECTOR;

                break;
                // 代表handle所在的transaction被提交了
                // handle有被checkpoint的情况
                case STATE_FOUR:    
                    fat_buf[index].handle = handle;
                    fat_buf[index].h_signal_bit = STATE_ONE;
                    fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
                    fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
                    fat_buf[index].handle->bh->b_size = BUFFER_HEAD_SECTOR;
                break;
                default: break;
            }
        }
        else{
            // do nothing
        }
        // 这里的写index对应的块必定会调用到transaction的commit
        if (write_fat_sector(index) == 1)
            goto read_fat_sector_err;

        if (read_block(fat_buf[index].buf, ThisFATSecNum, 1) == 1)
            goto read_fat_sector_err;

        fat_buf[index].cur = ThisFATSecNum;
        fat_buf[index].state = 1;
        

    } else{
        fat_buf[index].state |= 0x01;
        // 虽然这个index在内存里，但是在我们的设计中也存在我们要提交transaction的可能
        if(handle != NULL){
            if(fat_buf[index].handle==handle){

            }
            else{
                switch(fat_buf[index].h_signal_bit){
                    // 代表的是最初的情况
                    case STATE_ZERO:                    
                        fat_buf[index].handle = handle;
                        fat_buf[index].h_signal_bit = STATE_ONE;
                        fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
                        fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
                    break;
                    // 代表transaction还在running
                    // 没有被提交的情况
                    case STATE_ONE:
                        if(fat_buf[index].handle==handle){

                        }
                        else{
                            printk("STATE ONE ERROR!\n");
                            while(1){

                            }
                        }
                    break;
                    // 代表transaction的running状态结束
                    // 没有被提交的情况
                    case STATE_TWO: 
                        journal_commit_transactions(fat_buf[index].handle->h_transaction->t_journal, fat_buf[index].handle->h_transaction);
                        fat_buf[index].handle = handle;
                        fat_buf[index].h_signal_bit = STATE_ONE;
                        fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
                        fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
                        fat_buf[index].handle->bh->b_size = BUFFER_HEAD_SECTOR;

                    break;
                    // 代表handle所在的transaction被提交了
                    // 但是handle没有被checkpoint的情况
                    case STATE_THREE: 
                        fat_buf[index].handle = handle;
                        fat_buf[index].h_signal_bit = STATE_ONE;
                        fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
                        fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
                        fat_buf[index].handle->bh->b_size = BUFFER_HEAD_SECTOR;

                    break;
                    // 代表handle所在的transaction被提交了
                    // handle有被checkpoint的情况
                    case STATE_FOUR:    
                        fat_buf[index].handle = handle;
                        fat_buf[index].h_signal_bit = STATE_ONE;
                        fat_buf[index].handle->bh->b_blocknr = fat_buf[index].cur;
                        fat_buf[index].handle->bh->b_page1 = &(fat_buf[index]);
                        fat_buf[index].handle->bh->b_size = BUFFER_HEAD_SECTOR;
                    break;
                    default: break;
                }
            }
        }else{
            // do nothing
        }
    }
    return index;
read_fat_sector_err:
    return 0xffffffff;
}

/* path convertion */
u32 fs_next_slash(u8 *f) {
    u32 i, j, k;
    u8 chr11[13];
    for (i = 0; (*(f + i) != 0) && (*(f + i) != '/'); i++)
        ;

    for (j = 0; j < 12; j++) {
        chr11[j] = 0;
        filename11[j] = 0x20;
    }
    for (j = 0; j < 12 && j < i; j++) {
        chr11[j] = *(f + j);
        if (chr11[j] >= 'a' && chr11[j] <= 'z')
            chr11[j] = (u8)(chr11[j] - 'a' + 'A');
    }
    chr11[12] = 0;

    for (j = 0; (chr11[j] != 0) && (j < 12); j++) {
        if (chr11[j] == '.')
            break;

        filename11[j] = chr11[j];
    }

    if (chr11[j] == '.') {
        j++;
        for (k = 8; (chr11[j] != 0) && (j < 12) && (k < 11); j++, k++) {
            filename11[k] = chr11[j];
        }
    }

    filename11[11] = 0;

    return i;
}

/* strcmp */
u32 fs_cmp_filename(const u8 *f1, const u8 *f2) {
    u32 i;
    for (i = 0; i < 11; i++) {
        if (f1[i] != f2[i])
            return 1;
    }

    return 0;
}

/* Find a file, only absolute path with starting '/' accepted */
u32 fs_find(FILE *file) {
    u8 *f = file->path;
    u32 next_slash;
    u32 i, k;
    u32 next_clus;
    u32 index;
    u32 sec;

    if (*(f++) != '/')
        goto fs_find_err;

    index = fs_read_512(dir_data_buf, fs_dataclus2sec(2), &dir_data_clock_head, DIR_DATA_BUF_NUM);
    /* Open root directory */
    if (index == 0xffffffff)
        goto fs_find_err;

    /* Find directory entry */
    while (1) {
        file->dir_entry_pos = 0xFFFFFFFF;

        next_slash = fs_next_slash(f);

        while (1) {
            for (sec = 1; sec <= fat_info.BPB.attr.sectors_per_cluster; sec++) {
                /* Find directory entry in current cluster */
                for (i = 0; i < 512; i += 32) {
                    if (*(dir_data_buf[index].buf + i) == 0)
                        goto after_fs_find;

                    /* Ignore long path */
                    if ((fs_cmp_filename(dir_data_buf[index].buf + i, filename11) == 0) &&
                        ((*(dir_data_buf[index].buf + i + 11) & 0x08) == 0)) {
                        file->dir_entry_pos = i;
                        // refer to the issue in fs_close()
                        file->dir_entry_sector = dir_data_buf[index].cur - fat_info.base_addr;

                        for (k = 0; k < 32; k++)
                            file->entry.data[k] = *(dir_data_buf[index].buf + i + k);

                        goto after_fs_find;
                    }
                }
                /* next sector in current cluster */
                if (sec < fat_info.BPB.attr.sectors_per_cluster) {
                    index = fs_read_512(dir_data_buf, dir_data_buf[index].cur + 1, &dir_data_clock_head, DIR_DATA_BUF_NUM);
                    if (index == 0xffffffff)
                        goto fs_find_err;
                } else {
                    /* Read next cluster of current directory */
                    if (get_fat_entry_value(dir_data_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1, &next_clus) == 1)
                        goto fs_find_err;

                    if (next_clus <= fat_info.total_data_clusters + 1) {
                        index = fs_read_512(dir_data_buf, fs_dataclus2sec(next_clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
                        if (index == 0xffffffff)
                            goto fs_find_err;
                    } else
                        goto after_fs_find;
                }
            }
        }

    after_fs_find:
        /* If not found */
        if (file->dir_entry_pos == 0xFFFFFFFF)
            goto fs_find_ok;

        /* If path parsing completes */
        if (f[next_slash] == 0)
            goto fs_find_ok;

        /* If not a sub directory */
        if ((file->entry.data[11] & 0x10) == 0)
            goto fs_find_err;

        f += next_slash + 1;

        /* Open sub directory, high word(+20), low word(+26) */
        next_clus = get_start_cluster(file);

        if (next_clus <= fat_info.total_data_clusters + 1) {
            index = fs_read_512(dir_data_buf, fs_dataclus2sec(next_clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
            if (index == 0xffffffff)
                goto fs_find_err;
        } else
            goto fs_find_err;
    }
fs_find_ok:
    return 0;
fs_find_err:
    return 1;
}

/* Open: just do initializing & fs_find */
u32 fs_open(FILE *file, u8 *filename) {
    u32 i;

    /* Local buffer initialize */
    for (i = 0; i < LOCAL_DATA_BUF_NUM; i++) {
        file->data_buf[i].cur = 0xffffffff;
        file->data_buf[i].state = 0;
        
        // 以下是我们根据日志系统设计新增变量的初始化
        file->data_buf[i].h_signal_bit = 0;
        file->data_buf[i].handle = NULL;
    }

    file->clock_head = 0;

    for (i = 0; i < 256; i++)
        file->path[i] = 0;
    for (i = 0; i < 256 && filename[i] != 0; i++)
        file->path[i] = filename[i];

    file->loc = 0;

    if (fs_find(file) == 1)
        goto fs_open_err;

    /* If file not exists */
    if (file->dir_entry_pos == 0xFFFFFFFF)
        goto fs_open_err;

    return 0;
fs_open_err:
    return 1;
}
/* fflush, write global buffers to sd */
u32 fs_fflush() {
    u32 i;

    // FSInfo shoud add base_addr
    if (write_block(fat_info.fat_fs_info, 1 + fat_info.base_addr, 1) == 1)
        goto fs_fflush_err;

    if (write_block(fat_info.fat_fs_info, 7 + fat_info.base_addr, 1) == 1)
        goto fs_fflush_err;

    for (i = 0; i < FAT_BUF_NUM; i++)
        if (write_fat_sector(i) == 1)
            goto fs_fflush_err;

    for (i = 0; i < DIR_DATA_BUF_NUM; i++)
        if (fs_write_512(dir_data_buf + i) == 1)
            goto fs_fflush_err;

    return 0;

fs_fflush_err:
    return 1;
}

/* Close: write all buf in memory to SD */
u32 fs_close(FILE *file) {
    u32 i;
    u32 index;

    /* Write directory entry */
    index = fs_read_512(dir_data_buf, file->dir_entry_sector, &dir_data_clock_head, DIR_DATA_BUF_NUM);
    if (index == 0xffffffff)
        goto fs_close_err;

    dir_data_buf[index].state = 3;

    // Issue: need file->dir_entry to be local partition offset
    for (i = 0; i < 32; i++)
        *(dir_data_buf[index].buf + file->dir_entry_pos + i) = file->entry.data[i];
    /* do fflush to write global buffers */
    if (fs_fflush() == 1)
        goto fs_close_err;
    /* write local data buffer */
    for (i = 0; i < LOCAL_DATA_BUF_NUM; i++)
        if (fs_write_4k(file->data_buf + i) == 1)
            goto fs_close_err;

    return 0;
fs_close_err:
    return 1;
}

/* Read from file */
u32 fs_read(FILE *file, u8 *buf, u32 count) {
    u32 start_clus, start_byte;
    u32 end_clus, end_byte;
    u32 filesize = file->entry.attr.size;
    u32 clus = get_start_cluster(file);
    u32 next_clus;
    u32 i;
    u32 cc;
    u32 index;

#ifdef FS_DEBUG
    kernel_printf("fs_read: count %d\n", count);
    disable_interrupts();
#endif  // ! FS_DEBUG
    /* If file is empty */
    if (clus == 0)
        return 0;

    /* If loc + count > filesize, only up to EOF will be read */
    if (file->loc + count > filesize)
        count = filesize - file->loc;

    /* If read 0 byte */
    if (count == 0)
        return 0;

    start_clus = file->loc >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
    start_byte = file->loc & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);
    end_clus = (file->loc + count - 1) >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
    end_byte = (file->loc + count - 1) & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);

#ifdef FS_DEBUG
    kernel_printf("start cluster: %d\n", start_clus);
    kernel_printf("start byte: %d\n", start_byte);
    kernel_printf("end cluster: %d\n", end_clus);
    kernel_printf("end byte: %d\n", end_byte);
#endif  // ! FS_DEBUG
    /* Open first cluster to read */
    for (i = 0; i < start_clus; i++) {
        if (get_fat_entry_value(clus, &next_clus) == 1)
            goto fs_read_err;

        clus = next_clus;
    }

    cc = 0;
    while (start_clus <= end_clus) {
        index = fs_read_4k(file->data_buf, fs_dataclus2sec(clus), &(file->clock_head), LOCAL_DATA_BUF_NUM);
        if (index == 0xffffffff)
            goto fs_read_err;

        /* If in same cluster, just read */
        if (start_clus == end_clus) {
            for (i = start_byte; i <= end_byte; i++)
                buf[cc++] = file->data_buf[index].buf[i];
            goto fs_read_end;
        }
        /* otherwise, read clusters one by one */
        else {
            for (i = start_byte; i < (fat_info.BPB.attr.sectors_per_cluster << 9); i++)
                buf[cc++] = file->data_buf[index].buf[i];

            start_clus++;
            start_byte = 0;

            if (get_fat_entry_value(clus, &next_clus) == 1)
                goto fs_read_err;

            clus = next_clus;
        }
    }
fs_read_end:

#ifdef FS_DEBUG
    kernel_printf("fs_read: count %d\n", count);
    enable_interrupts();
#endif  // ! FS_DEBUG
    /* modify file pointer */
    file->loc += count;
    return cc;
fs_read_err:
    return 0xFFFFFFFF;
}

/* Find a free data cluster */
u32 fs_next_free(u32 start, u32 *next_free) {
    u32 clus;
    u32 ClusEntryVal;

    *next_free = 0xFFFFFFFF;

    for (clus = start; clus <= fat_info.total_data_clusters + 1; clus++) {
        if (get_fat_entry_value(clus, &ClusEntryVal) == 1)
            goto fs_next_free_err;

        if (ClusEntryVal == 0) {
            *next_free = clus;
            break;
        }
    }

    return 0;
fs_next_free_err:
    return 1;
}

/* Alloc a new free data cluster */
u32 fs_alloc(u32 *new_alloc, handle_t* handle) {
    printk("fs_alloc 1\n");
    u32 clus;
    u32 next_free;

    clus = get_u32(fat_info.fat_fs_info + 492) + 1;
    printk("fs_alloc 2\n");

    /* If FSI_Nxt_Free is illegal (> FSI_Free_Count), find a free data cluster
     * from beginning */
    if (clus > get_u32(fat_info.fat_fs_info + 488) + 1) {
        if (fs_next_free(2, &clus) == 1)
            goto fs_alloc_err;
        printk("fs_alloc 3\n");

        if (fs_modify_fat(clus, 0xFFFFFFFF, handle) == 1)
            goto fs_alloc_err;
        printk("fs_alloc 4\n");

    }

    /* FAT allocated and update FSI_Nxt_Free */
    if (fs_modify_fat(clus, 0xFFFFFFFF, handle) == 1)
        goto fs_alloc_err;

    printk("fs_alloc 5\n");

    if (fs_next_free(clus, &next_free) == 1)
        goto fs_alloc_err;

    printk("fs_alloc 6\n");

    /* no available free cluster */
    if (next_free > fat_info.total_data_clusters + 1)
        goto fs_alloc_err;

    set_u32(fat_info.fat_fs_info + 492, next_free - 1);

    *new_alloc = clus;

    u32 i;

    printk("clus: %d, sectors_per_cluster: %d\n",clus, fat_info.BPB.attr.sectors_per_cluster);
    /* Erase new allocated cluster */
    if (write_block(new_alloc_empty, fs_dataclus2sec(clus), fat_info.BPB.attr.sectors_per_cluster) == 1)
        goto fs_alloc_err;
    printk("fs_alloc 7\n");

    return 0;
fs_alloc_err:
    return 1;
}

/* Write to file */
// 这个是文件系统写操作的函数，我们的日志系统对于每一个写操作，都用一个transaction来表示
u32 fs_write(FILE *file, const u8 *buf, u32 count) {
    printk("fs_write is called\n");
    printk("journal magic number: %d\n",journal->j_superblock->s_header.h_magic);
    /* If write 0 bytes */
    if (count == 0) {
        return 0;
    }

    printk("begin calculate\n");
    u32 start_clus = file->loc >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
    u32 start_byte = file->loc & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);
    u32 end_clus = (file->loc + count - 1) >> fs_wa(fat_info.BPB.attr.sectors_per_cluster << 9);
    u32 end_byte = (file->loc + count - 1) & ((fat_info.BPB.attr.sectors_per_cluster << 9) - 1);
    
    /* If file is empty, alloc a new data cluster */
    u32 curr_cluster = get_start_cluster(file);

    printk("curr_cluster:   %d\n",curr_cluster);
    if (curr_cluster == 0) {
        printk("go into file empty case\n");
        // 新建一个transaction并分配空间
        transaction_t* transaction = (transaction_t*)kmalloc(sizeof(transaction_t));
        // 创建两个handle并分配空间
        handle_t* handle1, *handle2;
        handle1 = (handle_t*)kmalloc(sizeof(handle_t));
        handle2 = (handle_t*)kmalloc(sizeof(handle_t));
        // 给handle的struct buffer head分配空间
        handle1->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));
        handle2->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));

        // 使用指针将两个handle相连
        handle1->h_tnext = handle2;
        handle1->h_tprev = NULL;
        handle2->h_tprev = NULL;
        handle2->h_tprev = handle1;

        // checkpoint的list暂时不用
        handle1->h_cpnext = NULL;
        handle1->h_cpprev = NULL;
        handle2->h_cpnext = NULL;
        handle2->h_cpprev = NULL;

        // 设置handle1、handle2指向的transaction
        handle1->h_transaction = transaction;
        handle2->h_transaction = transaction;
        // 设置transaction的handle个数
        transaction->t_handle_count = 2;
        transaction->t_block_num = 1 + 8;
        // 设置transaction的第一个handle
        transaction->t_buffers = handle1;
        // 设置transaction的journal
        transaction->t_journal = journal;
        // 设置transaction的序列号
        transaction->t_tid = journal->j_transaction_sequence;
        transaction->t_state = T_RUNNING;
        journal->j_transaction_sequence++;

        // 将该transaction加到journal的running transaction列表中去
        transaction->t_tnext = journal->j_running_transaction;
        transaction->t_tprev = NULL;
        if(journal->j_running_transaction!=NULL)
            journal->j_running_transaction->t_tprev = transaction;
        journal->j_running_transaction = transaction;
        printk("fs_write 1\n");
    
        if (fs_alloc(&curr_cluster, handle1) == 1) {
            goto fs_write_err;
        }
        printk("fs_write 2\n");

        file->entry.attr.starthi = (u16)(((curr_cluster >> 16) & 0xFFFF));
        file->entry.attr.startlow = (u16)((curr_cluster & 0xFFFF));

        if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(curr_cluster), handle2) == 1)
            goto fs_write_err;
        printk("fs_write 3\n");

        // transaction执行完毕，将该transaction从running队列转移到commit队列
        transaction->t_state = T_COMMIT;

        // 修改handle对应的BUF的状态
        handle1->bh->b_page1->h_signal_bit = STATE_TWO;
        handle2->bh->b_page->h_signal_bit = STATE_TWO;

        if(journal->j_running_transaction==transaction){
            if(transaction->t_tnext==NULL){
                journal->j_running_transaction = NULL;
            }
            else{
                journal->j_running_transaction = journal->j_running_transaction->t_tnext;
                journal->j_running_transaction->t_tprev = NULL;
            }
        }
        else{
            if(transaction->t_tnext==NULL){
                transaction->t_tprev->t_tnext = NULL;
            }
            else{
                transaction->t_tprev->t_tnext = transaction->t_tnext;
                transaction->t_tnext->t_tprev = transaction->t_tprev;
            }
        }
        if(journal->j_committing_transaction==NULL){
            journal->j_committing_transaction=transaction;
            transaction->t_tnext = NULL;
            transaction->t_tprev = NULL;
        }
        else if(journal->j_committing_transaction!=NULL){
            journal->j_committing_transaction->t_tprev = transaction;
            transaction->t_tnext = journal->j_committing_transaction;
            transaction->t_tprev = NULL;
        }
    }
    
    /* Open first cluster to read */
    // 这里的操作是找到那个开始的cluster，存在要
    u32 next_cluster;
    for (u32 i = 0; i < start_clus; i++) {
        if (get_fat_entry_value(curr_cluster, &next_cluster) == 1)
            goto fs_write_err;

        /* If this is the last cluster in file, and still need to open next
         * cluster, just alloc a new data cluster */
        if (next_cluster > fat_info.total_data_clusters + 1) {

            // 因为这里要更改两个fat32的cluster项，所以我们在这里创建三个handle
            transaction_t* transaction1 = (transaction_t*)kmalloc(sizeof(transaction_t));

            handle_t* handle3, *handle4, *handle5;  

            // 将该transaction连接到j_running_transaction的队列里去
            transaction1->t_tnext = journal->j_running_transaction; 
            transaction1->t_tprev = NULL;
            if(journal->j_running_transaction!=NULL){
                journal->j_running_transaction->t_tprev = transaction1->t_tnext;
            }
            journal->j_running_transaction = transaction1;

            // 给该transaction相关的handle分配空间
            handle3 = (handle_t*)kmalloc(sizeof(handle_t));
            handle4 = (handle_t*)kmalloc(sizeof(handle_t));
            handle5 = (handle_t*)kmalloc(sizeof(handle_t));
            // 给handle下的buffer_head分配空间
            handle3->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));
            handle4->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));
            handle5->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));

            transaction1->t_buffers = handle3;
            transaction1->t_handle_count = 3;
            transaction1->t_block_num = 1 + 1 + 8;
            transaction1->t_journal = journal;
            transaction1->t_tid = journal->j_transaction_sequence;
            journal->j_transaction_sequence++;

            // 将handle相互链接起来
            handle3->h_tnext = handle4;
            handle4->h_tnext = handle5;
            handle4->h_tprev = handle3;
            handle5->h_tprev = handle4;
            handle5->h_tnext = NULL;
            handle3->h_tprev = NULL;

            // checkpoint的list暂时不用
            handle3->h_cpnext = NULL;
            handle3->h_cpprev = NULL;
            handle4->h_cpnext = NULL;
            handle4->h_cpprev = NULL;
            handle5->h_cpnext = NULL;
            handle5->h_cpprev = NULL;

            // 设置handle的transaction
            handle3->h_transaction = transaction1;
            handle4->h_transaction = transaction1;
            handle5->h_transaction = transaction1;

            // 设置transaction的状态
            transaction1->t_state = T_RUNNING;


            // 
            if (fs_alloc(&next_cluster,handle3) == 1)
                goto fs_write_err;

            if (fs_modify_fat(curr_cluster, next_cluster, handle4) == 1)
                goto fs_write_err;

            if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(next_cluster), handle5) == 1)
                goto fs_write_err;



            // transaction执行完毕，将该transaction从running队列转移到commit队列
            transaction1->t_state = T_COMMIT;
            
            handle3->bh->b_page1->h_signal_bit = STATE_TWO;
            handle4->bh->b_page1->h_signal_bit = STATE_TWO;
            handle5->bh->b_page->h_signal_bit = STATE_TWO;
            if(journal->j_running_transaction==transaction1){
                if(transaction1->t_tnext==NULL){
                    journal->j_running_transaction = NULL;
                }
                else{
                    journal->j_running_transaction = journal->j_running_transaction->t_tnext;
                    journal->j_running_transaction->t_tprev = NULL;
                }
            }
            else{
                if(transaction1->t_tnext==NULL){
                    transaction1->t_tprev->t_tnext = NULL;
                }
                else{
                    transaction1->t_tprev->t_tnext = transaction1->t_tnext;
                    transaction1->t_tnext->t_tprev = transaction1->t_tprev;
                }
            }
            if(journal->j_committing_transaction==NULL){
                journal->j_committing_transaction=transaction1;
                transaction1->t_tnext = NULL;
                transaction1->t_tprev = NULL;
            }
            else if(journal->j_committing_transaction!=NULL){
                journal->j_committing_transaction->t_tprev = transaction1;
                transaction1->t_tnext = journal->j_committing_transaction;
                transaction1->t_tprev = NULL;
            } 
        }

        curr_cluster = next_cluster;
    }

    u32 cc = 0;
    u32 index = 0;
    u32 flag = 0;
    // flag表示块的来源是已有的还是新出现的

    transaction_t* transaction2;
    handle_t* handle6;
    transaction_t *transaction3;
    handle_t *handle7, *handle8, *handle9;

    // 在这一层的while里，读入只会影响到一个cluster，所以我们在这里处理一个transaction
    // 如果是不需要新增块的。我们就处理transaction2，里面只有一个文件数据的handle
    // 如果是需要新增块的。我们就处理transaction3，里面有三个ahndle，其中有两个和fat表相关
    while (start_clus <= end_clus) {
        // 读入第一个文件的cluster
        index = fs_read_4k(file->data_buf, fs_dataclus2sec(curr_cluster), &(file->clock_head), LOCAL_DATA_BUF_NUM);
        if (index == 0xffffffff)
            goto fs_write_err;

        file->data_buf[index].state = 3;
        
        // flag等于0代表不用分配新的块的情况
        if(flag==0){
            // 这里是更改原来的buffer，所以只需要一个handle就可以了
            // 给transaction及其相关的指针变量分配空间
            transaction2 = (transaction_t*)kmalloc(sizeof(transaction_t));
            handle6 = (handle_t*)kmalloc(sizeof(handle_t));
            handle6->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));

            // 设置transaction的t_tid
            transaction2->t_tid = journal->j_transaction_sequence;
            journal->j_transaction_sequence++;

            // 
            transaction2->t_handle_count = 1;
            transaction2->t_block_num = 8;

            // 建立transaction和journal的链接
            if(journal->j_running_transaction!=NULL){
                // 判断队列是否为空
                journal->j_running_transaction->t_tprev = transaction2;
            }
            transaction2->t_tnext = journal->j_running_transaction;
            transaction2->t_tprev = NULL;
            journal->j_running_transaction = transaction2;

            handle6->h_tnext = NULL;
            handle6->h_tprev = NULL;
            handle6->h_cpnext = NULL;
            handle6->h_cpprev = NULL;

            file->data_buf[index].handle = handle6;
            file->data_buf[index].h_signal_bit = STATE_ONE;
            handle6->bh->b_page = &(file->data_buf[index]);
            handle6->bh->b_blocknr = file->data_buf[index].cur;
        }
        /* If in same cluster, just write */
        if (start_clus == end_clus) {
            // 文件的写入缓存操作
            for (u32 i = start_byte; i <= end_byte; i++)
                file->data_buf[index].buf[i] = buf[cc++];
            goto fs_write_end;

            if(flag==0){
                // transaction2执行完毕，将该transaction从running队列转移到commit队列
                // transaction2提交
                transaction2->t_state = T_COMMIT;
                handle6->bh->b_page->h_signal_bit = STATE_TWO;
                if(journal->j_running_transaction==transaction2){
                    if(transaction2->t_tnext==NULL){
                        journal->j_running_transaction = NULL;
                    }
                    else{
                        journal->j_running_transaction = journal->j_running_transaction->t_tnext;
                        journal->j_running_transaction->t_tprev = NULL;
                    }
                }
                else{
                    if(transaction2->t_tnext==NULL){
                        transaction2->t_tprev->t_tnext = NULL;
                    }
                    else{
                        transaction2->t_tprev->t_tnext = transaction2->t_tnext;
                        transaction2->t_tnext->t_tprev = transaction2->t_tprev;
                    }
                }
                if(journal->j_committing_transaction==NULL){
                    journal->j_committing_transaction=transaction2;
                    transaction2->t_tnext = NULL;
                    transaction2->t_tprev = NULL;
                }
                else if(journal->j_committing_transaction!=NULL){
                    journal->j_committing_transaction->t_tprev = transaction2;
                    transaction2->t_tnext = journal->j_committing_transaction;
                    transaction2->t_tprev = NULL;
                }
            }else{
                // 提交transaction3
                transaction3->t_state = T_COMMIT;
                handle7->bh->b_page1->h_signal_bit = STATE_TWO;
                handle8->bh->b_page1->h_signal_bit = STATE_TWO;        
                handle9->bh->b_page->h_signal_bit  = STATE_TWO;
                if(journal->j_running_transaction==transaction3){
                    if(transaction3->t_tnext==NULL){
                        journal->j_running_transaction = NULL;
                    }
                    else{
                        journal->j_running_transaction = journal->j_running_transaction->t_tnext;
                        journal->j_running_transaction->t_tprev = NULL;
                    }
                }
                else{
                    if(transaction3->t_tnext==NULL){
                        transaction3->t_tprev->t_tnext = NULL;
                    }
                    else{
                        transaction3->t_tprev->t_tnext = transaction3->t_tnext;
                        transaction3->t_tnext->t_tprev = transaction3->t_tprev;
                    }
                }
                if(journal->j_committing_transaction==NULL){
                    journal->j_committing_transaction=transaction3;
                    transaction3->t_tnext = NULL;
                    transaction3->t_tprev = NULL;
                }
                else if(journal->j_committing_transaction!=NULL){
                    journal->j_committing_transaction->t_tprev = transaction3;
                    transaction3->t_tnext = journal->j_committing_transaction;
                    transaction3->t_tprev = NULL;
                } 
            }
        }
        /* otherwise, write clusters one by one */
        else {
            // 文件的写入缓存操作
            for (u32 i = start_byte; i < (fat_info.BPB.attr.sectors_per_cluster << 9); i++)
                file->data_buf[index].buf[i] = buf[cc++];

            if(flag==0){
                
                // transaction2执行完毕，将该transaction从running队列转移到commit队列
                transaction2->t_state = T_COMMIT;
                if(journal->j_running_transaction==transaction2){
                    if(transaction2->t_tnext==NULL){
                        journal->j_running_transaction = NULL;
                    }
                    else{
                        journal->j_running_transaction = journal->j_running_transaction->t_tnext;
                        journal->j_running_transaction->t_tprev = NULL;
                    }
                }
                else{
                    if(transaction2->t_tnext==NULL){
                        transaction2->t_tprev->t_tnext = NULL;
                    }
                    else{
                        transaction2->t_tprev->t_tnext = transaction2->t_tnext;
                        transaction2->t_tnext->t_tprev = transaction2->t_tprev;
                    }
                }
                if(journal->j_committing_transaction==NULL){
                    journal->j_committing_transaction=transaction2;
                    transaction2->t_tnext = NULL;
                    transaction2->t_tprev = NULL;
                }
                else if(journal->j_committing_transaction!=NULL){
                    journal->j_committing_transaction->t_tprev = transaction2;
                    transaction2->t_tnext = journal->j_committing_transaction;
                    transaction2->t_tprev = NULL;
                } 
            }
            else{
                transaction3->t_state = T_COMMIT;
                if(journal->j_running_transaction==transaction3){
                    if(transaction3->t_tnext==NULL){
                        journal->j_running_transaction = NULL;
                    }
                    else{
                        journal->j_running_transaction = journal->j_running_transaction->t_tnext;
                        journal->j_running_transaction->t_tprev = NULL;
                    }
                }
                else{
                    if(transaction3->t_tnext==NULL){
                        transaction3->t_tprev->t_tnext = NULL;
                    }
                    else{
                        transaction3->t_tprev->t_tnext = transaction3->t_tnext;
                        transaction3->t_tnext->t_tprev = transaction3->t_tprev;
                    }
                }
                if(journal->j_committing_transaction==NULL){
                    journal->j_committing_transaction=transaction3;
                    transaction3->t_tnext = NULL;
                    transaction3->t_tprev = NULL;
                }
                else if(journal->j_committing_transaction!=NULL){
                    journal->j_committing_transaction->t_tprev = transaction3;
                    transaction3->t_tnext = journal->j_committing_transaction;
                    transaction3->t_tprev = NULL;
                } 
            }
            start_clus++;
            start_byte = 0;

            if (get_fat_entry_value(curr_cluster, &next_cluster) == 1)
                goto fs_write_err;

            /* If this is the last cluster in file, and still need to open next
             * cluster, just alloc a new data cluster */
            if (next_cluster > fat_info.total_data_clusters + 1) {
                // 初始化相应的transaction和handle
                transaction3 = (transaction_t*)kmalloc(sizeof(transaction_t));
                transaction3->t_buffers = handle7;
                transaction3->t_handle_count = 3;
                transaction3->t_block_num = 1 + 1 + 8;
                transaction3->t_journal = journal;
                transaction3->t_tid = journal->j_commit_sequence;
                journal->j_commit_sequence++;

                // 将transaction3和journal相连
                transaction3->t_tprev = NULL;
                transaction3->t_tnext = journal->j_running_transaction;
                if(journal->j_running_transaction!=NULL)
                    journal->j_running_transaction->t_tprev = transaction3;
                journal->j_running_transaction = transaction3;

                // 给transaction3相关的handle分配空间
                handle7 = (handle_t*)kmalloc(sizeof(handle_t));
                handle8 = (handle_t*)kmalloc(sizeof(handle_t));
                handle9 = (handle_t*)kmalloc(sizeof(handle_t));
                handle7->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));
                handle8->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));
                handle9->bh = (struct buffer_head*)kmalloc(sizeof(struct buffer_head));

                // 设置handle之间的链接
                handle7->h_tnext = handle8;
                handle8->h_tnext = handle9;
                handle9->h_tnext = NULL;
                handle9->h_tprev = handle8;
                handle8->h_tprev = handle7;
                handle7->h_tprev = NULL;

                // checkpoint的list暂时不用
                handle7->h_cpnext = NULL;
                handle7->h_cpprev = NULL;
                handle8->h_cpnext = NULL;
                handle8->h_cpprev = NULL;
                handle9->h_cpnext = NULL;
                handle9->h_cpprev = NULL;

                if (fs_alloc(&next_cluster, handle7) == 1)
                    goto fs_write_err;

                if (fs_modify_fat(curr_cluster, next_cluster, handle8) == 1)
                    goto fs_write_err;

                if (fs_clr_4k(file->data_buf, &(file->clock_head), LOCAL_DATA_BUF_NUM, fs_dataclus2sec(next_cluster), handle9) == 1)
                    goto fs_write_err;

                // 我们初始化transaction3之后不能马上进行提交，因为此时
                // transaction3的数据只是分配了，它要在下一层的while循环
                // 中才会被写入，然后我们才能提交相关的transaction

                // 设置flag，改变提交
                flag=1;
            }else{
                // 设置flag，改变提交
                flag=0;
            }

            curr_cluster = next_cluster;
        }
    }

fs_write_end:

    /* update file size */
    if (file->loc + count > file->entry.attr.size)
        file->entry.attr.size = file->loc + count;

    /* update location */
    file->loc += count;
   // while(1){}
    return cc;
fs_write_err:
   // while(1){}
    return 0xFFFFFFFF;
}

/* lseek */
void fs_lseek(FILE *file, u32 new_loc) {
    u32 filesize = file->entry.attr.size;

    if (new_loc < filesize)
        file->loc = new_loc;
    else
        file->loc = filesize;
}

/* find an empty directory entry */
u32 fs_find_empty_entry(u32 *empty_entry, u32 index) {
    u32 i;
    u32 next_clus;
    u32 sec;

    while (1) {
        for (sec = 1; sec <= fat_info.BPB.attr.sectors_per_cluster; sec++) {
            /* Find directory entry in current cluster */
            for (i = 0; i < 512; i += 32) {
                /* If entry is empty */
                if ((*(dir_data_buf[index].buf + i) == 0) || (*(dir_data_buf[index].buf + i) == 0xE5)) {
                    *empty_entry = i;
                    goto after_fs_find_empty_entry;
                }
            }

            if (sec < fat_info.BPB.attr.sectors_per_cluster) {
                index = fs_read_512(dir_data_buf, dir_data_buf[index].cur + sec, &dir_data_clock_head, DIR_DATA_BUF_NUM);
                if (index == 0xffffffff)
                    goto fs_find_empty_entry_err;
            } else {
                /* Read next cluster of current directory */
                if (get_fat_entry_value(dir_data_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1, &next_clus) == 1)
                    goto fs_find_empty_entry_err;

                /* need to alloc a new cluster */
                if (next_clus > fat_info.total_data_clusters + 1) {
                    if (fs_alloc(&next_clus,NULL) == 1)
                        goto fs_find_empty_entry_err;

                    if (fs_modify_fat(fs_sec2dataclus(dir_data_buf[index].cur - fat_info.BPB.attr.sectors_per_cluster + 1), next_clus,NULL) == 1)
                        goto fs_find_empty_entry_err;

                    *empty_entry = 0;

                    if (fs_clr_512(dir_data_buf, &dir_data_clock_head, DIR_DATA_BUF_NUM, fs_dataclus2sec(next_clus)) == 1)
                        goto fs_find_empty_entry_err;
                }

                index = fs_read_512(dir_data_buf, fs_dataclus2sec(next_clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
                if (index == 0xffffffff)
                    goto fs_find_empty_entry_err;
            }
        }
    }

after_fs_find_empty_entry:
    return index;
fs_find_empty_entry_err:
    return 0xffffffff;
}

/* create an empty file with attr */
u32 fs_create_with_attr(u8 *filename, u8 attr) {
    printk("fs create 1\n");
    u32 i;
    u32 l1 = 0;
    u32 l2 = 0;
    u32 empty_entry;
    u32 clus;
    u32 index;
    FILE file_creat;
    /* If file exists */
    if (fs_open(&file_creat, filename) == 0)
        goto fs_creat_err;
    printk("fs create 2\n");

    for (i = 255; i >= 0; i--)
        if (file_creat.path[i] != 0) {
            l2 = i;
            break;
        }

    for (i = 255; i >= 0; i--)
        if (file_creat.path[i] == '/') {
            l1 = i;
            break;
        }

    /* If not root directory, find that directory */
    if (l1 != 0) {
        for (i = l1; i <= l2; i++)
            file_creat.path[i] = 0;

        if (fs_find(&file_creat) == 1)
            goto fs_creat_err;
        printk("fs create 3\n");

        /* If path not found */
        if (file_creat.dir_entry_pos == 0xFFFFFFFF)
            goto fs_creat_err;

        clus = get_start_cluster(&file_creat);
        /* Open that directory */
        index = fs_read_512(dir_data_buf, fs_dataclus2sec(clus), &dir_data_clock_head, DIR_DATA_BUF_NUM);
        if (index == 0xffffffff)
            goto fs_creat_err;
        printk("fs create 4\n");

        file_creat.dir_entry_pos = clus;
    }
    /* otherwise, open root directory */
    else {
        printk("fs create 5\n");

        index = fs_read_512(dir_data_buf, fs_dataclus2sec(2), &dir_data_clock_head, DIR_DATA_BUF_NUM);
        if (index == 0xffffffff)
            goto fs_creat_err;
        printk("fs create 6\n");

        file_creat.dir_entry_pos = 2;
    }
    printk("fs create 7\n");

    /* find an empty entry */
    index = fs_find_empty_entry(&empty_entry, index);
    if (index == 0xffffffff)
        goto fs_creat_err;

    for (i = l1 + 1; i <= l2; i++)
        file_creat.path[i - l1 - 1] = filename[i];

    printk("fs create 8\n");

    file_creat.path[l2 - l1] = 0;
    fs_next_slash(file_creat.path);

    dir_data_buf[index].state = 3;

    /* write path */
    for (i = 0; i < 11; i++)
        *(dir_data_buf[index].buf + empty_entry + i) = filename11[i];

    /* write file attr */
    *(dir_data_buf[index].buf + empty_entry + 11) = attr;

    /* other should be zero */
    for (i = 12; i < 32; i++)
        *(dir_data_buf[index].buf + empty_entry + i) = 0;

    if (fs_fflush() == 1)
        goto fs_creat_err;

    return 0;
fs_creat_err:
    return 1;
}

u32 fs_create(u8 *filename) {
    return fs_create_with_attr(filename, 0x20);
}

void get_filename(u8 *entry, u8 *buf) {
    u32 i;
    u32 l1 = 0, l2 = 8;

    for (i = 0; i < 11; i++)
        buf[i] = entry[i];

    if (buf[0] == '.') {
        if (buf[1] == '.')
            buf[2] = 0;
        else
            buf[1] = 0;
    } else {
        for (i = 0; i < 8; i++)
            if (buf[i] == 0x20) {
                buf[i] = '.';
                l1 = i;
                break;
            }

        if (i == 8) {
            for (i = 11; i > 8; i--)
                buf[i] = buf[i - 1];

            buf[8] = '.';
            l1 = 8;
            l2 = 9;
        }

        for (i = l1 + 1; i < l1 + 4; i++) {
            if (buf[l2 + i - l1 - 1] != 0x20)
                buf[i] = buf[l2 + i - l1 - 1];
            else
                break;
        }

        buf[i] = 0;

        if (buf[i - 1] == '.')
            buf[i - 1] = 0;
    }
}
