#include <driver/vga.h>
#include <zjunix/log.h>
#include <zjunix/slab.h>
#include "fat.h"
#include "utils.h"

u8 mk_dir_buf[32];
FILE file_create;

/* remove directory entry */
u32 fs_rm(u8 *filename) {
    u32 clus;
    u32 next_clus;
    FILE mk_dir;

    if (fs_open(&mk_dir, filename) == 1)
        goto fs_rm_err;

    /* Mark 0xE5 */
    mk_dir.entry.data[0] = 0xE5;

    /* Release all allocated block */
    clus = get_start_cluster(&mk_dir);

    while (clus != 0 && clus <= fat_info.total_data_clusters + 1) {
        if (get_fat_entry_value(clus, &next_clus) == 1)
            goto fs_rm_err;

        if (fs_modify_fat(clus, 0, NULL) == 1)
            goto fs_rm_err;

        clus = next_clus;
    }

    if (fs_close(&mk_dir) == 1)
        goto fs_rm_err;

    return 0;
fs_rm_err:
    return 1;
}

/* move directory entry */
u32 fs_mv(u8 *src, u8 *dest) {
    u32 i;
    FILE mk_dir;
    u8 filename11[13];

    /* if src not exists */
    if (fs_open(&mk_dir, src) == 1)
        goto fs_mv_err;

    /* create dest */
    if (fs_create_with_attr(dest, mk_dir.entry.data[11]) == 1)
        goto fs_mv_err;

    /* copy directory entry */
    for (i = 0; i < 32; i++)
        mk_dir_buf[i] = mk_dir.entry.data[i];

    /* new path */
    for (i = 0; i < 11; i++)
        mk_dir_buf[i] = filename11[i];

    if (fs_open(&file_create, dest) == 1)
        goto fs_mv_err;

    /* copy directory entry to dest */
    for (i = 0; i < 32; i++)
        file_create.entry.data[i] = mk_dir_buf[i];

    if (fs_close(&file_create) == 1)
        goto fs_mv_err;

    /* mark src directory entry 0xE5 */
    mk_dir.entry.data[0] = 0xE5;

    if (fs_close(&mk_dir) == 1)
        goto fs_mv_err;

    return 0;
fs_mv_err:
    return 1;
}

/* mkdir, create a new file and write . and .. */
u32 fs_mkdir(u8 *filename) {
    u32 i;
    FILE mk_dir;
    FILE file_creat;
    kernel_printf("mkdir debug 1\n");
    if (fs_create_with_attr(filename, 0x30) == 1)
        goto fs_mkdir_err;
    kernel_printf("mkdir debug 2\n");

    if (fs_open(&mk_dir, filename) == 1)
        goto fs_mkdir_err;
    kernel_printf("mkdir debug 3\n");

    mk_dir_buf[0] = '.';
    for (i = 1; i < 11; i++)
        mk_dir_buf[i] = 0x20;

    mk_dir_buf[11] = 0x30;
    for (i = 12; i < 32; i++)
        mk_dir_buf[i] = 0;

    if (fs_write(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;
    kernel_printf("mkdir debug 4\n");

    fs_lseek(&mk_dir, 0);
    kernel_printf("mkdir debug 5\n");

    mk_dir_buf[20] = mk_dir.entry.data[20];
    mk_dir_buf[21] = mk_dir.entry.data[21];
    mk_dir_buf[26] = mk_dir.entry.data[26];
    mk_dir_buf[27] = mk_dir.entry.data[27];

    if (fs_write(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;
    kernel_printf("mkdir debug 6\n");

    mk_dir_buf[0] = '.';
    mk_dir_buf[1] = '.';

    for (i = 2; i < 11; i++)
        mk_dir_buf[i] = 0x20;

    mk_dir_buf[11] = 0x30;
    for (i = 12; i < 32; i++)
        mk_dir_buf[i] = 0;

    set_u16(mk_dir_buf + 20, (file_creat.dir_entry_pos >> 16) & 0xFFFF);
    set_u16(mk_dir_buf + 26, file_creat.dir_entry_pos & 0xFFFF);

    if (fs_write(&mk_dir, mk_dir_buf, 32) == 1)
        goto fs_mkdir_err;
    kernel_printf("mkdir debug 7\n");

    for (i = 28; i < 32; i++)
        mk_dir.entry.data[i] = 0;

    if (fs_close(&mk_dir) == 1)
        goto fs_mkdir_err;
    kernel_printf("mkdir debug 8\n");

    return 0;
fs_mkdir_err:
    return 1;
}

u32 fs_rmdir(u8 *filename){
    u32 clus;
    u32 next_clus;
    FILE rm_dir;

    /* if dir does not exist */
    if(fs_open(&rm_dir, filename)==1)
        goto fs_rmdir_err;

    /* Mark 0xE5 */
    rm_dir.entry.data[0] = 0xE5;

    /* Release all allocated block */ 
    clus = get_start_cluster(&rm_dir);

    while(clus != 0 && clus <= fat_info.total_data_clusters + 1){

        if(get_fat_entry_value(clus, &next_clus) == 1)
            goto fs_rmdir_err;

        if(fs_modify_fat(clus, 0, NULL) == 1)
            goto fs_rmdir_err;

        clus = next_clus;
    }

    if(fs_close(&rm_dir) == 1)
        goto fs_rmdir_err;
    return 0;
fs_rmdir_err:
    return 1;
}
u32 fs_cat(u8 *path) {
    u8 filename[12];
    FILE cat_file;

    /* Open */
    if (0 != fs_open(&cat_file, path)) {
        log(LOG_FAIL, "File %s open failed", path);
        return 1;
    }

    /* Read */
    u32 file_size = get_entry_filesize(cat_file.entry.data);
    u8 *buf = (u8 *)kmalloc(file_size + 1);
    fs_read(&cat_file, buf, file_size);
    buf[file_size] = 0;
    kernel_printf("%s\n", buf);
    fs_close(&cat_file);
    kfree(buf);
    return 0;
}