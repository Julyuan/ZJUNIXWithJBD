#include"journal.h"

// 内存里的日志全局变量
extern journal_t* journal;

// 日志系统最顶层的初始化
u32 init_journal_info(){
	// print some info
	printk("begin: init journal!\n");
	// 给该日志分配新的空间
	journal = (journal_t*)kmalloc(sizeof(journal_t));
	// allocate space for superblock pointer
	journal->j_superblock = (journal_superblock_t*)kmalloc(sizeof(journal_superblock_t));

	u32 err = 0;
	// 从SD卡里加载superblock并读入内存
	err = load_superblock(journal);
	printk("JOURNAL INIT s_start:	%d\n",journal->j_superblock->s_start);
	printk("JOURNAL INIT s_sequence:	%d\n",journal->j_superblock->s_sequence);
	printk("JOURNAL INIT s_maxlen:	%d\n",journal->j_superblock->s_maxlen);
	printk("JOURNAL INIT s_first:	%d\n",journal->j_superblock->s_first);
	printk("err value: %d\n",err);
	if(err==0){
		// 启动日志的恢复程序
		journal_recover(journal);
		printk("journal superblock load success!\n");
		while(1){}
	}else{
		printk("journal superblock load failed!\n");
		//while(1){}
		journal_update_superblock(journal);
	}
	return 0;
}

// 当我们第一次使用日志系统的时候我们需要将该日志系统初始化了
void journal_new_superblock(journal_t *journal){
	
	printk("journal new superblock!\n");
	// 新建局部指针变量journal_superblock_t
	journal_superblock_t* j_sb;

	// 给该指针变量分配空间
	j_sb = kmalloc(sizeof(sizeof(journal_superblock_t)));

	// 设置superblock的相关value
	j_sb->s_header.h_blocktype = JFS_SUPERBLOCK_V1;
	j_sb->s_header.h_magic = JFS_MAGIC_NUMBER;
	j_sb->s_header.h_sequence = 0;

	j_sb->s_blocksize = BUFFER_HEAD_SECTOR;
	j_sb->s_maxlen = JOURNAL_MAX_LENGTH;
	j_sb->s_start = JOURNAL_DEFAULT_START;
	j_sb->s_sequence = JOURNAL_DEFAULT_SEQUENCE;
	j_sb->s_first = JOURNAL_DEFAULT_FIRST;

	printk("magic:	%d\n",j_sb->s_header.h_magic);
	printk("block size: %d\n",j_sb->s_blocksize);
	printk("maxlen:	%d\n",j_sb->s_maxlen);
	printk("start:	%d\n",j_sb->s_start);
	printk("sequence:	%d\n",j_sb->s_sequence);
	// 将journal的j_superblock字段设置成该指针变量
	journal->j_superblock = j_sb;

}

u32 journal_reset(journal_t *journal){
    journal_superblock_t* sb = journal->j_superblock;
    u32 first, last;

    first = sb->s_first;
    last = sb->s_maxlen;
}

/*
 * Conversion of logical to physical block numbers for the journal
 *
 * On external journals the journal blocks are identity-mapped, so
 * this is a no-op.  If needed, we can use j_blk_offset - everything is
 * ready.
 */
// 实现一个日志块到物理块的映射
u32 journal_bmap(journal_t *journal, u32 blocknr,
		 u32 *retp)
{
	*retp = blocknr + JOURNAL_MAX_LENGTH;
	return 1;
}

/*
 * Log buffer allocation routines:
 */
// 获取存在在日志中的下一个日志块
u32 journal_next_log_block(journal_t *journal, u32 *retp)
{
	u32 blocknr;

	//J_ASSERT(journal->j_free > 1);

	blocknr = journal->j_head;
	journal->j_head++;
	journal->j_free--;
	if (journal->j_head == journal->j_last)
		journal->j_head = journal->j_first;
	return journal_bmap(journal, blocknr, retp);
}

/*
 * Load the on-disk journal superblock and read the key fields into the
 * journal_t.
 */
// 加载SD卡中的日志超级块
u32 load_superblock(journal_t *journal)
{
	printk("journal load superblock!\n");
	int err;
	journal_superblock_t *sb;

	err = journal_get_superblock(journal);
	sb = journal->j_superblock;

	// 对内存中的日志结构相关变量赋值
	journal->j_tail_sequence = sb->s_sequence;
	journal->j_tail = sb->s_start;
	journal->j_first = sb->s_first;
	journal->j_last = sb->s_maxlen;
	if (err)
		return err;



	return 0;
}


/*
 * Read the superblock for a given journal, performing initial
 * validation of the format.
 */

