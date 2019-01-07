#include"commit.h"

extern struct fs_info fat_info;

/*
 * journal_commit_transactions
 * 这个是顶层api，每当缓存区中的一个缓存区需要写回磁盘，并且它的
 * 对应的transaction还没有commit的时候我们就调用这个api，对于
 * 每一个t_id小于该transaction，而没有被提交的transaction，我们
 * 都应该把它们提交
 * 
 */

void journal_commit_transactions(journal_t *journal, transaction_t *transaction){

    // 错误检查，确保这两个指针不是空指针
    // 如果出错的话就打印相关的错误信息
    if(journal==NULL){
        printk("transactions commit error, journal is NULL!\n");
        return;
    }

    if(transaction == NULL){
        printk("transactions commit error, transaction is NULL!\n");
        return;
    }

    // 获取transaction的序列号
    tid_t t_cur_id = transaction->t_tid;

    // 新建transaction_t的指针变量
    transaction_t* start_transaction, *end_transaction, *cur_transaction;
    start_transaction = journal->j_committing_transaction;

    if(start_transaction == NULL){
        printk("commit transactions error, start transaction is NULL!\n");
        return;
    }

    cur_transaction = start_transaction;

    // transaction遍历，找到最后一个transaction
    for(u32 i=0;i<journal->j_commit_transaction_count - 1;i++){
        cur_transaction = cur_transaction->t_tnext;

        // 错误判断    
        if(cur_transaction==NULL){
            printk("commit transactions error, cur transaction is NULL!\n");
            return;
        }
    }

    end_transaction = cur_transaction;

    // 这里需要注意的是，在我们现在的日志设计模式下，等待提交transaction
    // 在队列里是有序的，它的t_tid的大小是递减的，最早等待提交的日志是在
    // 队列的最后
    while(1){

        // 当这个transaction是先做完的，我们就需要将这个transaction提交
        if(cur_transaction->t_tid <= t_cur_id){
            journal_commit_transaction(journal,cur_transaction);
        }
        else{
            break;
        }
        cur_transaction = cur_transaction->t_tprev;

        // 下面的判断是代表已经到链表头了
        if(cur_transaction==NULL)
            break;
    }
}


/*
 * journal_commit_transaction
 *
 * The primary function for committing a transaction to the log.  This
 * function is called by the journal thread to begin a complete commit.
 */
// 若选择journal模式提交日志，我们需要写回所有元数据和文件数据,

