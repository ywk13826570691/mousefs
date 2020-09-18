#include "inode.h"

static DEFINE_SPINLOCK(bitmap_lock);

extern const struct inode_operations msfs_file_inode_operations;
extern const struct inode_operations msfs_dir_inode_operations;
extern const struct file_operations msfs_file_operations;
extern const struct file_operations msfs_dir_operations;
extern struct inode_operations msfs_symlink_inode_operations;
extern struct address_space_operations msfs_aops;

struct msfs_inode * msfs_raw_inode(struct super_block *sb, ino_t ino, struct buffer_head **bh)
{
	struct msfs_sb_info *ms_sb_info = msfs_sb(sb);
	struct msfs_super_block *ms_sb = ms_sb_info->s_ms;
	int ninodes = ms_sb->s_ninodes;
    struct msfs_inode *p;
    int block;
	
    if (ino + 1 > ninodes || ino == 0) {
        printk("msfs_raw_inode ino to bigger or reading 0 ino\n");
		return NULL;
	}
	
    block = 2 + ms_sb->s_imap_blocks + ms_sb->s_zmap_blocks + ino / MSFS_INODES_PER_BLOCK;
	*bh = sb_bread(sb, block);
	if (!*bh) {
		printk("Unable to read inode block\n");
		return NULL;
	}
	p = (void *)(*bh)->b_data;
    return p + ino % MSFS_INODES_PER_BLOCK;
}

struct buffer_head * msfs_update_inode(struct inode * inode)
{
	struct buffer_head * bh;
	struct msfs_inode * raw_inode;
	struct msfs_inode_info *msfs_inode = msfs_i(inode);
	int i;

	raw_inode = msfs_raw_inode(inode->i_sb, inode->i_ino, &bh);
	if (!raw_inode)
        return NULL;
	raw_inode->i_mode = inode->i_mode;
	raw_inode->i_uid = fs_high2lowuid(i_uid_read(inode));
	raw_inode->i_gid = fs_high2lowgid(i_gid_read(inode));

	raw_inode->i_size = inode->i_size;
	raw_inode->i_mtime = inode->i_mtime.tv_sec;
	if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
		raw_inode->r_dev = old_encode_dev(inode->i_rdev);
	else for (i = 0; i < 10; i++)
		raw_inode->i_zone[i] = msfs_inode->mfs_inode.i_zone[i];
	mark_buffer_dirty(bh);
    mark_inode_dirty(inode);
    return bh;
}


int msfs_new_block(struct super_block *sb)
{
    struct msfs_sb_info *sbi = msfs_sb(sb);
    int bits_per_zone = MSFS_BLOCK_SIZE;
    int i;

    for (i = 0; i < sbi->s_ms->s_zmap_blocks; i++) {
        struct buffer_head *bh = sbi->s_zmap[i];
        int j;

        spin_lock(&bitmap_lock);
        j = msfs_find_first_zero_bit(bh->b_data, bits_per_zone);

        if (j < bits_per_zone && j >= 0) {
            msfs_set_bit(j, bh->b_data);
            spin_unlock(&bitmap_lock);
            mark_buffer_dirty(bh);
            j += i * bits_per_zone + sbi->s_ms->s_firstdatazone + 1;
            if (j < sbi->s_ms->s_firstdatazone || j + 1 > sbi->s_ms->s_nzones)
                break;
            return j;
        }
        spin_unlock(&bitmap_lock);
    }
    return 0;
}

