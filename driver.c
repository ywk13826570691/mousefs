#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

#include <linux/sched.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h>   /* kmalloc() */
#include <linux/fs.h>   /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/timer.h>
#include <linux/types.h> /* size_t */
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/hdreg.h> /* HDIO_GETGEO */
#include <linux/kdev_t.h>
#include <linux/vmalloc.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h> /* invalidate_bdev */
#include <linux/bio.h>

MODULE_LICENSE("Vkang BSD/GPL");

extern int setup_msfs_filesystem(char *p, int size);
static int major = 0;

static int sect_size = 512;

static int nsectors = 1024*2*2;
static char buf_dev[1024*1024*2] = { 0 };

/*
* The internal representation of our device.
*/
struct blk_dev{
         int size;                        /* Device size in sectors */
         u8 *data;                        /* The data array */
         struct request_queue *queue;     /* The device request queue */
         struct gendisk *gd;              /* The gendisk structure */
         struct page *p;
};

struct blk_dev *dev;


/*
* Handle an I/O request, in sectors.
*/
static void blk_transfer(struct blk_dev *dev, unsigned long sector,
   unsigned long nsect, char *buffer, int write)
{
    unsigned long offset = sector * sect_size;
    unsigned long nbytes = nsect * sect_size;

    if ((offset + nbytes) > dev->size) {
       printk (KERN_NOTICE "Beyond-end write (%ld %ld)\n", offset, nbytes);
       return;
    }
    if (write)
       memcpy(dev->data + offset, buffer, nbytes);
    else
       memcpy(buffer, dev->data + offset, nbytes);
}

/*
* The simple form of the request function.
*/
static void blk_request(struct request_queue *q)
{
    struct request *req;

    req = blk_fetch_request(q);
    while (req != NULL)
    {
       struct blk_dev *dev = req->rq_disk->private_data;

       blk_transfer(dev, blk_rq_pos(req), blk_rq_cur_sectors(req), req->buffer, rq_data_dir(req));

       if(!__blk_end_request_cur(req, 0))
       {
            req = blk_fetch_request(q);
       }
    }
}

/*
* The device operations structure.
*/
static struct block_device_operations blk_ops = {
.owner            = THIS_MODULE,
};

static int __init blk_init(void)
{
    int err = 0;
    //注册设备块驱动程序
    major = register_blkdev(0, "blk");
    if (major <= 0) {
       printk(KERN_WARNING "blk: unable to get major number\n");
       return -EBUSY;
    }
    dev = kmalloc(sizeof(struct blk_dev), GFP_KERNEL);
    if (dev == NULL)
    {
       err = -ENOMEM;
       goto out_unregister;
    }

#if 0
    dev->p = alloc_pages(GFP_KERNEL, 8);

    if (dev->p == NULL)
    {
        printk("alloc_pages failure\n");
        goto out_free4;
    }
    free_pages((unsigned long)page_address(dev->p), 8);
#endif
    /*
    * Get some memory.
    */
    dev->size = nsectors * sect_size;
    //dev->data = kzalloc(dev->size, GFP_KERNEL);
    //if (dev->data == NULL) {
       //printk (KERN_NOTICE "kzalloc failure.\n");
       //goto out_free4;
    //}

    dev->data = buf_dev;


    //初始化请求队列
    dev->queue = blk_init_queue(blk_request, NULL);
    if (dev->queue == NULL)
        goto out_free3;

    //指明扇区的大小
    blk_queue_logical_block_size(dev->queue, sect_size);
    dev->queue->queuedata = dev;


   //申请一个gendisk结构，初始化
    dev->gd = alloc_disk(1);
    if (! dev->gd) {
       printk (KERN_NOTICE "alloc_disk failure\n");
       goto out_free2;
    }
    dev->gd->major = major;
    dev->gd->first_minor = 0;
    dev->gd->fops = &blk_ops;
    dev->gd->queue = dev->queue;
    dev->gd->private_data = dev;
    sprintf (dev->gd->disk_name, "msfsblk%d", 0);
    set_capacity(dev->gd, nsectors*(sect_size/sect_size));

    //注册块设备
    add_disk(dev->gd);

    setup_msfs_filesystem(dev->data, dev->size);

    return err;
out_free2:
    blk_cleanup_queue(dev->queue);
out_free3:
    //kfree(dev->data);
//out_free4:
    kfree(dev);
out_unregister:
    unregister_blkdev(major, "blk");
    return err;
}
static void blk_exit(void)
{
   if (dev->gd) {
        del_gendisk(dev->gd);
        put_disk(dev->gd);
   }
   if (dev->queue)
        blk_cleanup_queue(dev->queue);
#if 0
   if (dev->data)
        kfree(dev->data);
#endif
    unregister_blkdev(major, "blk");
    kfree(dev);
}

module_init(blk_init);
module_exit(blk_exit);
