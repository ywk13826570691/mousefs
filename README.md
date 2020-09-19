# mousefs
这是一个简单的linux文件系统叫他mosefs
mousefs文件系统磁盘块是1024字节
文件最大是10k，因为只使用了一级块
编译后生成 drv.ko和msfs.ko
安装此两个驱动后直接mount /dev/msfsblk0 /mnt
会在/mnt目录下看到文件msfs.txt文件 ok
仅供学习和理解linux文件系统和块设备驱动

Linux Simple filesystem mousefs


/*
 * This is an simple filesystem ,mouse filesystem only for learn Linux filesystem
 Ex:
 
--------------------------------------------------------------------------------------------------------------------
MRB 1024 | super block 1024 | imap n*1024 | zmap n*1024 | inodes 1024*8*sizeof(inode) | first data zone 1024 | datazone
---------------------------------------------------------------------------------------------------------------------

Note:
1,imap and zmap we do not use bit, we using byte ,sunch one block 1024 byte, so we only can map 1024;
2, Our index all from 0
3, Inode i_no 0 is using for invalid inode, can not using for an file .....
4, Super block s_firstdatazone is device 1024 block number from 0
5, Super block s_nzones is device has total 1024 block count

An example:

--------------------------------------------------------------------------------------------------------------------
0 | 1 super block | 2 imap | 3 zmap | 4 inode | 5 first data zone for root dir | 6 data | 7 data| 8 data |
---------------------------------------------------------------------------------------------------------------------
Super Block:
s_ninodes:
s_nzones: 9
s_imap_blocks:1
s_zmap_blocks:1
s_firstdatazone:5
This an very simple filesystem
*/
