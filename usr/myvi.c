#include "myvi.h"
#include <driver/ps2.h>
#include <driver/vga.h>
#include <zjunix/fs/fat.h>


// 操作系统shell里实现vi命令对应的源文件
extern int cursor_freq;
int pre_cursor_freq;
// file代表内存里的文件对象
FILE file;

// is_new_file判断是不是新文件
int is_new_file;

// 缓存区
char buffer[BUFFER_SIZE];
char instruction[COLUMN_LEN] = "";

// 文件名字的指针
char *filename;
int inst_len = 0;
int size = 0;
int cursor_location;
int page_location = 0;
int page_end;
int err;
int mode;

// 以上都是文件系统里的全局变量

// myvi的初始化函数，将相关变量设置成0
char myvi_init() {
    int i;
    size = 0;
    inst_len = 0;
    cursor_location = 0;
    page_location = 0;
    err = 0;
    mode = 0;
    page_end = 0;
    for (i = 0; i < BUFFER_SIZE; i++) {
        buffer[i] = 0;
    }
    return 0;
}

// 字母的大写转小写
char to_lower_case(char ch) {
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 'a';
    else
        return ch;
}

// 字符串拷贝
char *mystrcpy(char *dest, const char *src) {
    do {
        *(dest++) = *(src++);
    } while (*src);

    return dest;
}

// 加载文件函数，输入的参数是文件的路径
void load_file(char *file_path) {
    int file_size;  // 文件的大小
    int cnt = 0;
    unsigned char newch;    
    // 将该文件打开，并用全局变量file绑定
    unsigned int ret = fs_open(&file, file_path);   

    // 如果ret==1，代表文件不存在
    if (ret != 0) {
        is_new_file = 1;
        buffer[size++] = '\n';
        return;
    } else {
        is_new_file = 0;
    }

    // 得到文件的大小
    file_size = get_entry_filesize(file.entry.data);
    int i = 0;
    // 这里的操作是从原来的文件里逐个读入字符
    for (i = 0; i < file_size; i++) {
        fs_read(&file, &newch, 1);
        if (newch != 13) {
            buffer[size++] = (char)newch;
        }
        // 如果size超出了buffer_size，我们就报错
        if (size == BUFFER_SIZE - 1) {
            err = 2;
            return;
        }
    }
    // 设置结尾的字符串
    if (size == 0 || buffer[size - 1] != '\n') {
        buffer[size++] = '\n';
    }
    fs_close(&file);
}
// 保存文件
void save_file() {
    // 判断是否是新的文件，如果是新的就创建一个新的文件
    if (is_new_file) {
        fs_create(filename);
    }
    // 顺序调用文件打开函数、定位函数和写文件函数，因为一个文件
    // 原来的内容已经放在buffer里面了，所以定位的位置是0
    fs_open(&file, filename);
    fs_lseek(&file, 0);
    fs_write(&file, buffer, size);
    int ret = fs_close(&file);
}
// 这个函数是插入函数。因为在这里key是插入到buffer的中间
// 后面的内容都需要后移一格
void insert_key(char key, int site) {
    if (size >= BUFFER_SIZE) {
        err = 1;
        return;
    }

    int i = 0;
    for (i = size; i > site; i--)
        buffer[i] = buffer[i - 1];
    buffer[site] = key;
    size++;
}
// 删除，原理和之前的相同
void delete_key(int site) {
    int i = 0;
    for (i = site; i < size - 1; i++)
        buffer[i] = buffer[i + 1];
    size--;
}
// 将char显示在vga屏幕上
void put_char_on_screen(char ch, int row, int column, int color) {
    kernel_putchar_at(ch, color & 0xFFFF, (color >> 16) & 0xFFFF, row, column);
}

// 屏幕刷新函数，将整个屏幕的内容刷新
void screen_flush() {
    int row = 0, column = 0;
    int loc = page_location;
    int next_column, color;

    while (row < ROW_LEN && loc < size) {
        if (loc != cursor_location)
            color = COLOR_BLACK_WHITE;
        else
            color = COLOR_WHITE_BLACK;

        switch (buffer[loc]) {
            case KEYBOARD_ENTER_N:
                put_char_on_screen(KEYBOARD_SPACE, row, column, color);
                column++;
                color = COLOR_BLACK_WHITE;
                for (; column < COLUMN_LEN; column++)
                    put_char_on_screen(KEYBOARD_SPACE, row, column, color);
                row++;
                column = 0;
                break;
            case KEYBOARD_ENTER_R:
                break;
            case KEYBOARD_TAB:
                next_column = (column & 0xFFFFFFFC) + 4;
                for (; column < next_column; column++)
                    put_char_on_screen(KEYBOARD_SPACE, row, column, color);
                break;
            default:  // other ascii character
                put_char_on_screen(buffer[loc], row, column, color);
                column++;
        }

        if (column == COLUMN_LEN) {
            row++;
            column = 0;
        }

        loc++;
    }

    page_end = loc;

    if (loc == size && loc == cursor_location) {
        put_char_on_screen(KEYBOARD_SPACE, row, column, COLOR_WHITE_BLACK);
        column++;
        if (column == COLUMN_LEN) {
            row++;
            column = 0;
        }
    }

    if (row < ROW_LEN) {
        if (column != 0) {
            for (; column < COLUMN_LEN; column++)
                put_char_on_screen(KEYBOARD_SPACE, row, column, COLOR_BLACK_WHITE);
            row++;
            column = 0;
        }
        for (; row < ROW_LEN; row++) {
            put_char_on_screen('~', row, column, COLOR_BLACK_BLUE);
            column++;
            for (; column < COLUMN_LEN; column++)
                put_char_on_screen(KEYBOARD_SPACE, row, column, COLOR_BLACK_WHITE);
            column = 0;
        }
    }

    int i = 0;
    for (i = 0; i < COLUMN_LEN - 20; i++) {
        if (i < inst_len)
            put_char_on_screen(instruction[i], ROW_LEN, i, COLOR_GREEN_WHITE);
        else
            put_char_on_screen(' ', ROW_LEN, i, COLOR_GREEN_WHITE);
    }
}
// 获得字符的输入
char get_key() {
    return kernel_getchar();
}

