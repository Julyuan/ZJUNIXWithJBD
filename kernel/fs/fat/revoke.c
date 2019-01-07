#include"recovery.h"

// 将一个handle所关联的磁盘块插入到journal的revoke hash table里去
int journal_revoke(handle_t *handle, unsigned int blocknr, struct buffer_head *bh_in){
    
}



static struct jbd_revoke_table_s *journal_init_revoke_table(int hash_size)
{
	int shift = 0;
	int tmp = hash_size;
	struct jbd_revoke_table_s *table;

	//table = kmem_cache_alloc(revoke_table_cache, GFP_KERNEL);
	table = kmalloc(sizeof(table));
    if (!table)
		goto out;

	while((tmp >>= 1UL) != 0UL)
		shift++;

	table->hash_size = hash_size;
	table->hash_shift = shift;
	table->hash_table =
		(struct list_head*)kmalloc(hash_size * sizeof(struct list_head));
	if (!table->hash_table) {
		//kmem_cache_free(revoke_table_cache, table);
		kfree(table);
        table = 0;
		goto out;
	}

	for (tmp = 0; tmp < hash_size; tmp++)
		INIT_LIST_HEAD(&table->hash_table[tmp]);

out:
	return table;
}

/* Utility functions to maintain the revoke table */

/* Borrowed from buffer.c: this is a tried and tested block hash function */
static inline int hash(journal_t *journal, unsigned int block)
{
	struct jbd_revoke_table_s *table = journal->j_revoke_table;
	int hash_shift = table->hash_shift;

	return ((block << (hash_shift - 6)) ^
		(block >> 13) ^
		(block << (hash_shift - 12))) & (table->hash_size - 1);
}

static int insert_revoke_hash(journal_t *journal, unsigned int blocknr,
			      tid_t seq)
{
	struct list_head *hash_list;
	struct jbd_revoke_record_s *record;

repeat:
    record = (struct jbd_revoke_record_s*)kmalloc(sizeof(struct jbd_revoke_record_s));
	//record = kmem_cache_alloc(revoke_record_cache, GFP_NOFS);

	record->sequence = seq;
	record->blocknr = blocknr;
	hash_list = &journal->j_revoke_table->hash_table[hash(journal, blocknr)];
	list_add(&record->hash, hash_list);
	return 0;
}

/* Find a revoke record in the journal's hash table. */

static struct jbd_revoke_record_s *find_revoke_record(journal_t *journal,
						      unsigned int blocknr)
{
	struct list_head *hash_list;
	struct jbd_revoke_record_s *record;

	hash_list = &journal->j_revoke_table->hash_table[hash(journal, blocknr)];

	record = (struct jbd_revoke_record_s *) hash_list->next;
	while (&(record->hash) != hash_list) {
		if (record->blocknr == blocknr) {
			return record;
		}
		record = (struct jbd_revoke_record_s *) record->hash.next;
	}
	return NULL;
}

// 这里是将revoke存在内存里的记录写入日志磁盘区
/*
 * Write revoke records to the journal for all entries in the current
 * revoke hash, deleting the entries as we go.
 */
void journal_write_revoke_records(journal_t *journal, transaction_t *transaction)
{
    // 感觉这个用不到了
	// struct journal_head *descriptor;
	
    struct jbd_revoke_record_s *record;
	struct jbd_revoke_table_s *revoke;
	struct list_head *hash_list;
	u32 i, offset, count;

	//descriptor = NULL;
	offset = 0;
	count = 0;

	/* select revoke table for committing transaction */
	revoke = journal->j_revoke_table;

    // 我们应该先把journal_block和journal_block_tag处理了
    // 新分配一个journal_block，并且设置新的参数                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        
    union journal_block *buf = kmalloc(sizeof(union journal_block));
    buf->header.h_blocktype = JFS_COMMIT_BLOCK;
    buf->header.h_magic = JFS_MAGIC_NUMBER;
    buf->header.h_sequence = transaction->t_tid;


	for (i = 0; i < revoke->hash_size; i++) {
		hash_list = &revoke->hash_table[i];

		while (!list_empty(hash_list)) {
			record = (struct jbd_revoke_record_s *)
				hash_list->next;
			write_one_revoke_record(journal, buf, &offset,record);
			count++;

            // 将该记录从链表中删除
			list_del(&record->hash);
			//kmem_cache_free(revoke_record_cache, record);
            // 
            kfree(record);
		}
	}
	//if (descriptor)
	//	flush_descriptor(journal, descriptor, offset, write_op);
	//jbd_debug(1, "Wrote %d revoke records\n", count);
}

/*
 * Write out one revoke record.  We need to create a new descriptor
 * block if the old one is full or if we have not already created one.
 */
// 这个函数的作用是写一条记录
static void write_one_revoke_record(journal_t *journal,
                    union journal_block* buf,
				    u32 *offsetp,
				    struct jbd_revoke_record_s *record)
{
	struct journal_head *descriptor;
	int offset;
	journal_header_t *header;

	offset = *offsetp;

	// * (&jh2bh(descriptor)->b_data[offset])) =
	// 	cpu_to_be32(record->blocknr);

	offset += 4;
	*offsetp = offset;
}