void journal_commit_transaction(journal_t *journal, transaction_t *transaction)
{
    // 错误检查，确保这两个指针不是空指针
    // 如果出错的话就打印相关的错误信息
    if(journal==NULL){
        printk("transaction commit error, journal is NULL!\n");
        return;
    }

    if(transaction == NULL){
        printk("transaction commit error, transaction is NULL!\n");
        return;
    }

    transaction_t* cur = journal->j_committing_transaction;
    handle_t* cur_handle;
    unsigned char* tagp = NULL;
    u32 blocknr;
    u32 space_left = 0;
	u32 first_tag = 0;
	u32 tag_flag;
    
    cur_handle = transaction->t_buffers;
    // 在日志中的第一个未使用的块
    u32 head;
    head = journal->j_head;
    u32 tail;
    tail = journal->j_tail;

    // 新分配一个journal_block，并且设置新的参数                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        
    union journal_block *buf = kmalloc(sizeof(union journal_block));

    // 确定描述符块的相关参数
    buf->header.h_blocktype = JFS_DESCRIPTOR_BLOCK;
    buf->header.h_magic = JFS_MAGIC_NUMBER;
    buf->header.h_sequence = transaction->t_tid;

    // tagp在这里是一个字符串的指针变量，用来控制
    // 字符串的复制,tagp将会永远的指向日志块缓冲区
    // 的头部
	tagp = &buf->data[sizeof(journal_header_t)];

    // 代表一个块剩余的空间
    space_left = BLOCK_SIZE - sizeof(journal_header_t);
    
    // tag_buf是journal_block_tag的union结构，为的是方便字符串的拷贝
    union journal_block_tag *tag_buf = kmalloc(sizeof(union journal_block_tag));

    // 将剩余的journal_block_tag_t写入描述符块
    u32 count = transaction->t_block_num;

    while(1){
        handle_t* handle_p = transaction->t_buffers;
        
        // 如果只有一个sector，我们只要写一个block_tag
        if(handle_p->bh->b_size==BUFFER_HEAD_SECTOR){

            // 个人感觉这里还是用union稳一点
            tag_buf->tag.t_blocknr = handle_p->bh->b_blocknr;

            // t_flags是一个指示标识符
            // 对于t_flags的赋值要采用按位或的方法
            // 因为t_flags可能会具有多样的属性
            if(count==transaction->t_handle_count){
                // 代表第一个块
                tag_buf->tag.t_flags |= JFS_FLAG_FIRST_TAG;
            }
            else if(count==1){
                // 代表最后一个块
                tag_buf->tag.t_flags |= JFS_FLAG_LAST_TAG;
            }
            else{
                // 代表的是中间的块
                tag_buf->tag.t_flags |= JFS_FLAG_SAME_UUID;
            }
            // 拷贝内存，将tagp的内容拷贝到tag_buf里去，这里只需拷贝8个byte就可以了
            kernel_memcpy(tagp, tag_buf, 8);
            tagp+=8;
            count--;

        }   // 如果存在一个cluster，我们就需要连续的写多个block_tag
        else{ 
            for(u32 i=0;i<fat_info.BPB.attr.sectors_per_cluster;i++){

                // 对于cluster的BUF，它的cur的值实质上就是其中第一个sector的位置
                tag_buf->tag.t_blocknr = handle_p->bh->b_blocknr+i;
                if(count==transaction->t_block_num){
                    // 代表第一个块
                    tag_buf->tag.t_flags |= JFS_FLAG_FIRST_TAG;
                }
                else if(count==1){
                    // 代表最后一个块
                    tag_buf->tag.t_flags |= JFS_FLAG_LAST_TAG;
                }
                else{
                    // 代表的是中间的块
                    tag_buf->tag.t_flags |= JFS_FLAG_SAME_UUID;
                }
                // 拷贝内存，将tagp的内容拷贝到tag_buf里去
                kernel_memcpy(tagp, tag_buf, 8);
                // 移动指针的位置
                tagp+=8;
                count--;
            }
        }

        handle_p = handle_p->h_tnext;

        if(handle_p == NULL){
            break;
        }
        if(count==0)
            break;
    }

    if(journal->j_free==0){
        do_checkpoint(journal);
        if(journal->j_free==0){
            printk("free space error!\n");
            return;
        }
    }

    // still have problem
    journal_write_block(buf,journal,journal->j_head);
    journal->j_head = journal_pointer_increment(journal->j_head, journal);
    journal->j_free--;

    // 处理数据块
    while(1){
        write_data_block(cur_handle, journal);
        cur_handle = cur_handle->h_tnext;
    }

    // 我们写一个提交块
    union journal_block *buf1 = kmalloc(sizeof(union journal_block));
    buf1->header.h_blocktype = JFS_COMMIT_BLOCK;
    buf1->header.h_magic = JFS_MAGIC_NUMBER;

    if(journal->j_free==0){
        do_checkpoint(journal);
        if(journal->j_free==0){
            printk("free space error!\n");
            return;
        }
    }

    // 我们在这里把描述符块写入日志的磁盘去
    journal_write_block(buf1,journal,journal->j_head);
    journal->j_head = journal_pointer_increment(journal->j_head, journal);
    journal->j_free--;

    // 我们在这里写一个取消块
    journal_write_revoke_records(journal, transaction);

    // 因为我们写入了日志，日志里的superblock内容发生了改变
    // 为了保证数据的一致性，我们需要把日志中的superblock更新
    // 到磁盘
    journal_update_superblock(journal);

    // 在这里我们需要释放该函数里分配的临时空间
    // 目前分配的空间有buf1、buf、tag_buf
    if(buf1!=NULL){
        kfree(buf1);
    }

    if(buf!=NULL){
        kfree(buf);
    }

    if(tag_buf!=NULL){
        kfree(buf);
    }

    // 在这里我们需要调整transaction的handle
    // 我们使用handle里的h_cpnext、h_cpprev将这些剩余的handle相连
    handle_t *h1,*h2;
    h1 = transaction->t_buffers;
    if(h1==NULL){
        printk("h1 error!");
        return;
    } 

    // 通过while循环遍历链表
    // 将这些handle相互连接
    while(1){
        h2 = h1->h_tnext;
        // 判断h2是不是空指针
        // 这也表示是否到了双向链表的末尾
        if(h2==NULL){
            h1->h_cpnext = NULL;
            break;
        }
        else{
            h1->h_cpnext = h2;
            h2->h_cpprev = h1;
        }
        h1 = h1->h_tnext;

        // 原来的h_tnext和h_tprev已经失效了
        // 在这里我们直接把它们删去
        // h1->h_tnext = NULL;
        // h1->h_tprev = NULL;
    }

    // 我们这里将该transaction从journal_commit_transaction中移除，
    // 加到journal_checkpoint_transaction的队列中去
    transaction->t_state = T_CHECKPOINT;
    if(journal->j_committing_transaction==transaction){
        if(transaction->t_tnext==NULL){
            journal->j_committing_transaction = NULL;
        }
        else{
            journal->j_committing_transaction = journal->j_committing_transaction->t_tnext;
            journal->j_committing_transaction->t_tprev = NULL;
        }
    }
    else{
        if(transaction->t_tnext == NULL){
            transaction->t_tprev->t_tnext = NULL;
        }
        else{
            transaction->t_tprev->t_tnext = transaction->t_tnext;
            transaction->t_tnext->t_tprev = transaction->t_tprev;
        }
    }

    // 将该transaction放到checkpoint队列里去
    // 需要注意的是这里的checkpoint队列必须是有序的，因为checkpoint操作
    // 必须是有序的，前面的transaction必须比后面的先checkpoint
    // 我们在做checkpoint操作的时候是从这个队列头

    // 如果这个checkpoint队列是空的，我们就直接把它放到队列头
    if(journal->j_checkpoint_transaction == NULL){
         journal->j_checkpoint_transaction=transaction;
         transaction->t_cpnext = NULL;
         transaction->t_cpprev = NULL;
     }
     else if(journal->j_checkpoint_transaction != NULL){
         transaction_t* checkpoint_transaction = journal->j_checkpoint_transaction;
         
         while(1){
            if(checkpoint_transaction->t_tid > transaction->t_tid){
                break;
            }
            else{
                checkpoint_transaction = checkpoint_transaction->t_cpnext;
                if(checkpoint_transaction==NULL)
                    break;
            }
         }

         // checkpoint_transaction是用来确定transaction在checkpoint队列的位置的
         // 该transaction应该放在checkpoint_transaction之前
         // 这里还要判断的是checkpoint_transaction是不是队列的头
         if(checkpoint_transaction==journal->j_checkpoint_transaction){
            journal->j_committing_transaction = transaction;
            transaction->t_cpnext = checkpoint_transaction;
            transaction->t_cpprev = NULL;
            checkpoint_transaction->t_cpprev = transaction;
         }
         else if(checkpoint_transaction == NULL){
            // 这里表示已经到队列的结尾了
            // 我们先采用了重新遍历的方法，虽然有点问题
            checkpoint_transaction = journal->j_checkpoint_transaction;
            while(checkpoint_transaction->t_cpnext!=NULL){
                checkpoint_transaction = checkpoint_transaction->t_cpnext;
            }

            // 此时checkpoint_transaction是队列的最后一个transaction
            checkpoint_transaction->t_cpnext = transaction;
            transaction->t_cpprev = checkpoint_transaction;
            transaction->t_cpnext = NULL;
         }
         else{
             // 处理最普通的情况
             transaction->t_cpnext = checkpoint_transaction;
             transaction->t_cpprev = checkpoint_transaction->t_cpprev;
             checkpoint_transaction->t_cpprev->t_cpnext = transaction;
             checkpoint_transaction->t_cpprev = transaction;
         }

     }

}

