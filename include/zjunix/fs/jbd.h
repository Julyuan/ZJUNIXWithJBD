#ifndef _ZJUNIX_JBD_H
#define _ZJUNIX_JBD_H

#include <zjunix/type.h>
#include<zjunix/fs/fscache.h>
#include<zjunix/list.h>
#include<driver/vga.h>

// 宏定义，用来日志的描述符块里
// 用来指示块的类型
#define JFS_DESCRIPTOR_BLOCK	1
#define JFS_COMMIT_BLOCK	2
#define JFS_SUPERBLOCK_V1	3
#define JFS_SUPERBLOCK_V2	4
#define JFS_REVOKE_BLOCK	5

// 在我们的日志的系统里日志块的大小是固定的，为512byte
#define BLOCK_SIZE 512

// 这里是buffer head的大小区别
#define BUFFER_HEAD_SECTOR 512
#define BUFFER_HEAD_CLUSTER 4096

#define CHECKPOINT_NUMBER 10
#define JFS_MAGIC_NUMBER 0x03b3998 /* The first 4 bytes of /dev/random! */

// superblock相关宏定义
#define JOURNAL_BLOCK_SIZE 512
#define JOURNAL_MAX_LENGTH 10
#define JOURNAL_DEFAULT_START 0
#define JOURNAL_DEFAULT_SEQUENCE 0
#define JOURNAL_DEFAULT_FIRST	1
#define JOURNAL_SUPERBLOCK_POSITION 15600000

#define printk kernel_printf

typedef struct handle_s		handle_t;	/* Atomic operation type */
typedef struct journal_s	journal_t;	/* Journal control structure */
typedef struct buf_4k		BUF_4K;
typedef struct buf_512		BUF_512;


typedef struct journal_header_s{
    u32 h_magic;        //  魔数，用来描述是否是日志块
    u32 h_blocktype;    // 块的类型
    u32 h_sequence;     // 描述块对应transaction序号
} journal_header_t;

typedef struct journal_block_tag_s
{
	u32		t_blocknr;	/* The on-disk block number */
	u32		t_flags;	/* See below */
} journal_block_tag_t;

union journal_block_tag{
	journal_block_tag_t tag;
	u8 data[8];
};

typedef struct journal_revoke_header_s
{
	journal_header_t r_header;
	u32		 r_count;	/* Count of bytes used in the block */
} journal_revoke_header_t;

// 这里是关于journal_block_tag_s所定义的一系列宏，用来表明块的性质
#define JFS_FLAG_FIRST_TAG	1	/* first tag in this descriptor block */
#define JFS_FLAG_SAME_UUID	2	/* block has same uuid as previous */ 
#define JFS_FLAG_DELETED	4	/* block deleted by this transaction */
#define JFS_FLAG_LAST_TAG	8	/* last tag in this descriptor block */



typedef struct journal_superblock_s{
    journal_header_t s_header; 	// 用于表示本块是一个超级块
    u32 s_blocksize;			// journal 所在设备的块大小
    u32 s_maxlen;				// 日志的长度，即包含多少个块
    u32 s_first;				// 日志中的开始块号，
    u32 s_sequence;				// 就是指该值应该是日志中最旧的一个事务的 ID
    u32 s_start;				// 日志开始的块号

}journal_superblock_t;


union journal_superblock{
	journal_superblock_t j_superblock;
	u8 data[512];
};

struct buffer_head {
	unsigned long b_state;		/* buffer state bitmap (see above) */
	BUF_4K		  *b_page;		/* 指向对应的缓存页 */
	BUF_512		  *b_page1;		

	u32 b_blocknr;				/* start block number 逻辑块号*/
	u32 b_size;					/* size of mapping */
};

typedef struct transaction_s	transaction_t;	/* Compound transaction type */


struct jbd_revoke_table_s{
	u32 hash_size;
	u32 hash_shift;
	struct list_head *hash_table;
};



struct jbd_revoke_record_s{
	struct list_head hash;
	u32 sequence;
	u32 blocknr;
};

struct handle_s
{
	transaction_t	*h_transaction;	// 本原子操作属于哪个transaction
	transaction_t   *h_next_transaction;
	int			h_buffer_credits;	// 本原子操作的额度，即可以包含的磁盘块数
	int			h_ref;			// 引用计数
	int			h_err;
	handle_t 		*h_tnext,  *h_tprev;
	handle_t		*h_cpnext, *h_cpprev;	

