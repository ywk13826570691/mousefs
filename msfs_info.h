#ifndef __LINUX_MSFS_INFO__H__
#define __LINUX_MSFS_INFO__H__

#include "msfs.h"
#include <linux/fs.h>
#include <linux/pagemap.h>

#define MSFS_INODES_PER_BLOCK ((MSFS_BLOCK_SIZE)/(sizeof(struct msfs_inode)))


struct msfs_inode_info {
	struct msfs_inode mfs_inode;
	struct inode vfs_inode;
};


struct msfs_sb_info {
	struct buffer_head ** s_imap;
	struct buffer_head ** s_zmap;
	struct buffer_head * s_sbh;
	struct msfs_super_block *s_ms;
};



static inline struct msfs_inode_info *msfs_i(struct inode *inode)
{
	return container_of(inode, struct msfs_inode_info, vfs_inode);
}

static inline struct msfs_sb_info *msfs_sb(struct super_block *sb)
{
	return sb->s_fs_info;
}


#endif