//写回一个磁盘块到日志里去
void write_data_block(handle_t* handle, journal_t* journal)
{
    // 错误检查，确保这两个指针不是空指针
    // 如果出错的话就打印相关的错误信息
    if(journal==NULL){
        printk("write data block error, journal is NULL!\n");
        return;
    }

    if(handle == NULL){
        printk("write data block error, transaction is NULL!\n");
        return;
    }

    // 当剩余的块数不够了的时候我们就要做checkpoint
    // 的相关操作了，我们需要释放一些旧的transaction的
    // 日志块
    if(journal->j_free<=8){
        do_checkpoint(journal);
    }

    // 对于handle指向的缓存区，我们是有两种情况的，一种是sector，sector
    // 大小和日志中的block相似，因此只需写一个块就可以了，而对于cluster来说，
    // 它的大小有8个block，因此我们需要连续写八个块
    if(handle->bh->b_size==BUFFER_HEAD_SECTOR){
        write_block(handle->bh->b_page1->buf, journal->j_head, 1);
        journal->j_head++;
        journal->j_free--;
    }else if(handle->bh->b_size==BUFFER_HEAD_CLUSTER){
        write_block(handle->bh->b_page->buf, journal->j_head, fat_info.BPB.attr.sectors_per_cluster);

        // 修改对应的j_head和j_free
        journal->j_head+=8;
        journal->j_free-=8;
    }
};