// 定位到上一行
void page_location_last_line() {
    int loc = page_location;
    do {
        loc--;
    } while (loc > 0 && buffer[loc - 1] != '\n' && buffer[loc - 1] != '\r');
    if (loc >= 0)
        page_location = loc;
}

// 定位到下一行
void page_location_next_line() {
    int loc = page_location;
    while (loc < size && buffer[loc] != '\n')
        loc++;
    if (loc + 1 < size)
        page_location = loc + 1;
}

// 光标移动的函数
void cursor_prev_line() {
    int loc = cursor_location;
    int offset = 0;
    do {
        loc--;
        offset++;
    } while (loc > 0 && buffer[loc - 1] != '\n');

    if (loc <= 0)
        return;
    while (loc > 0 && buffer[loc - 1] != '\n')
        loc--;
    cursor_location = loc;
}

// 光标移动的函数
void cursor_next_line() {
    int loc = cursor_location;
    while (loc < size && buffer[loc] != '\n')
        loc++;
    loc++;
    if (loc < size)
        cursor_location = loc;
}

// 输入是一个key，根据不同的模式
// 执行不同的方法
void do_command_mode(char key) {
    switch (key) {
        case 'j':
            cursor_next_line();
            break;
        case 'h':
            if (cursor_location > 0 && buffer[cursor_location - 1] != '\n')
                cursor_location--;
            break;
        case 'k':
            cursor_prev_line();
            break;
        case 'l':
            if (cursor_location + 1 < size && buffer[cursor_location] != '\n')
                cursor_location++;
            break;
        case 'x':
            if (cursor_location != size - 1)
                delete_key(cursor_location);
            break;
        case ':':
            mode = 2;
            instruction[0] = ':';
            int i = 0;
            for (i = 1; i < COLUMN_LEN; i++)
                instruction[i] = ' ';
            inst_len = 1;
            break;
        case 'i':
            mode = 1;
            return;
        default:
            break;
    }
    if (cursor_location < page_location)
        page_location_last_line();
    else if (cursor_location >= page_end)
        page_location_next_line();
    screen_flush();
}

void do_insert_mode(char key) {
    switch (key) {
        case 27:
            // case 'q':
            mode = 0;
            return;
        case 0x8:
            if (cursor_location != 0)
                delete_key(cursor_location - 1);
            cursor_location--;
            screen_flush();
            if (cursor_location < page_location)
                page_location_last_line();
            break;
        default:
            insert_key(key, cursor_location);
            cursor_location++;
            screen_flush();  // this line is needed because page_end may changed
                             // after insertion
            if (cursor_location >= page_end)
                page_location_next_line();
            break;
    }
    screen_flush();
}

void do_last_line_mode(char key) {
    switch (key) {
        case 27:  // ESC
            inst_len = 0;
            mode = 0;
            break;
        case 8:
            if (inst_len > 0)
                inst_len--;
            break;
        case '\n':
            if (inst_len > 0 && instruction[0] == ':') {
                if (inst_len == 3 && instruction[1] == 'q' && instruction[2] == '!') {
                    err = 1;
                } else if (inst_len == 3 && instruction[1] == 'w' && instruction[2] == 'q') {
                    save_file();
                    err = 1;
                }
            }
            break;
        default:
            instruction[inst_len++] = to_lower_case(key);
            break;
    }
    screen_flush();
}

int myvi(char *para) {
    myvi_init();

    filename = para;
    pre_cursor_freq = cursor_freq;
    cursor_freq = 0;
    kernel_set_cursor();

    mystrcpy(file.path, filename);

    load_file(filename);

    screen_flush();

    /* global variable initial */

    while (err == 0) {
        char key = get_key();
        switch (mode) {
            case 0:  // command mode
                do_command_mode(key);
                break;
            case 1:  // insert mode
                do_insert_mode(key);
                break;
            case 2:  // last line mode
                do_last_line_mode(key);
                break;
            default:
                break;
        }
    }

    cursor_freq = pre_cursor_freq;
    kernel_printf("vi finish\n");
//while(1){}
    kernel_set_cursor();
    kernel_clear_screen(31);

    return 0;
}