int msfs_free_block(struct super_block *sb, int block)
{
    struct msfs_sb_info *sbi = msfs_sb(sb);
    struct buffer_head *bh;
    unsigned long bit, zone, zmap_block;
    int ret = -ENODEV;
    if (block < sbi->s_ms->s_firstdatazone || block >= sbi->s_ms->s_nzones) {
        printk("Trying to free block not in datazone\n");
        return ret;
    }
    zone = block - sbi->s_ms->s_firstdatazone;

    zmap_block = zone / MSFS_BLOCK_SIZE;

    bit = zone - zmap_block * MSFS_BLOCK_SIZE - 1;
    bh = sbi->s_zmap[zmap_block];
    spin_lock(&bitmap_lock);
    msfs_clear_bit(bit, bh->b_data);
    spin_unlock(&bitmap_lock);
    mark_buffer_dirty(bh);
    return 0;
}
struct inode *msfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct buffer_head * bh;
    struct msfs_inode * raw_inode;
    struct msfs_inode_info *msfs_info;
    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return inode;

    msfs_info = msfs_i(inode);
    raw_inode = msfs_raw_inode(inode->i_sb, ino, &bh);
    if (!raw_inode)
    {
        iget_failed(inode);
        return ERR_PTR(-EIO);
    }

    inode->i_mode = raw_inode->i_mode;
    i_uid_write(inode, raw_inode->i_uid);
    i_gid_write(inode, raw_inode->i_gid);
    set_nlink(inode, raw_inode->i_nlinks);
    inode->i_size = raw_inode->i_size;
    inode->i_mtime.tv_sec = raw_inode->i_mtime;
    inode->i_atime.tv_sec = raw_inode->i_atime;
    inode->i_ctime.tv_sec = raw_inode->i_ctime;
    inode->i_blocks = 0;
    inode->i_ino = ino;
    msfs_info->mfs_inode =  *raw_inode;
    msfs_set_inode(inode, old_decode_dev(raw_inode->r_dev));
    brelse(bh);
    unlock_new_inode(inode);
    return inode;
}

unsigned long msfs_count_free_blocks(struct super_block *sb)
{
    struct msfs_sb_info *m_sbi = msfs_sb(sb);
    int i = 0, j = 0, count = 0, blocks = 0;
    char *p;
    struct buffer_head *bh;
    int all_blocks = m_sbi->s_ms->s_nzones - m_sbi->s_ms->s_firstdatazone - 1;
    for (i = 0; i < m_sbi->s_ms->s_zmap_blocks; i++)
    {
        bh = m_sbi->s_zmap[i];
        p = bh->b_data;
        for (j = 0; j < MSFS_BLOCK_SIZE; j++)
        {
            blocks++;
            if (blocks <= all_blocks)
            {
                if (p[j] == 0)
                {
                    count++;
                }
            }
            else
            {
                break;
            }
        }
    }
    return count;
}

//frree inode data block
int msfs_truncate(struct inode *inode)
{
    struct msfs_inode_info *ms_info = msfs_i(inode);
    struct msfs_sb_info *m_sb = msfs_sb(inode->i_sb);
    int i = 0;
    for(i = 0; i < 10; i++)
    {
        if (ms_info->mfs_inode.i_zone[i] > m_sb->s_ms->s_firstdatazone)
        {
            msfs_free_block(inode->i_sb, ms_info->mfs_inode.i_zone[i]);
            ms_info->mfs_inode.i_zone[i] = 0;
        }
    }

    inode->i_mtime = inode->i_ctime = inode->i_atime = CURRENT_TIME_SEC;
    mark_inode_dirty(inode);
    return 0;
}

//free imap
int msfs_free_inode(struct inode *inode)
{
    struct msfs_sb_info *sbi = msfs_sb(inode->i_sb);
    struct buffer_head *bh;
    unsigned long ino, bit, map_block;

    ino = inode->i_ino;
    if (ino < 2 || ino + 1 > sbi->s_ms->s_ninodes) {
        printk("msfs_free_inode: inode 1 or nonexistent inode\n");
        return -ENODEV;
    }

    map_block = (ino) / MSFS_BLOCK_SIZE;
    if (map_block + 1 > sbi->s_ms->s_imap_blocks) {
        printk("msfs_free_inode: nonexistent imap in superblock %ld %d\n", ino, sbi->s_ms->s_imap_blocks);
        return -ENODEV;
    }

    msfs_clear_inode(inode);	/* clear on-disk copy */

    bh = sbi->s_imap[map_block];
    spin_lock(&bitmap_lock);
    bit = ino - map_block * MSFS_BLOCK_SIZE;
    msfs_clear_bit(bit, bh->b_data);
    spin_unlock(&bitmap_lock);
    mark_buffer_dirty(bh);
    return 0;
}