// 处理checkpoint 队列，
void do_checkpoint(journal_t* journal){
    // 取出日志中checkpoint队列里的第一个transaction
    transaction_t* first_transaction = journal->j_checkpoint_transaction;
    transaction_t* cur_transaction = first_transaction;
    // 获取checkpoint列表中的第一个
    // handle_t* handle_cp = first_transaction->t_checkpoint_list;
    // 我们在这里要调整journal的某些参数，还要将checkpoint的队列进行一定的调整

    u32 total = CHECKPOINT_NUMBER;
    u32 count = 0;
    while(1){
        count+=do_one_checkpoint(journal, cur_transaction);
        cur_transaction = cur_transaction->t_cpnext;
        if(cur_transaction==NULL || count >= total){
            break;
        }
    }

    // 此时first_transaction到cur_transaction之间的transaction已经失去作用了
    // 我们应该把这些空间释放出来，因为其中的handle已经被处理掉了，我们在这里就不再
    // 处理
    transaction_t* temp1_transaction = first_transaction;
    transaction_t* temp2_transaction;
    while(1){
        if(temp1_transaction==NULL){
            break;
        }
        temp2_transaction = temp1_transaction;
        
        // 在这里temp2_transaction是我们即将要释放的transaction，我们必须先检查它的状态
        // 因为在之前的do_one_checkpoint循环里面我们已经变更了transaction的状态，如果正常
        // 的话它的状态应该是T_FINISHED。如果不是这个状态则说明出现了异常的情况。我们必须要打
        // 印错误信息并且不应该释放这块空间
        if(temp2_transaction->t_state!=T_FINISHED){
            printk("checkpoint error, transaction state error!\n");
        }else{
            kfree(temp2_transaction);
        }
        temp1_transaction = temp1_transaction->t_cpnext;

        // cur_transaction在这里是一个暂停的标志，遇到cur_transaction
        // 说明transaction已经结束了
        if(temp1_transaction==cur_transaction)
            break;
    }

    // 因为我们写入了日志，日志里的superblock内容发生了改变
    // 为了保证数据的一致性，我们需要把日志中的superblock更新
    // 到磁盘
    journal_update_superblock(journal);
}