	struct buffer_head 	*bh;
	// 以上是三个标志
};




struct transaction_s
{
	journal_t		*t_journal;	// 指向所属的jounal
	tid_t			t_tid;		// 本事务的序号

	/*
	 * Transaction's current state
	 * [no locking - only kjournald alters this]
	 * [j_list_lock] guards transition of a transaction into T_FINISHED
	 * state and subsequent call of __journal_drop_transaction()
	 * FIXME: needs barriers
	 * KLUDGE: [use j_state_lock]
	 */
	enum {
		T_RUNNING,
		T_COMMIT,
		T_CHECKPOINT,
		T_FINISHED
	} t_state;  // 事务的状态

	u32			t_log_start;
	// log中本transaction_t从日志中哪个块开始
	u32 		t_phys_block_num;
	//	本transaction_t在物理日志区的块数目，这里不单单是
	//  数据块，我们还要包括描述符块和提交块等一系列的功能块
	u32			t_block_num;
	// 本transaction_t中block区的个数

	handle_t	*t_buffers;
	// 元数据块缓冲区链表
	// 这里面可都是宝贵的元数据啊，对文件系统的一致性至关重要！
	handle_t	*t_sync_datalist;
	// 本transaction_t被提交之前，
	// 需要被刷新到磁盘上的数据块（非元数据块）组成的双向链表。
	// 因为在ordered模式，我们要保证先刷新数据块，再刷新元数据块。
	handle_t	*t_forget;
	// 被遗忘的缓冲区的链表。
	// 当本transaction提交后，可以un-checkpointed的缓冲区。
	// 这种情况是这样：
	// 一个缓冲区正在被checkpointed，但是后来又调用journal_forget(),
	// 此时以前的checkpointed项就没有用了。
	// 此时需要在这里记录下来这个缓冲区，
	// 然后un-checkpointed这个缓冲区。
	handle_t	*t_checkpoint_list;
	// 本transaction_t可被checkpointed之前，
	// 需要被刷新到磁盘上的所有缓冲区组成的双向链表。
	// 这里面应该只包括元数据缓冲区。
	u32			t_outstanding_credits;
	// 本事务预留的额度
	transaction_t		*t_cpnext, *t_cpprev;	
	// 用于在checkpoint队列上组成链表
	transaction_t		*t_tnext, *t_tprev; 
	// 用于在journal队列上组成链表
	u32 t_handle_count;	
	// 本transaction_t有多少个handle_t
};


struct journal_s
{
	unsigned long		j_flags;	// journal的状态
	int			j_errno;			
	journal_superblock_t	*j_superblock;
	u32			j_format_version;

	transaction_t		*j_running_transaction;
	// 指向正在运行的transaction
	transaction_t		*j_committing_transaction;
	// 指向正在提交的transaction
	transaction_t		*j_checkpoint_transaction;
	// 仍在等待进行checkpoint操作的所有事务组成的循环队列
	// 一旦一个transaction执行checkpoint完成，则从此队列删除。
	// 第一项是最旧的transaction，以此类推。
	u32		j_head;
	// journal中第一个未使用的块
	u32		j_tail;
	// journal中仍在使用的最旧的块号
	// 这个值为0，则整个journal是空的。
	u32		j_free;

	u32		j_first;
	u32		j_last;
	// 这两个是文件系统格式化以后就保存到超级块中的不变的量。
	// 日志块的范围[j_first, j_last)
	// 来自于journal_superblock_t

	int			j_blocksize;

	unsigned int		j_maxlen;
	// 磁盘上journal的最大块数

	tid_t		j_tail_sequence;
	// 日志中最旧的事务的序号

	tid_t		j_transaction_sequence;
	// 下一个授权的事务的顺序号

	tid_t		j_commit_sequence;
	// 最近提交的transaction的顺序号

	int			j_max_transaction_buffers;
	// 一次提交允许的最多的元数据缓冲区块数

	// 指向journal正在使用的revoke hash table
	struct jbd_revoke_table_s *j_revoke_table;