struct inode *msfs_new_inode(const struct inode *dir, umode_t mode, int *error)
{
    struct super_block *sb = dir->i_sb;
    struct msfs_sb_info *sbi = msfs_sb(sb);
    struct inode *inode = new_inode(sb);
    struct buffer_head * bh;
    int bits_per_zone = MSFS_BLOCK_SIZE;
    unsigned long j;
    int i;

    if (!inode) {
        *error = -ENOMEM;
        return NULL;
    }

    j = bits_per_zone;
    bh = NULL;
    *error = -ENOSPC;
    spin_lock(&bitmap_lock);
    for (i = 0; i < sbi->s_ms->s_imap_blocks; i++) {
        bh = sbi->s_imap[i];
        j = msfs_find_first_zero_bit(bh->b_data, bits_per_zone);
        if (j < bits_per_zone)
            break;
    }

    if (!bh || j >= bits_per_zone) {
        printk("Not any more inode for using\n");
        spin_unlock(&bitmap_lock);
        iput(inode);
        return NULL;
    }

    msfs_set_bit(j, bh->b_data);

    spin_unlock(&bitmap_lock);

    j += i * bits_per_zone;
    if (j <= 0 || j + 1 > sbi->s_ms->s_ninodes) {
        iput(inode);
        return NULL;
    }
    mark_buffer_dirty(bh);

    inode_init_owner(inode, dir, mode);
    inode->i_ino = j;

    inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
    inode->i_blocks = 0;
    for(i = 0; i < 10; i++)
    {
        msfs_i(inode)->mfs_inode.i_zone[i] = 0;
    }
    insert_inode_hash(inode);
    mark_inode_dirty(inode);
    *error = 0;
    return inode;
}

// clear inode disk info
int msfs_clear_inode(struct inode *inode)
{
    struct buffer_head *bh = NULL;

    struct msfs_inode *raw_inode;
    raw_inode = msfs_raw_inode(inode->i_sb, inode->i_ino, &bh);
    if (raw_inode) {
        raw_inode->i_nlinks = 0;
        raw_inode->i_mode = 0;
        memset(raw_inode, 0, sizeof (struct msfs_inode));
    }
    if (bh) {
        mark_buffer_dirty(bh);
        brelse (bh);
    }

    return 0;
}

void msfs_set_inode(struct inode *inode, dev_t rdev)
{
    if (S_ISREG(inode->i_mode)) {
        inode->i_op = &msfs_file_inode_operations;
        inode->i_fop = &msfs_file_operations;
        inode->i_mapping->a_ops = &msfs_aops;
    } else if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &msfs_dir_inode_operations;
        inode->i_fop = &msfs_dir_operations;
        inode->i_mapping->a_ops = &msfs_aops;
    } else if (S_ISLNK(inode->i_mode)) {
        inode->i_op = &msfs_symlink_inode_operations;
        inode->i_mapping->a_ops = &msfs_aops;
    } else
        init_special_inode(inode, inode->i_mode, rdev);
}

unsigned long msfs_count_free_inodes(struct super_block *sb)
{
    struct msfs_sb_info *m_sbi = msfs_sb(sb);
    int i = 0, j = 0, count = 0, inodes = 0;
    char *p;
    struct buffer_head *bh;
    int all_inodes = m_sbi->s_ms->s_ninodes;
    for (i = 0; i < m_sbi->s_ms->s_imap_blocks; i++)
    {
        bh = m_sbi->s_imap[i];
        p = bh->b_data;
        for (j = 0; j < MSFS_BLOCK_SIZE; j++)
        {
            inodes++;
            if (inodes <= all_inodes) {
                if (p[j] == 0) {
                    count++;
                }
            } else {
                break;
            }
        }
    }
    return count;
}

