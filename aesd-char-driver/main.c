/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Guy Levanon");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

static int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

static int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    return 0;
}

static ssize_t aesd_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *device = filp->private_data;
    struct aesd_buffer_entry *entry;
    size_t offset_in_entry = 0;

    if (mutex_lock_interruptible(&device->lock))
        return -ERESTARTSYS;

    PDEBUG("read %zu bytes from offset %lld",count,*f_pos);
    
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&device->circular_buffer, *f_pos, &offset_in_entry);
    if (entry == NULL)
    {
        goto cleanup;
    }

    count = min(count, entry->size - offset_in_entry);
    if (copy_to_user(buf, &entry->buffptr[offset_in_entry], count))
    {
        retval = -EFAULT;
        goto cleanup;
    }

    *f_pos += count;
    retval = count;

cleanup:
    mutex_unlock(&device->lock);
    return retval;
}

static int append_to_pending_chunk(struct aesd_dev *device, const char __user *buf, size_t count)
{
    int retval = 0;
    size_t new_pending_buffer_size = device->pending_buffer_size + count;
    char *new_pending_buffer = NULL;

    new_pending_buffer = kmalloc(new_pending_buffer_size, GFP_KERNEL);
    if (new_pending_buffer == NULL)
    {
        retval = -ENOMEM;
        goto cleanup;
    }

    if (copy_from_user(&new_pending_buffer[device->pending_buffer_size], buf, count))
    {
        retval = -EFAULT;
        goto cleanup;
    }

    if (device->pending_buffer != NULL)
    {
        memcpy(new_pending_buffer, device->pending_buffer, device->pending_buffer_size);
        kfree(device->pending_buffer);
    }

    device->pending_buffer = new_pending_buffer;
    device->pending_buffer_size = new_pending_buffer_size;

cleanup:
    if (retval != 0 && new_pending_buffer != NULL)
    {
        kfree(new_pending_buffer);
    }
    return retval;
}

static void flush_pending_buffer(struct aesd_dev *device)
{
    struct aesd_buffer_entry entry = {0};

    PDEBUG("flushing pending_buffer to the circular_buffer[%d]", device->circular_buffer.in_offs);

    entry.buffptr = device->pending_buffer;
    entry.size = device->pending_buffer_size;
    aesd_circular_buffer_add_entry(&device->circular_buffer, &entry);

    device->pending_buffer = NULL;
    device->pending_buffer_size = 0;
}

static ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    ssize_t retval;
    struct aesd_dev *device = filp->private_data;
    (void) f_pos;

    if (mutex_lock_interruptible(&device->lock))
        return -ERESTARTSYS;

    PDEBUG("write %zu bytes to offset %lld", count, *f_pos);
    
    retval = append_to_pending_chunk(device, buf, count);
    if (retval < 0)
    {
        goto cleanup;
    }

    if (strnchr(device->pending_buffer, device->pending_buffer_size, '\n'))
    {
        flush_pending_buffer(device);
    }

    retval = count;

cleanup:
    mutex_unlock(&device->lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

static int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;

    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.circular_buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }

    PDEBUG("aesd module loaded!");
    return result;
}

static void aesd_cleanup_module(void)
{
    int index = 0;
    struct aesd_buffer_entry *entry;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    PDEBUG("aesd module unloading!");

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index)
    {
        if (entry->buffptr != NULL)
        {
            kfree(entry->buffptr);
        }
    }

    if (aesd_device.pending_buffer != NULL)
    {
        kfree(aesd_device.pending_buffer);
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