u32 journal_get_superblock(journal_t *journal)
{
	printk("journal get superblock!\n");
	// 创建superblock的union变量，方便SD卡的读取
	union journal_superblock sb_io;
	
	// readblock即是读取一个块，JOURNAL_SUPERBLOCK_POSITION
	// 在这里是superblock在日志中的固定存放位置
	read_block(sb_io.data,JOURNAL_SUPERBLOCK_POSITION,1);
	
	// 创建过渡的journal_superblock_t，拷贝sb_io里的数据
	journal_superblock_t *sb;
	sb = (journal_superblock_t*)kmalloc(sizeof(journal_superblock_t));
	
	// 相关字段值的拷贝
	sb->s_start = sb_io.j_superblock.s_start;
	sb->s_first = sb_io.j_superblock.s_first;
	sb->s_header = sb_io.j_superblock.s_header;
	sb->s_maxlen = sb_io.j_superblock.s_maxlen;
	sb->s_sequence = sb_io.j_superblock.s_sequence;
	sb->s_blocksize = sb_io.j_superblock.s_blocksize;

	printk("sb h_magic:	%d\n",sb->s_header.h_magic);
	printk("sb s_blocksize %d\n",sb->s_blocksize);
	printk("sb s_start:	%d\n",sb->s_start);
	printk("sb s_sequence %d\n",sb->s_sequence);
	// 如果MAGIC_NUMBER不对，则说明读取出现了错误
	// if (sb->s_header.h_magic != JFS_MAGIC_NUMBER ||
	//     sb->s_blocksize != journal->j_blocksize) {
	if(sb->s_header.h_magic != JFS_MAGIC_NUMBER) {
		printk("JBD: no valid journal superblock found\n");
		goto out;
	}
	

	// 判断journal的blocktype
	switch((sb->s_header.h_blocktype)) {
	case JFS_SUPERBLOCK_V1:
		journal->j_format_version = 1;
		break;
	default:
		printk("JBD: unrecognised superblock format ID\n");
		goto out;
	}

	// if ((sb->s_maxlen) < journal->j_maxlen)
	// 	journal->j_maxlen = (sb->s_maxlen);
	// else if ((sb->s_maxlen) > journal->j_maxlen) {
	// 	printk ("JBD: journal file too short\n");
	// 	goto out;
	// }
	journal->j_superblock = sb;
	printk("load success1\n");
	return 0;

// 这个是读取失败的情景
out:
	// 如果读取失败了，我们就要重新new一个superblock出来
	// 并且我们需要释放空间
	kfree(sb);
	printk("enter out!\n");
	journal_new_superblock(journal);
	return 1;
}

// 这个是journal中更新超级块的函数
// 它的作用是更新SD卡中的超级块
// 它在如下场合会被调用：
// 1、transaction提交完成的时候
// 2、checkpoint完成的时候
// 3、重新初始化superblock的时候
// 4、……

void journal_update_superblock(journal_t* journal){

	if(journal==NULL){

		printk("update superblock error, journal is NULL\n");
		return;
	}

	// 构造一个用来写回superblock的union结构
	union journal_superblock jsb;
	union journal_superblock jsb_test;
	// 先对journal中的superblock的值进行修改
	// 因为这里有很多不变量，所以在这里我们只需修改
	// 两项就可以了
	journal->j_superblock->s_sequence = journal->j_tail_sequence;
	journal->j_superblock->s_start = journal->j_tail;

	// 进行标准的赋值操作
	jsb.j_superblock.s_blocksize = journal->j_superblock->s_blocksize;
	jsb.j_superblock.s_first 	 = journal->j_superblock->s_first;
	jsb.j_superblock.s_header 	 = journal->j_superblock->s_header;
	jsb.j_superblock.s_maxlen  	 = journal->j_superblock->s_maxlen;
	jsb.j_superblock.s_sequence  = journal->j_superblock->s_sequence;
	jsb.j_superblock.s_start 	 = journal->j_superblock->s_start;


	printk("call write_block!\n");
	printk("magic %d\n",jsb.j_superblock.s_header.h_magic);
	printk("block type %d\n",jsb.j_superblock.s_header.h_blocktype);
	write_block(jsb.data,JOURNAL_SUPERBLOCK_POSITION,1);

	read_block(jsb_test.data, JOURNAL_SUPERBLOCK_POSITION, 1);

	printk("write_block test\n");
	printk("jsb_test magic: %d\n",jsb_test.j_superblock.s_header.h_magic);
	printk("block type %d\n",jsb_test.j_superblock.s_header.h_blocktype);

}

// 写一个日志的数据块
void journal_write_block(union journal_block* block, journal_t* journal, u32 position){
	// 我们直接实例化write_block
	write_block(block->data, journal->j_first+position,1);
}

// 这个函数是用来控制在日志环形结构里指针的递增的
// 因为在这里+1的操作比较频繁，所以单独的列了出来
u32 journal_pointer_increment(u32 pointer, journal_t* jorunal){
	pointer++;
	if(pointer>=journal->j_last)
		pointer -= (journal->j_last - journal->j_first);
	return pointer;
}

// 这个函数是用来控制在日志环形结构里指针的移动的，length代表移动的步长
// 步长在这里是可正可负的，如果是负的话代表向后移动
u32 journal_pointer_move(u32 pointer, journal_t* journal, u32 length){
	pointer += length;

	// 我们在这里只需要做两边的临界判断就可以了
	if(pointer>=journal->j_last){
		pointer -= (journal->j_last - journal->j_first);
	}
	else if(pointer <= 0){
		pointer += (journal->j_last - journal->j_first);
	}

	return pointer;
}

// 这个函数是在日志操作结束的时候调用
// 在这里我们需要把还没有提交的transaction给提交了，
// 并且我们需要释放无用的内存空间
void journal_finish(journal_t* journal){
	if(journal==NULL){
		printk("JOURNAL FINISH ERROR, journal is NULL!\n");
		return;
	}
	transaction_t* transaction;
	transaction = journal->j_committing_transaction;
	while(transaction!=NULL){
		journal_commit_transactions(journal, transaction);
		transaction = transaction->t_tnext;
	}

	journal_update_superblock(journal);
	kfree(journal->j_superblock);
	kfree(journal);

	return;
}