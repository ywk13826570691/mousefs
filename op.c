#include "inode.h"
#include "msfs_info.h"

static int msfs_setattr(struct dentry *dentry, struct iattr *attr)
{
    struct inode *inode = dentry->d_inode;
    int error;

    error = inode_change_ok(inode, attr);
    if (error)
        return error;

    if ((attr->ia_valid & ATTR_SIZE) &&
        attr->ia_size != i_size_read(inode)) {
        error = inode_newsize_ok(inode, attr->ia_size);
        if (error)
            return error;

        truncate_setsize(inode, attr->ia_size);
        msfs_truncate(inode);
    }

    setattr_copy(inode, attr);
    mark_inode_dirty(inode);
    return 0;
}

int msfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
    struct super_block *sb = dentry->d_sb;
    generic_fillattr(dentry->d_inode, stat);

    stat->blocks = (sb->s_blocksize / 512) * (stat->size / MSFS_BLOCK_SIZE);
    stat->blksize = 512;
    return 0;
}

const struct inode_operations msfs_file_inode_operations = {
    .setattr	= msfs_setattr,
    .getattr	= msfs_getattr,
};

const struct file_operations msfs_file_operations = {
    .llseek		= generic_file_llseek,
    .read		= do_sync_read,
    .aio_read	= generic_file_aio_read,
    .write		= do_sync_write,
    .aio_write	= generic_file_aio_write,
    .mmap		= generic_file_mmap,
    .fsync		= generic_file_fsync,
    .splice_read	= generic_file_splice_read,
};

static int msfs_get_block(struct inode *inode, sector_t block,
            struct buffer_head *bh_result, int create)
{
    int err = -EIO;
    int free_block = 0;
    struct msfs_inode_info *m_inode = msfs_i(inode);

    if (block >= 10)
    {
        return err;
    }

    if (m_inode->mfs_inode.i_zone[block] == 0 && !create)
    {
        return err;
    }

    if (m_inode->mfs_inode.i_zone[block] == 0)
    {
        free_block = msfs_new_block(inode->i_sb);
        if (!free_block)
        {
            printk("no blocks to get\n");
            return err;
        }
        m_inode->mfs_inode.i_zone[block] = free_block;
        mark_inode_dirty(inode);
    }
    else
    {
        free_block = m_inode->mfs_inode.i_zone[block];
    }

    map_bh(bh_result, inode->i_sb, free_block);

    return 0;
}
static int msfs_readpage(struct file *file, struct page *page)
{
    return block_read_full_page(page, msfs_get_block);
}


static int msfs_writepage(struct page *page, struct writeback_control *wbc)
{
    return block_write_full_page(page, msfs_get_block, wbc);
}

static void msfs_write_failed(struct address_space *mapping, loff_t to)
{
    struct inode *inode = mapping->host;

    if (to > inode->i_size) {
        truncate_pagecache(inode, to, inode->i_size);
        msfs_truncate(inode);
    }
}

static int msfs_write_begin(struct file *file, struct address_space *mapping,
            loff_t pos, unsigned len, unsigned flags,
            struct page **pagep, void **fsdata)
{
    int ret;

    ret = block_write_begin(mapping, pos, len, flags, pagep,
                msfs_get_block);
    if (unlikely(ret))
        msfs_write_failed(mapping, pos + len);

    return ret;
}

static sector_t msfs_bmap(struct address_space *mapping, sector_t block)
{
    return generic_block_bmap(mapping, block, msfs_get_block);
}

const struct address_space_operations msfs_aops = {
    .readpage = msfs_readpage,
    .writepage = msfs_writepage,
    .write_begin = msfs_write_begin,
    .write_end = generic_write_end,
    .bmap = msfs_bmap,
};

