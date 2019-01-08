
#include "recovery.h"


enum passtype {PASS_SCAN, PASS_REVOKE, PASS_REPLAY};

u32 journal_set_revoke(journal_t *journal,
						u32 blocknr,
						tid_t sequence);

// 恢复信息，记录了日志恢复阶段的相关变量的值
struct recovery_info{
    u16 start_transaction;
    u16 end_transaction;

    u32 nr_replays;
    u32 nr_revokes;
    u32 nr_revoke_hits;

};

static u32 do_one_pass(journal_t *journal, struct recovery_info *info, enum passtype pass);


static u32 scan_revoke_records(journal_t *journal, union journal_block *bh,
			       tid_t sequence, struct recovery_info *info);

// 根据offset读进一个块，并且将内容赋值到buffer head上去
u32 jread(u8 *bhp, journal_t *journal, u32 offset){
	if(bhp==NULL){
		return 0;
	}
	read_block(bhp,JOURNAL_SUPERBLOCK_POSITION+offset,1);
	return 1;
}

// 这个是对一个bh块的释放操作
static inline void brelse(union journal_block* bh)
{
	if (bh){
		kfree(bh);
	}
}

/*
 * Count the number of in-use tags in a journal descriptor block.
 */
// 计算一个描述符块里有多少个tag,该函数的返回值是journal_block_tag
// 的数目
static u32 count_tags(union journal_block *bh, int size)
{
	u8*			tagp;
	journal_block_tag_t *	tag;
	u32			nr = 0;

	tagp = &bh->data[sizeof(journal_header_t)];

	while ((tagp - bh->data + sizeof(journal_block_tag_t)) <= size) {
		tag = (journal_block_tag_t *) tagp;

		nr++;
		tagp += sizeof(journal_block_tag_t);
		
		// 下面的代码是在ext3中出现的，UUID用来表示磁盘块所属的文件系统
		// 然而在我们如今的设计里还没有这个概念，我们暂时不同考虑这个东西
		// if (!(tag->t_flags & (JFS_FLAG_SAME_UUID)))
		// 	 tagp += 16;

		if (tag->t_flags & JFS_FLAG_LAST_TAG)
			break;
	}

	return nr;
}


u32 journal_recover(journal_t *journal){
	printk("begin recovery\n");
	if(journal==NULL){
		printk("recovery error, journal is NULL\n");
		while(1){}
	}
    u32 err, err2;

	// 日志的超级块
    journal_superblock_t* sb = NULL;

    struct recovery_info info;
	
	// 对恢复信息进行初始化
    kernel_memset(&info, 0, sizeof(info));
    sb = journal->j_superblock;
	printk("JOURNAL RECOVERY s_start:	%d\n",sb->s_start);
	printk("JOURNAL RECOVERY s_sequence:	%d\n",sb->s_sequence);
	printk("JOURNAL RECOVERY s_maxlen:	%d\n",sb->s_maxlen);
	printk("JOURNAL RECOVERY s_first:	%d\n",sb->s_first);

	//while(1){}
	if (!sb->s_start) {
		printk("no need to recovery!\n");
		//while(1){}
		// 如果文件系统是被正常卸载的，则不需要恢复。
		// 递增 j_transaction_sequence，使整个日志无效。
		journal->j_transaction_sequence = (sb->s_sequence) + 1;
		return 0;
	}

    err = do_one_pass(journal, &info, PASS_SCAN);
	if (!err)
		err = do_one_pass(journal, &info, PASS_REVOKE);
	if (!err)
		err = do_one_pass(journal, &info, PASS_REPLAY);

    journal->j_transaction_sequence = ++info.end_transaction;

}



/* Make sure we wrap around the log correctly! */
#define wrap(journal, var)						\
do {									\
	if (var >= (journal)->j_last)					\
		var -= ((journal)->j_last - (journal)->j_first);	\
} while (0)