// 处理单独的一个checkpoint
u32 do_one_checkpoint(journal_t*journal, transaction_t* transaction){
    if(journal==NULL){
        printk("checkpoint error, journal is NULL!");
        return 0;
    }
    if(transaction==NULL){
        printk("checkpoint error, transaction is NULL!");
        return 0;
    }

    // 完成一个transaction的checkpoint操作
    handle_t* handle_head = transaction->t_checkpoint_list;

    // 写回磁盘块的引用计数
    u32 write_count;
    write_count = 0;

    // 因为这里涉及到释放空间的问题，我们需要两个指针变量
    handle_t* handle_ano;
    while(handle_head!=NULL){

        if(handle_head->bh->b_size==BUFFER_HEAD_SECTOR){
            if(handle_head->bh->b_page1->handle != handle_ano){
                // 因为我们没有引入引用计数这一个概念，所以这里必须要考虑一个BUF
                // 归属到其他handle的情况，我们暂不考虑这种情况，直接暴力写回
                write_block(handle_head->bh->b_page1->buf,handle_head->bh->b_page1->cur,1);
            }else{
                write_block(handle_head->bh->b_page1->buf,handle_head->bh->b_page1->cur,1);
            }
            write_count+=1;
        }
        else if(handle_head->bh->b_size==BUFFER_HEAD_CLUSTER){
            if(handle_head->bh->b_page1->handle != handle_ano){
                // 因为我们没有引入引用计数这一个概念，所以这里必须要考虑一个BUF
                // 归属到其他handle的情况，我们暂不考虑这种情况，直接暴力写回
                write_block(handle_head->bh->b_page1->buf,handle_head->bh->b_page1->cur,fat_info.BPB.attr.sectors_per_cluster);
            }else{
                write_block(handle_head->bh->b_page1->buf,handle_head->bh->b_page1->cur,fat_info.BPB.attr.sectors_per_cluster);
            }

            write_count+=fat_info.BPB.attr.sectors_per_cluster;
        }

    }

    
    // 在做完checkpoint以后该transaction就完全失效了，那么我们就需要
    // 回收这一块日志空间，删除该transaction相关的所有handle
    handle_head = transaction->t_checkpoint_list;

    while(handle_head!=NULL){
        // 这里要释放内存的空间
        handle_ano = handle_head;
        kfree(handle_ano->bh);
        kfree(handle_ano);
        handle_head=handle_head->h_tnext;
    }

    // 改变transaction的状态
    transaction->t_state = T_FINISHED;

    // 修改日志中空闲块的数目
    journal->j_free += transaction->t_block_num;
    // 修改日志中的尾部结构，这里还是要处理环形的结构
    journal->j_tail = journal_pointer_move(journal->j_tail,journal,transaction->t_phys_block_num);
    return write_count;
}

// 将某个handle从transaction上删除
void journal_erase_handle(transaction_t* transaction, handle_t* handle){
    if(handle->h_transaction!=transaction){
        printk("erase handle error! transaction not equal\n");
        return;
    }
    if(transaction==NULL){
        printk("erase handle error! transaction is NULL\n");
        return;
    }
    if(handle==NULL){
        printk("erase handle error! handle is NULL\n");
        return;
    }

    // 改变原来handle所在的checkpoint链表的链接
    // 先判断这个handle是不是链表头
    // 对此进行不同方式的处理
    if(handle->h_cpprev==NULL){

        // 再是要判断这个handle是不是最后一个handle 
        // 对此进行不同方式的处理
        if(handle->h_cpnext==NULL){
            transaction->t_buffers = NULL;
        }
        else{
            transaction->t_buffers = handle->h_cpnext;
            handle->h_cpnext->h_cpprev = NULL;
        }
    }
    else{
        if(handle->h_cpnext==NULL){
            handle->h_cpprev->h_cpnext = NULL;
        }
        else{
            handle->h_cpprev->h_cpnext = handle->h_cpnext;
            handle->h_cpnext->h_cpprev = handle->h_cpprev;
        }
    }

    handle->h_cpnext = NULL;
    handle->h_cpprev = NULL;

    transaction->t_handle_count--;
    if(handle->bh!=NULL)
        kfree(handle->bh);
    kfree(handle);
}