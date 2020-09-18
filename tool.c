
#ifdef CONFIG_MSFS_TOOL
#include <stdio.h>
#endif
#include <linux/string.h>
#include "msfs.h"
#include <linux/printk.h>

#ifdef CONFIG_MSFS_TOOL
char zone[1024*1024*2] = { 0 };
#endif
int setup_msfs_filesystem(char *p, int size)
{

    int all_zones = size / MSFS_BLOCK_SIZE;
    int inode_count = MSFS_BLOCK_SIZE;
    int inode_map_blocks = inode_count / MSFS_BLOCK_SIZE;
    int inode_blocks =  (inode_count * sizeof (struct msfs_inode)) / MSFS_BLOCK_SIZE;
    int zone_map_blocks = all_zones / MSFS_BLOCK_SIZE;

    char *p_imap_block;
    char *p_root_inode;
    char *p_first_data_zone;
    char *p_first_zone_block;
    char *p_zone_map_block;

    struct msfs_inode root_inode;
    struct msfs_dir_entry *de;
    struct msfs_dir_entry decpy;
    struct msfs_inode *msfs_txt;

    int i = 0;
    char *p_sp = (p + 1024);
    struct msfs_super_block sp;
    memset(p, 0, size);


    sp.s_nzones = all_zones;
    sp.s_magic = MSFS_MAGIG;
    sp.s_ninodes = inode_count;
    sp.s_imap_blocks = inode_map_blocks;
    sp.s_zmap_blocks = zone_map_blocks;
    sp.s_firstdatazone = 2 + inode_map_blocks + zone_map_blocks + inode_blocks;


    memcpy(p_sp, &sp, sizeof(struct msfs_super_block)); //ok our superblock

    //create root inode

    root_inode.i_mode = 0040000;
    root_inode.i_size = sizeof(struct msfs_dir_entry)*3;//".", "..", "msfs.txt"

    p_root_inode = p + (2 + inode_map_blocks + zone_map_blocks)*MSFS_BLOCK_SIZE + sizeof (struct msfs_inode)*MSFS_ROOT_INO;
    for (i = 0; i < 10; i++)
    {
        root_inode.i_zone[i] = 0;
    }

    root_inode.i_zone[0] = sp.s_firstdatazone;

    memcpy(p_root_inode, &root_inode, sizeof (struct msfs_inode));

    //root inode has used and zero inode alse used
    p_imap_block = p + 2*MSFS_BLOCK_SIZE;
    *p_imap_block = 1;
    *(p_imap_block + 1) = 1;

    // now we create a file in root dir msfs.txt
    p_first_data_zone = p + (sp.s_firstdatazone)*MSFS_BLOCK_SIZE;

    de = (struct msfs_dir_entry *)p_first_data_zone;
    memset(&decpy, 0, sizeof (struct msfs_dir_entry));
    memcpy(de->name, ".", strlen("."));
    de->inode = MSFS_ROOT_INO;
    de+=1;
    memcpy(de->name, "..", strlen(".."));
    de->inode = MSFS_ROOT_INO;

    de += 1;
    memcpy(de->name, "msfs.txt", strlen("msfs.txt"));
    de->inode = 2;

    *(p_imap_block + 2) = 1;

    msfs_txt = (struct msfs_inode *)(p_root_inode + sizeof (struct msfs_inode));

    msfs_txt->i_mode = 0100000;

    for (i = 0; i < 10; i++)
    {
        msfs_txt->i_zone[i] = 0;
    }

    msfs_txt->i_zone[0] = sp.s_firstdatazone + 1;

    p_first_zone_block = p + (sp.s_firstdatazone + 1)*1024;
    memcpy(p_first_zone_block, "hello msfs\n", strlen("hello msfs\n"));

    msfs_txt->i_size = strlen("hello msfs\n");

    p_zone_map_block = p + (2 + inode_map_blocks)*MSFS_BLOCK_SIZE;
    *p_zone_map_block = 1;
    return 0;

}

#ifdef CONFIG_MSFS_TOOL
void scan_msfs_filesystem(char *p, int size)
{
    struct msfs_super_block *sp = (struct msfs_super_block *)(p + 1024);
    printf("-----:%d\n", sizeof (struct msfs_inode));
    printf("msfs block info:%d %d %d %d %d %d\n",sp->s_magic,
           sp->s_nzones,sp->s_ninodes, sp->s_imap_blocks, sp->s_zmap_blocks, sp->s_firstdatazone);

    char *p_root_inode = p + (2 + sp->s_imap_blocks + sp->s_zmap_blocks)*1024 + sizeof (struct msfs_inode)*MSFS_ROOT_INO;
    struct msfs_inode *root_inode  = (struct msfs_inode *)p_root_inode;
    printf("root inode :%d\n", root_inode->i_zone[0]);
    struct msfs_inode *msfs_file = (p_root_inode + sizeof (struct msfs_inode));
    printf("root inode :%d\n", msfs_file->i_zone[0]);

    char *p_imap_block = p + 2*1024;
    int i = 0;
    for ( i =0 ;i < 1024; i++)
    {
        printf(" %d ", p_imap_block[i]);
    }
    printf("\n");
    char *p_zmap_block = p + (2 + sp->s_imap_blocks)*1024;
    for ( i =0 ;i < sp->s_zmap_blocks*1024; i++)
    {
        printf(" %d ", p_zmap_block[i]);
    }
    //ls root dir
    printf("\n");
    char *p_first_data_zone = p + sp->s_firstdatazone*1024;
    int j = 10224 / (sizeof (struct msfs_dir_entry));
    struct msfs_dir_entry *de1 = (struct msfs_dir_entry *)p_first_data_zone;
    for (i = 0; i < j; i++)
    {
        struct msfs_dir_entry *de = de1 + i;
        if (de->inode)
        {
            printf("%s %d %d\n", de->name, de->inode, i);
            if (de->inode == 2)
            {
                int m = de->inode / (1024/sizeof (struct msfs_inode));
                int k = de->inode % (1024/sizeof (struct msfs_inode));
                struct msfs_inode *file_node = ( struct msfs_inode *)((p_root_inode + m*1024) + (k-1)*sizeof(struct msfs_inode));
                printf("%d %s\n",file_node->i_zone[0], (p + file_node->i_zone[0]*1024));
            }
        }
    }
    //printf("\n");
}

int main(int argc, char **argv)
{
    setup_msfs_filesystem(zone, 1024*1024*2);
    scan_msfs_filesystem(zone, 1024*1024*2);
    return 0;
}
#endif