ino_t msfs_inode_by_name(struct dentry *dentry)
{
    struct buffer_head *bh;
    struct msfs_dir_entry *de = msfs_find_entry(dentry, &bh);
    if (de)
    {
        return de->inode;
    }
    return 0;

}

struct msfs_dir_entry *msfs_find_entry(struct dentry *dentry, struct buffer_head **bh)
{
    const unsigned char * name = dentry->d_name.name;
    struct inode * dir = dentry->d_parent->d_inode;
    struct super_block * sb = dir->i_sb;
    struct buffer_head *bh_res, *bh_block;
    struct msfs_dir_entry *de, *p_de;
    __u32 inumber, i = 0, j = 0, dir_size = sizeof(struct msfs_dir_entry);
    struct msfs_inode *raw_inode = msfs_raw_inode(sb, dir->i_ino, &bh_res);
    struct msfs_inode_info *si = msfs_i(dentry->d_parent->d_inode);

    if (!raw_inode)
    {
        return NULL;
    }
    for (i = 0 ; i < 10; i++)
    {
        if (si->mfs_inode.i_zone[i])
        {
            bh_block = sb_bread(sb, si->mfs_inode.i_zone[i]);
            de = (struct msfs_dir_entry *)bh_block->b_data;
            inumber = MSFS_BLOCK_SIZE / dir_size;

            for (j = 0; j < inumber; j++)
            {
                p_de = de + j;
                if (!strcmp(name, p_de->name))
                {
                    *bh = bh_block;
                    return p_de;
                }
            }
        }
    }
    return NULL;

}

int msfs_delete_entry(struct msfs_dir_entry *de, struct buffer_head *bh)
{
    struct msfs_dir_entry *de_p = (struct msfs_dir_entry *)bh->b_data;
    __u32 inumber, i = 0, dir_size = sizeof(struct msfs_dir_entry);
    if (!de || !bh)
    {
        return -EIO;
    }

    inumber = MSFS_BLOCK_SIZE / dir_size;


    for (i =0 ; i < inumber; i++)
    {
        if (strcmp(de->name, de_p->name) == 0)
        {
            de_p->inode = 0;
            memset(de_p->name, 0, MSFS_FILENAME_MAX_LEN);
            mark_buffer_dirty(bh);
            return 0;
        }
        de_p++;
    }
    return -EIO;
}


struct page * dir_get_page(struct inode *dir, unsigned long n)
{
    struct address_space *mapping = dir->i_mapping;
    struct page *page = read_mapping_page(mapping, n, NULL);
    if (!IS_ERR(page))
        kmap(page);
    return page;
}

void dir_put_page(struct page *page)
{
    kunmap(page);
    page_cache_release(page);
}

#if 0
inline int msfs_find_first_zero_bit(const void *vaddr, unsigned size)
{
    const unsigned short *p = vaddr, *addr = vaddr;
    unsigned short num;

    if (!size)
        return 0;

    size >>= 4;
    while (*p++ == 0xffff) {
        if (--size == 0)
            return (p - addr) << 4;
    }

    num = *--p;
    return ((p - addr) << 4) + ffz(num);
}
#endif

inline int msfs_find_first_zero_bit(const void *vaddr, unsigned int size)
{
    const char *p = vaddr;
    int ret = -ENODEV;
    unsigned int i = 0;

    if (!size || vaddr == NULL)
        return ret;

    for(i = 0; i < size; i++)
    {
        if (p[i] == 0)
        {
            ret = i;
            break;
        }
    }
    return ret;
}

void msfs_set_bit(int nr, void *addr)
{
    char *p = addr;
    p[nr] = 1;
}

void msfs_clear_bit(int nr, void *addr)
{
    char *p = addr;
    p[nr] = 0;
}