int msfs_add_link(struct dentry *dentry, struct inode *inode)
{
    struct inode *dir = dentry->d_parent->d_inode;
    const char * name = dentry->d_name.name;
    int namelen = dentry->d_name.len;
    struct super_block * sb = dir->i_sb;
    struct msfs_dir_entry *de = NULL;
    __u32 inumber, i = 0, j = 0, dir_size = sizeof(struct msfs_dir_entry);
    struct buffer_head *bh_block;
    int free_block = 0;
    struct msfs_inode_info *si = msfs_i(dir);

    inumber = MSFS_BLOCK_SIZE / dir_size;

    for (i = 0 ; i < 10; i++)
    {
        if (si->mfs_inode.i_zone[i])
        {
            bh_block = sb_bread(sb, si->mfs_inode.i_zone[i]);
            de = (struct msfs_dir_entry *)bh_block->b_data;

            for (j = 0; j < inumber; j++)
            {
                if (de->inode == 0)
                {
                    goto out;
                }
                if (!strcmp(name, de->name))
                {
                    brelse(bh_block);
                    return -EEXIST;
                }
                de++;
            }
        }
        else
        {
            free_block = msfs_new_block(dir->i_sb);
            if (free_block)
            {
                si->mfs_inode.i_zone[i] = free_block;
                bh_block = sb_bread(sb, si->mfs_inode.i_zone[i]);
                de = (struct msfs_dir_entry *)bh_block->b_data;
                goto out;
            }
        }
    }
out:
    if (de)
    {
        de->inode = inode->i_ino;
        memset(de->name, 0, MSFS_FILENAME_MAX_LEN);
        memcpy(de->name, name, namelen);
        i_size_write(dir, dir->i_size + sizeof(struct msfs_dir_entry));
        mark_buffer_dirty(bh_block);
        mark_inode_dirty(dir);
        return 0;
    }
    return -ENODEV;
}

static int add_nondir(struct dentry *dentry, struct inode *inode)
{
    int err = msfs_add_link(dentry, inode);
    if (!err) {
        d_instantiate(dentry, inode);
        return 0;
    }
    //inode_dec_link_count(inode);
    iput(inode);
    return err;
}

static int msfs_mknod(struct inode * dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
    int error = -EINVAL;
    struct inode *inode;

    if (!old_valid_dev(rdev))
        return -EINVAL;

    inode = msfs_new_inode(dir, mode, &error);

    if (inode) {
        msfs_set_inode(inode, rdev);
        mark_inode_dirty(inode);
        error = add_nondir(dentry, inode);
    }
    return error;
}

static int msfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
        bool excl)
{
    return msfs_mknod(dir, dentry, mode, 0);
}

static struct dentry *msfs_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags)
{
    struct inode * inode = NULL;
    ino_t ino;

    if (dentry->d_name.len > MSFS_FILENAME_MAX_LEN)
        return ERR_PTR(-ENAMETOOLONG);
    ino = msfs_inode_by_name(dentry);

    if (ino)
    {
        inode = msfs_iget(dir->i_sb, ino);
        if (IS_ERR(inode))
            return ERR_CAST(inode);
    }
    d_add(dentry, inode);
    return NULL;
}

static int msfs_link(struct dentry * old_dentry, struct inode * dir,
    struct dentry *dentry)
{
    struct inode *inode = old_dentry->d_inode;

    inode->i_ctime = CURRENT_TIME_SEC;
    inode_inc_link_count(inode);
    ihold(inode);
    return add_nondir(dentry, inode);
}

static int msfs_unlink(struct inode * dir, struct dentry *dentry)
{
    int err = -ENOENT;
    struct inode * inode = dentry->d_inode;
    struct msfs_dir_entry * de;
    struct buffer_head *bh;

    de = msfs_find_entry(dentry, &bh);
    if (!de)
        goto end_unlink;

    err = msfs_delete_entry(de, bh);
    if (err)
        goto end_unlink;
    i_size_write(dentry->d_parent->d_inode, dentry->d_parent->d_inode->i_size - sizeof (struct msfs_dir_entry));
    mark_buffer_dirty(bh);

    inode->i_ctime = dir->i_ctime;
    if (S_ISLNK(inode->i_mode))
        inode_dec_link_count(inode);
    mark_inode_dirty(inode);
    mark_inode_dirty(dir);
end_unlink:
    return err;
}