// 扫描整一块日志区
static u32 do_one_pass(journal_t *journal, struct recovery_info *info, enum passtype pass){
    u32     first_commit_ID, next_commit_ID;
    u32		next_log_block;
    u32 	err, success = 0;
    journal_superblock_t *sb = NULL;
    journal_header_t     *tmp = NULL;
    //struct buffer_head   *bh;

	// 这是日志从磁盘到内存的接口，它使得读写更加的规范
	union journal_block* bh = NULL;
	bh = (union journal_block*)kmalloc(sizeof(union journal_block));

	// transaction的序号
    u32     sequence;
	// 日志磁盘区的块类型
    u32     blocktype;

	// 获得日志的超级块
    sb = journal->j_superblock;

	// 获得最旧的那个transaction的序号和开始块号
    next_commit_ID = sb->s_sequence;
    next_log_block = sb->s_start;

	// 赋值第一个要提交的ID
    first_commit_ID = next_commit_ID;

	// 如果我们进行的是第一次的扫描操作，我们就要更新
	// recover_info的信息
    if(pass == PASS_SCAN)
        info->start_transaction = first_commit_ID;

	// 顶层的扫描操作
    while(1){
        u32         flags;
		// 记录字符缓存的偏移量
        u8 *      tagp = NULL;
		
		// 貌似每次都需要重新分配空间
		bh = (union journal_block*)kmalloc(sizeof(union journal_block));

		// 从日志的磁盘去读进一个块
        err = jread(bh->data, journal, next_log_block);
		
		// 下一个日志块递增，注意环形结构，这个递增操作是通用的，
		// 即每一个外层的while循环都会进行递增的操作
        next_log_block++;
        wrap(journal, next_log_block);

		// 获得描述符块的header
        tmp = &(bh->header);

        if(tmp->h_magic != JFS_MAGIC_NUMBER){
            // 这里应该是处理brelse，可能是释放bh的空间，
			brelse(bh);
            break;
        }

		// 确定块的类型和序列号
        blocktype = tmp->h_blocktype;
        sequence = tmp->h_sequence;

		// 因为这是我们读入的第一个块，如果ID没有匹配的话说明我们的日志是有问题的
        if(sequence != next_commit_ID){
			// 打印错误信息
			printk("recovery error, commit ID doesn't match!\n");

			// 这里应该是处理brelse，可能是释放bh的空间，
           	brelse(bh);
            break;
        }

		switch(blocktype) {
		case JFS_DESCRIPTOR_BLOCK:
			/* If it is a valid descriptor block, replay it
			 * in pass REPLAY; otherwise, just skip over the
			 * blocks it describes. */
			// 我们只会在replay的状态下才会进行transaction重做，在其他的阶段
			// 我们是要进行跳过的操作的
			if (pass != PASS_REPLAY) {
				next_log_block +=
					count_tags(bh, journal->j_blocksize);
				wrap(journal, next_log_block);
				// 这里应该是处理brelse，可能是释放bh的空间，

				//brelse(bh);
				continue;
			}

			/* A descriptor block: we can now write all of
			 * the data blocks.  Yay, useful work is finally
			 * getting done here! */
			// tagp 在这里是一个char数组的指针，我们要准备开始做
			// 写回的操作了，此时的passtype应该是PASS_REPLAY
			tagp = &(bh->data[sizeof(journal_header_t)]);

			// 最外层的while是判断日志磁盘区是否越界
			while ((tagp - bh->data +sizeof(journal_block_tag_t)) <= journal->j_blocksize) 
			{
				// 处理这个描述符快，将其中的数据块从日志中读出来，根据指示写回到磁盘的原始位置
				// u32在这里相当于一个指针
				u32 io_block;

				//从描述符块中取出一个journal_block_tag_t结构
				union journal_block_tag* tag = (union journal_block_tag*) tagp;
				flags = tag->tag.t_flags;

				//io_block表示描述符块之后的日志中的一个数据块
				//我们要把io_block中的数据写回到磁盘的原始位置
				io_block = next_log_block++;
				wrap(journal, next_log_block);

				// 获取记录在日志中的数据块，obh在这里相当于是接口
				union journal_block* obh = (union journal_block*)kmalloc(sizeof(union journal_block));
				err = jread(obh->data, journal, io_block);
				if (err) {
					/* Recover what we can, but
					 * report failure at the end. */
					success = err;
					printk (
						"JBD: IO error %d recovering "
						"block %u in log\n",
						err, io_block);
				} else {
					u32 blocknr;

				//	J_ASSERT(obh != NULL);
					if(obh==NULL){
						printk("recovery error, obh is NULL!\n");
						while(1){}
					}
					blocknr = tag->tag.t_blocknr;

					/* If the block has been
					 * revoked, then we're all done
					 * here. */
					// 这里是要测试取消的操作
					/*
					if (journal_test_revoke
					    (journal, blocknr,
					     next_commit_ID)) {
						brelse(obh);
						++info->nr_revoke_hits;
						goto skip_write;
					}	*/

					/* Find a buffer for the new
					 * data being restored */
					// 获取目标的块
					union journal_block* nbh = (union journal_block*)kmalloc(sizeof(union journal_block));

					read_block(nbh->data, blocknr, 1);
					if (nbh == NULL) {
						printk("JBD: Out of memory during recovery.\n");
						err = -ENOMEM;
						brelse(bh);
						brelse(obh);
						goto failed;
					}

					kernel_memcpy(nbh->data, obh->data, journal->j_blocksize);
					write_block(nbh->data, blocknr, 1);
					
					// 这里涉及到的是转义的操作，我们暂时不用
				//	if (flags & JFS_FLAG_ESCAPE) {
				//		*((__be32 *)nbh->b_data) =
				//		cpu_to_be32(JFS_MAGIC_NUMBER);
			//		}

	
					++info->nr_replays;
					brelse(obh);
					brelse(nbh);
				}

			// 原本在revoke的机制下，如果一个块被跳过了就直接执行下面的代码
			skip_write:
				tagp += sizeof(journal_block_tag_t);
				if (!(flags & JFS_FLAG_SAME_UUID))
					tagp += 16;

				if (flags & JFS_FLAG_LAST_TAG)
					break;
			}

			brelse(bh);
			continue;

		case JFS_COMMIT_BLOCK:
			/* Found an expected commit block: not much to
			 * do other than move on to the next sequence
			 * number. */
			// 如果是一个提交块，我们就什么也不做，但是我们递增了next_commit_ID
			// 因为没有提交块的日志是不完整的，我们要确定info变量里的end_transaction
			brelse(bh);
			next_commit_ID++;
			continue;

		case JFS_REVOKE_BLOCK:
			/* If we aren't in the REVOKE pass, then we can
			 * just skip over this block. */
			if (pass != PASS_REVOKE) {
				brelse(bh);
				continue;
			}
			// 扫描revoke记录
			err = scan_revoke_records(journal, bh,
						  next_commit_ID, info);
			brelse(bh);
			if (err)
				goto failed;
			continue;

		 default:
		// 	jbd_debug(3, "Unrecognised magic %d, end of scan.\n",
		// 		  blocktype);
			brelse(bh);
			goto done;
		}
	}

 done:
	/*
	 * We broke out of the log scan loop: either we came to the
	 * known end of the log or we found an unexpected block in the
	 * log.  If the latter happened, then we know that the "current"
	 * transaction marks the end of the valid log.
	 */

	if (pass == PASS_SCAN)
		info->end_transaction = next_commit_ID;
	else {
		/* It's really bad news if different passes end up at
		 * different places (but possible due to IO errors). */
		if (info->end_transaction != next_commit_ID) {
			printk ("JBD: recovery pass %d ended at "
				"transaction %d, expected %d\n",
				pass, next_commit_ID, info->end_transaction);
			while(1){}
			if (!success)
				success = -EIO;
		}
	}

	return success;

 failed:
	return err;
}

