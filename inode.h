#ifndef __MSFS__INODE__H
#define __MSFS__INODE__H
#include "msfs_info.h"
#include <linux/buffer_head.h>

struct buffer_head *msfs_update_inode(struct inode * inode);
struct msfs_inode * msfs_raw_inode(struct super_block *sb, ino_t ino, struct buffer_head **bh);

int msfs_new_block(struct super_block *sb);
int msfs_free_block(struct super_block *sb, int block);
unsigned long msfs_count_free_blocks(struct super_block *sb);

struct inode *msfs_iget(struct super_block *sb, unsigned long ino);
void msfs_set_inode(struct inode *inode, dev_t rdev);

int msfs_truncate(struct inode *inode);
int msfs_free_inode(struct inode *inode);
struct inode *msfs_new_inode(const struct inode *dir, umode_t mode, int *error);
int msfs_clear_inode(struct inode *inode);
unsigned long msfs_count_free_inodes(struct super_block *sb);


ino_t msfs_inode_by_name(struct dentry *dentry);
struct msfs_dir_entry *msfs_find_entry(struct dentry *dentry, struct buffer_head **bh);
int msfs_delete_entry(struct msfs_dir_entry *de, struct buffer_head *bh);

struct page * dir_get_page(struct inode *dir, unsigned long n);
void dir_put_page(struct page *page);

int msfs_find_first_zero_bit(const void *vaddr, unsigned int size);
void msfs_set_bit(int nr, void *addr);
void msfs_clear_bit(int nr, void *addr);


#endif