static int msfs_symlink(struct inode * dir, struct dentry *dentry,
      const char * symname)
{
    int err = -ENAMETOOLONG;
    int i = strlen(symname)+1;
    struct inode * inode;

    if (i > MSFS_FILENAME_MAX_LEN)
        goto out;

    inode = msfs_new_inode(dir, S_IFLNK | 0777, &err);
    if (!inode)
        goto out;

    msfs_set_inode(inode, 0);
    err = page_symlink(inode, symname, i);
    if (err)
        goto out_fail;

    err = add_nondir(dentry, inode);
out:
    return err;

out_fail:
    inode_dec_link_count(inode);
    iput(inode);
    goto out;
}

int msfs_make_empty(struct inode *inode, struct inode *dir)
{
    int err = 0, free_block_num = 0;
    struct msfs_inode_info *ms_i_info = msfs_i(inode);
    struct buffer_head *bh;
    struct msfs_dir_entry *de;
    if (ms_i_info->mfs_inode.i_zone[0] == 0)
    {
       free_block_num = msfs_new_block(inode->i_sb);
    }

    if (!free_block_num)
    {
        return -ENOMEM;
    }

    ms_i_info->mfs_inode.i_zone[0] = free_block_num;

    bh = sb_bread(inode->i_sb, free_block_num);

    de = (struct msfs_dir_entry *)bh->b_data;

    de->inode = inode->i_ino;
    strcpy(de->name, ".");
    de++;
    de->inode = dir->i_ino;
    strcpy(de->name, "..");

    i_size_write(inode, sizeof (struct msfs_dir_entry)*2);

    mark_buffer_dirty(bh);
    mark_inode_dirty(inode);

    return err;
}

static int msfs_mkdir(struct inode * dir, struct dentry *dentry, umode_t mode)
{
    struct inode * inode;
    int err;

    //inode_inc_link_count(dir);

    inode = msfs_new_inode(dir, S_IFDIR | mode, &err);
    if (!inode)
        goto out_dir;

    msfs_set_inode(inode, 0);

    //inode_inc_link_count(inode);

    err = msfs_make_empty(inode, dir);
    if (err)
        goto out_fail;

    err = msfs_add_link(dentry, inode);
    if (err)
        goto out_fail;

    d_instantiate(dentry, inode);
out:
    return err;

out_fail:
    //inode_dec_link_count(inode);
    //inode_dec_link_count(inode);
    iput(inode);
out_dir:
    //inode_dec_link_count(dir);
    goto out;
}

static int msfs_rmdir(struct inode * dir, struct dentry *dentry)
{
    int err = -ENOTEMPTY;
    err = msfs_unlink(dir, dentry);
    return err;
}


struct msfs_dir_entry * msfs_dotdot (struct inode *dir, struct buffer_head **bh)
{
    struct msfs_dir_entry *de = NULL;
    struct msfs_inode_info *ms_inode = msfs_i(dir);
    struct buffer_head *bh_res;

    bh_res = sb_bread(dir->i_sb, ms_inode->mfs_inode.i_zone[0]);
    de = (struct msfs_dir_entry *)bh_res->b_data;
    *bh = bh_res;
    return (de + 1);
}

static int msfs_rename(struct inode * old_dir, struct dentry *old_dentry,
               struct inode * new_dir, struct dentry *new_dentry)
{
    struct inode * old_inode = old_dentry->d_inode;
    struct inode * new_inode = new_dentry->d_inode;
    struct msfs_dir_entry * dir_de = NULL;
    struct msfs_dir_entry * old_de, *new_de;
    int err = -ENOENT;
    struct buffer_head *bh, *bh_new, *bh_old;

    old_de = msfs_find_entry(old_dentry, &bh);
    if (!old_de)
        return err;

    if (S_ISDIR(old_inode->i_mode)) {
        err = -EIO;
        dir_de = msfs_dotdot(old_inode, &bh_old);
        if (!dir_de)
        {
            brelse(bh);
            return err;
        }
    }