/* Scan a revoke record, marking all blocks mentioned as revoked. */

static u32 scan_revoke_records(journal_t *journal, union journal_block *bh,
			       tid_t sequence, struct recovery_info *info)
{
	journal_revoke_header_t *header;
	u32 offset, max;

	header = (journal_revoke_header_t *) bh->data;
	offset = sizeof(journal_revoke_header_t);
	max = (header->r_count);

	while (offset < max) {
		u32 blocknr;
		u32 err;

		// 这句话好像有点问题，貌似是u8转成u32
		blocknr = *((u32 *)(bh->data+offset));

		offset += 4;
		err = journal_set_revoke(journal, blocknr, sequence);
		if (err)
			return err;
		++info->nr_revokes;
	}
	return 0;
}

u32 journal_set_revoke(journal_t *journal,
						u32 blocknr,
						tid_t sequence)
{
	struct jbd_revoke_record_s *record;

	record = find_revoke_record(journal, blocknr);

	if(record){
		if(tid_gt(sequence, record->sequence))
			record->sequence = sequence;
		return 0;
	}

	return insert_revoke_hash(journal, blocknr, sequence);
}

/*
 * Test revoke records.  For a given block referenced in the log, has
 * that block been revoked?  A revoke record with a given transaction
 * sequence number revokes all blocks in that transaction and earlier
 * ones, but later transactions still need replayed.
 */

int journal_test_revoke(journal_t *journal,
			unsigned int blocknr,
			tid_t sequence)
{
	struct jbd_revoke_record_s *record;

	record = find_revoke_record(journal, blocknr);
	if (!record)
		return 0;
	if (tid_gt(sequence, record->sequence))
		return 0;
	return 1;
}