	u32 j_commit_transaction_count;	
	// 本journal相关的、即将commit的transaction的数目

	int			j_wbufsize;
	// 一个描述符块中可以记录的块数
};



// 描述符块的封装结构
union journal_block{
	journal_header_t header;
	u8 data[512];
};

static inline int tid_gt(tid_t x, tid_t y)
{
	int difference = (x - y);
	return (difference > 0);
}

static inline int tid_geq(tid_t x, tid_t y)
{
	int difference = (x - y);
	return (difference >= 0);
}

typedef struct journal_s journal_t;
typedef struct handle_s  handle_t;

// jbd下的函数分类是按照它声明的源文件位置划分的

// 日志在内存里的表现形式，这是一个全局变量
journal_t* journal;

//与日志相关的一系列的函数



/* journal.c下的函数 */

// 日志初始化函数，在初始化fat32文件系统的时候调用
// 这是一个顶层的函数，用来从SD卡里读取日志，并且启动
// 日志的恢复程序
u32 init_journal_info();

// 当我们发现读入的superblock有问题的时候我们就调用这个函数。
// 它的作用是构造一个原始的superblock
void journal_new_superblock(journal_t *journal);

// 这个是日志更新的函数，用来更新日志超级块的元数据
// 它在超级块元数据改变的时候会被调用
void journal_update_superblock(journal_t* journal);

// 加载SD卡中的日志超级块
u32 load_superblock(journal_t *journal);

// 获取存在在日志中的下一个日志块
u32 journal_next_log_block(journal_t *journal, u32 *retp);

// 实现一个日志块到物理块的映射
u32 journal_bmap(journal_t *journal, u32 blocknr, u32 *retp);

// 实现了journal中指针字段（抽象的指针，实质是u32）的环形递增
u32 journal_pointer_increment(u32 pointer, journal_t* jorunal);

u32 journal_pointer_move(u32 pointer, journal_t* journal, u32 length);

u32 journal_get_superblock(journal_t *journal);

void journal_write_block(union journal_block* block, journal_t* journal, u32 position);
/* revoke.c下的函数 */ 

u32 journal_revoke(handle_t *handle, u32 blocknr, struct buffer_head *bh_in);

void write_one_revoke_record(journal_t *journal,
                    union journal_block* buf,
				    u32 *offsetp,
				    struct jbd_revoke_record_s *record);

struct jbd_revoke_table_s *journal_init_revoke_table(u32 hash_size);

u32 insert_revoke_hash(journal_t *journal, u32 blocknr,
			      tid_t seq);


struct jbd_revoke_record_s *find_revoke_record(journal_t *journal,
						      u32 blocknr);

void journal_write_revoke_records(journal_t *journal, transaction_t *transaction);




/* recovery.c下的函数 */ 

// 日志系统的恢复函数，当此文件系统初始化的时候会调用。该函数在运行的过程中
// 会把磁盘的日志去里完整的、但是还没有被checkpoint的
u32 journal_recover(journal_t *journal);

// 这个函数是用来日志系统的recovery阶段的，它的作用是读取一个日志系统里的块
// 虽然它不是必要的，但是因为ext3中有这个函数，我还是把它放进去了
u32 jread(u8 *bhp, journal_t *journal, u32 offset);

// 这是journal_recover中会用到的函数，它的主要作用是从起始位置到终止位置遍历
// 日志块。在journal_recover中我们要做三次recover的操作，而每一次操作的目的
// 都是不同的

/* commit.c下的函数 */ 

// transaction的提交函数，用来提交一个在journal中的transaction
void journal_commit_transaction(journal_t *journal, transaction_t *transaction);

// transaction提交函数的顶层，用来处理某个transaction的提交请求，可能会提交多个transaction
void journal_commit_transactions(journal_t *journal, transaction_t *transaction);

// 写一个数据块，写的位置由journal中的j_head决定
void write_data_block(handle_t* handle, journal_t* journal);

// 对于一个transaction作checkpoint的操作
u32 do_one_checkpoint(journal_t*journal, transaction_t* transaction);

// 删除transaction里checkpoint队列一个已经写回SD卡的handle
void journal_erase_handle(transaction_t* transaction, handle_t* handle);


void do_checkpoint(journal_t* journal);


#endif