#include <linux/module.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/highuid.h>
#include <linux/vfs.h>
#include <linux/writeback.h>
#include "msfs.h"
#include "msfs_info.h"
#include "inode.h"


static struct kmem_cache * msfs_inode_cachep;


static void init_once(void *foo)
{
	struct msfs_inode_info *ei = (struct msfs_inode_info *) foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	msfs_inode_cachep = kmem_cache_create("msfs_inode_cache",
					     sizeof(struct msfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (msfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(msfs_inode_cachep);
}

static struct inode *msfs_alloc_inode(struct super_block *sb)
{
	struct msfs_inode_info *ei;
	ei = (struct msfs_inode_info *)kmem_cache_alloc(msfs_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	return &ei->vfs_inode;
}


static void msfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(msfs_inode_cachep, msfs_i(inode));
}

static void msfs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, msfs_i_callback);
}

static int msfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	int err = 0;
	struct buffer_head *bh;

    bh = msfs_update_inode(inode);
	if (!bh)
		return -EIO;
	if (wbc->sync_mode == WB_SYNC_ALL && buffer_dirty(bh)) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk("IO error syncing msfs inode [%s:%08lx]\n",
				inode->i_sb->s_id, inode->i_ino);
			err = -EIO;
		}
	}
	brelse (bh);
	return err;
}

static void msfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
    if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode))) {
        return;
    }
	if (!inode->i_nlink) {
		inode->i_size = 0;
        msfs_truncate(inode);
	}
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	if (!inode->i_nlink)
        msfs_free_inode(inode);
}

static void msfs_put_super(struct super_block *sb)
{
    int i;
    struct msfs_sb_info *sbi = msfs_sb(sb);

    if (!(sb->s_flags & MS_RDONLY)) {
        mark_buffer_dirty(sbi->s_sbh);
    }
    for (i = 0; i < sbi->s_ms->s_imap_blocks; i++) {
        brelse(sbi->s_imap[i]);
    }
    for (i = 0; i < sbi->s_ms->s_zmap_blocks; i++) {
        brelse(sbi->s_zmap[i]);
    }
    brelse (sbi->s_sbh);
    kfree(sbi->s_imap);
    sb->s_fs_info = NULL;
    kfree(sbi);
}

static int msfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
    struct super_block *sb = dentry->d_sb;
    struct msfs_sb_info *sbi = msfs_sb(sb);
    u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
    buf->f_type = sb->s_magic;
    buf->f_bsize = sb->s_blocksize;
    buf->f_blocks = (sbi->s_ms->s_nzones - sbi->s_ms->s_firstdatazone - 1);
    buf->f_bfree = msfs_count_free_blocks(sb);
    buf->f_bavail = buf->f_bfree;
    buf->f_files = sbi->s_ms->s_ninodes;
    buf->f_ffree = msfs_count_free_inodes(sb);
    buf->f_namelen = MSFS_FILENAME_MAX_LEN;
    buf->f_fsid.val[0] = (u32)id;
    buf->f_fsid.val[1] = (u32)(id >> 32);
    return 0;
}

static int msfs_remount (struct super_block * sb, int * flags, char * data)
{
    return 0;
}

static const struct super_operations msfs_sops = {
	.alloc_inode	= msfs_alloc_inode,
	.destroy_inode	= msfs_destroy_inode,
	.write_inode	= msfs_write_inode,
	.evict_inode	= msfs_evict_inode,
	.put_super	= msfs_put_super,
	.statfs		= msfs_statfs,
	.remount_fs	= msfs_remount,
};


static int msfs_fill_super(struct super_block *s, void *data, int silent)
{
	struct buffer_head *bh;
	struct buffer_head **map;
	struct msfs_super_block *ms;
	struct inode *root_inode;
	struct msfs_sb_info *sbi;
	int ret = -EINVAL;
	int i , block;
	
	sbi = kzalloc(sizeof(struct msfs_sb_info), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	s->s_fs_info = sbi;
	
	if(!sb_set_blocksize(s, MSFS_BLOCK_SIZE)) {
		goto bad_device;
	}
	bh = sb_bread(s, 1);
	
	if (!bh) {
		goto bad_device;
	}
	
	ms = (struct msfs_super_block *)bh->b_data;
	sbi->s_ms = ms;
	sbi->s_sbh = bh;
#if 0
    printk("--magic---:%d %d %d %d %d %d\n", ms->s_magic, ms->s_nzones, ms->s_ninodes, ms->s_imap_blocks,
           ms->s_zmap_blocks, ms->s_firstdatazone);
#endif
	i = (ms->s_imap_blocks + ms->s_zmap_blocks) * sizeof(bh);

	map = kzalloc(i, GFP_KERNEL);
	
	if (!map) {
		goto bad_map;
	}
	
	sbi->s_imap = &map[0];
	sbi->s_zmap = &map[sbi->s_ms->s_imap_blocks];
	
	block = 2;
	
	for (i=0 ; i < sbi->s_ms->s_imap_blocks ; i++) {
        sbi->s_imap[i] = sb_bread(s, block);
		block++;
	}
	
	for (i=0 ; i < sbi->s_ms->s_zmap_blocks ; i++) {
		sbi->s_zmap[i]=sb_bread(s, block);
		block++;
	}
	
    s->s_op = &msfs_sops;

    root_inode = msfs_iget(s, MSFS_ROOT_INO);

    if (!root_inode)
    {
        printk("get Root inode nll\n");
        goto root_err;
    }

    s->s_root = d_make_root(root_inode);
	
    return 0;
root_err:
    for (i=0 ; i < sbi->s_ms->s_imap_blocks ; i++)
    {
        brelse(sbi->s_imap[i]);
    }

    for (i=0 ; i < sbi->s_ms->s_zmap_blocks ; i++)
    {
        brelse(sbi->s_zmap[i]);
    }
    kfree(map);
bad_map:
	brelse(bh);
bad_device:
	kfree(sbi);
	return ret;
}

static struct dentry *msfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, msfs_fill_super);
}


static struct file_system_type ms_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "msfs",
	.mount		= msfs_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("msfs");

static int __init init_ms_fs(void)
{
	int err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&ms_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	return err;
}

static void __exit exit_ms_fs(void)
{
    unregister_filesystem(&ms_fs_type);
	destroy_inodecache();
}

module_init(init_ms_fs)
module_exit(exit_ms_fs)
MODULE_LICENSE("GPL");

