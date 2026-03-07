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
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Venetia Furtado"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle read
     */
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    if (filp == NULL || buf == NULL || fpos == NULL)
    {
        PDEBUG("ERROR: input argument error");
        return -EINVAL;
    }

    ssize_t retval = 0;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    // allocate memory
    char *kbuf = kmalloc(count, GFP_KERNEL);
    if (kbuf == NULL)
    {
        PDEBUG("ERROR: Cannot allocate memory for new data");
        return -ENOMEM;
    }

    // copy from user to kernel space
    unsigned long status = copy_from_user(kbuf, buf, count);
    if (status != 0)
    {
        PDEBUG("ERROR: copy from userspace failed");
        return -EFAULT;
    }

    // check if data contains newline
    bool write_to_cb = false;
    for (size_t i = 0; i < count; i++)
    {
        if (kbuf[i] == '\n')
        {
            write_to_cb = true;
            break;
        }
    }

    struct aesd_dev *dev = filp->private_data;
    if (dev == NULL)
    {
        PDEBUG("ERROR: device driver pointer is NULL");
        return -EINVAL;
    }

    // start critical section
    mutex_lock(&dev->lock);

    // save data for future write operations if no newline found
    dev->new_entry.buffptr = krealloc(dev->new_entry.buffptr, dev->new_entry.size + count, GFP_KERNEL);
    if (dev->new_entry.buffptr == NULL)
    {
        kfree(kbuf);
        PDEBUG("ERROR: krealloc failed");
        mutex_unlock(&dev->lock);
        return -ENOMEM;
    }
    memcpy((char *)dev->new_entry.buffptr + dev->new_entry.size, kbuf, count);
    dev->new_entry.size += count;

    // write to cb since newline was found
    if (write_to_cb == true)
    {
        const char *buffptr_to_free = aesd_circular_buffer_add_entry(&(dev->buffer), &(dev->new_entry));

        if (buffptr_to_free != NULL)
        {
            kfree(buffptr_to_free);
            PDEBUG("WARN: kfree of old buffer failed");
        }

        dev->new_entry.buffptr = NULL;
        dev->new_entry.size = 0;
    }
    mutex_unlock(&dev->lock);
    // end critical section

    // set retval to count to signal that requested number of bytes has been transferred
    retval = count;

    return retval;
}
struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
                                 "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&(aesd_device.buffer));
    aesd_device.new_entry.buffptr = NULL;
    aesd_device.new_entry.size = 0;
    mutex_init(&aesd_device.lock);

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    uint8_t index = 0;
    struct aesd_buffer_entry *entryptr = NULL;
    AESD_CIRCULAR_BUFFER_FOREACH(entryptr, &aesd_device.buffer, index)
    {
        if (entryptr->buffptr != NULL)
        {
#ifdef __KERNEL__
            kfree(entryptr->buffptr);
#else
            free(entryptr->buffptr);
#endif
            entryptr->buffptr = NULL;
        }
    }

    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