    if (new_inode)
    {
        new_de = msfs_find_entry(new_dentry, &bh_new);

        if (new_de)
        {
            new_de->inode = old_inode->i_ino;
            strcpy(new_de->name, new_dentry->d_name.name);
            mark_buffer_dirty(bh_new);
            //drop_nlink(new_inode);
            //inode_dec_link_count(new_inode);
        }
    }
    else
    {
        msfs_add_link(new_dentry, old_inode);
        //inode_inc_link_count(new_dir);
        if (dir_de)
        {
            // old is a dir we shoud handle ".."
            dir_de->inode = new_dir->i_ino;
            mark_buffer_dirty(bh_old);
        }
    }
    msfs_delete_entry(old_de, bh);
    i_size_write(old_dir, new_dir->i_size - sizeof (struct msfs_dir_entry));
    new_dir->i_atime = CURRENT_TIME;
    mark_inode_dirty(old_dir);
    mark_inode_dirty(new_dir);
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}


const struct inode_operations msfs_dir_inode_operations = {
    .create		= msfs_create,
    .lookup		= msfs_lookup,
    .link		= msfs_link,
    .unlink		= msfs_unlink,
    .symlink	= msfs_symlink,
    .mkdir		= msfs_mkdir,
    .rmdir		= msfs_rmdir,
    .mknod		= msfs_mknod,
    .rename		= msfs_rename,
    .getattr	= msfs_getattr,
};

/*
 * Everytime we readdir the kernel will pass pos to me how cal pos(byte)?
 *
 * if we last time read 5 byte our filp->f_pos must += 5 event your ino is zero
 *
*/

static int msfs_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
    unsigned long pos = filp->f_pos;
    struct inode *inode = file_inode(filp);
    struct msfs_inode_info *msfs_inode_f = msfs_i(inode);
    int i = 0, j = 0, first = 0, offset = 0, index = 0;

    int per_de = MSFS_BLOCK_SIZE / sizeof(struct msfs_dir_entry);
    int block = pos / MSFS_BLOCK_SIZE;
    struct msfs_dir_entry *de;
    struct buffer_head *bh;
    int over;

    if (block > 10)
    {
        return -ENODEV;
    }

    for(i = block; i < 10; i++)
    {
        if (msfs_inode_f->mfs_inode.i_zone[i] != 0)
        {
            bh = sb_bread(inode->i_sb, msfs_inode_f->mfs_inode.i_zone[i]);
            de = (struct msfs_dir_entry *)bh->b_data;
            j = 0;
            if (!first)
            {
                first = 1;
                offset = pos - block * MSFS_BLOCK_SIZE;
                index = offset / sizeof(struct msfs_dir_entry);
                if (index >= per_de)
                {
                    continue;
                }
                else
                {
                    j = index;
                }
                de += j;
            }

            for(; j < per_de; j++)
            {
                if (!de->inode)
                {
                    // even if an null dir wo also must update filp->f_pos
                    filp->f_pos += sizeof(struct msfs_dir_entry);
                    de++;
                    continue;
                }

                filp->f_pos += sizeof(struct msfs_dir_entry);
                over = filldir(dirent, de->name, strnlen(de->name, MSFS_FILENAME_MAX_LEN), filp->f_pos, de->inode,
                        DT_UNKNOWN);
                if (over)
                {
                    brelse(bh);
                    goto out;
                }
                de++;
            }
            brelse(bh);
        }
        else
        {
            return -EIO;
        }
    }
out:
    return 0;
}

const struct file_operations msfs_dir_operations = {
    .llseek		= generic_file_llseek,
    .read		= generic_read_dir,
    .readdir	= msfs_readdir,
    .fsync		= generic_file_fsync,
};


const struct inode_operations msfs_symlink_inode_operations = {
    .readlink	= generic_readlink,
    .follow_link	= page_follow_link_light,
    .put_link	= page_put_link,
    .getattr	= msfs_getattr,
};

