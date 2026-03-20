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
#include <linux/slab.h> // required for kmalloc
#include <linux/fs.h>   // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Venetia Furtado");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    filp->private_data = NULL;
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    struct aesd_dev *dev = NULL;
    size_t entry_offset_byte_rtn = 0;
    struct aesd_buffer_entry *entry = NULL;
    ssize_t num_copy_bytes = 0;
    unsigned long copy_status = 0;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    // error checking
    if (filp == NULL || buf == NULL || f_pos == NULL)
    {
        PDEBUG("ERROR: input argument error");
        return -EINVAL;
    }

    dev = filp->private_data;
    if (dev == NULL)
    {
        PDEBUG("ERROR: device driver pointer is NULL");
        return -EINVAL;
    }

    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("ERROR: Not able to lock mutex");
        return -ERESTARTSYS;
    }

    // find data offset
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&(dev->buffer), *f_pos, &entry_offset_byte_rtn);
    if (entry == NULL)
    {
        PDEBUG("DEBUG: Invalid offset provided");
        mutex_unlock(&dev->lock);
        return 0;
    }

    // copy data
    num_copy_bytes = min(count, entry->size - entry_offset_byte_rtn);

    copy_status = copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, num_copy_bytes);
    if (copy_status == 0)
    {
        // advance file position by number of bytes copied
        *f_pos += num_copy_bytes;
        retval = num_copy_bytes;
    }
    else
    {
        PDEBUG("DEBUG: copy to user failed");
        retval = -EFAULT;
        return 0;
    }

    mutex_unlock(&dev->lock);

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = 0;
    char *kbuf = NULL;
    unsigned long status;
    bool write_to_cb = false;
    struct aesd_dev *dev = NULL;
    size_t i = 0;

    if (filp == NULL || buf == NULL || f_pos == NULL)
    {
        PDEBUG("ERROR: input argument error");
        return -EINVAL;
    }

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    // allocate memory
    kbuf = kmalloc(count, GFP_KERNEL);
    if (kbuf == NULL)
    {
        PDEBUG("ERROR: Cannot allocate memory for new data");
        return -ENOMEM;
    }

    // copy from user to kernel space
    status = copy_from_user(kbuf, buf, count);
    if (status != 0)
    {
        PDEBUG("ERROR: copy from userspace failed");
        return -EFAULT;
    }

    // check if data contains newline
    for (i = 0; i < count; i++)
    {
        if (kbuf[i] == '\n')
        {
            write_to_cb = true;
            break;
        }
    }

    dev = filp->private_data;
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

loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = NULL;
    loff_t new_f_pos = -1;
    size_t total_size = 0;

    // error checking
    if (filp == NULL)
    {
        PDEBUG("ERROR: input argument error");
        return -EINVAL;
    }

    dev = filp->private_data;

    if (dev == NULL)
    {
        PDEBUG("ERROR: device driver pointer is NULL");
        return -EINVAL;
    }

    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("ERROR: Not able to lock mutex");
        return -ERESTARTSYS;
    }

    total_size = aesd_circular_buffer_get_length(&(dev->buffer));

    mutex_unlock(&dev->lock);

    switch (whence)
    {
    case SEEK_SET:
        new_f_pos = offset;
        break;

    case SEEK_CUR:
        new_f_pos = filp->f_pos + offset;
        break;

    case SEEK_END:
        new_f_pos = total_size + offset;
        break;

    default:
        return -EINVAL;
    }

    if (new_f_pos < 0 || new_f_pos > total_size)
    {
        return -EINVAL;
    }

    filp->f_pos = new_f_pos;

    return new_f_pos;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = NULL;
    struct aesd_seekto seekto_val;
    unsigned long status;
    loff_t new_f_pos = -1;

    if (cmd != AESDCHAR_IOCSEEKTO)
    {
        return -ENOTTY;
    }

    // error checking
    if (filp == NULL)
    {
        PDEBUG("ERROR: input argument error");
        return -EINVAL;
    }

    dev = filp->private_data;

    if (dev == NULL)
    {
        PDEBUG("ERROR: device driver pointer is NULL");
        return -EINVAL;
    }

    // copy from user to kernel space
    status = copy_from_user(&seekto_val, &arg, sizeof(struct aesd_seekto));
    if (status != 0)
    {
        PDEBUG("ERROR: copy from userspace failed");
        return -EFAULT;
    }

    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("ERROR: Not able to lock mutex");
        return -ERESTARTSYS;
    }

    new_f_pos = aesd_circular_buffer_find_seekto_fpos(&(dev->buffer), seekto_val.write_cmd, seekto_val.write_cmd_offset);

    mutex_unlock(&dev->lock);

    if (new_f_pos == -1)
    {
        return -EINVAL;
    }

    filp->f_pos = new_f_pos;

    return 0;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_ioctl,
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
    uint8_t index = 0;
    struct aesd_buffer_entry *entryptr = NULL;

    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    AESD_CIRCULAR_BUFFER_FOREACH(entryptr, &aesd_device.buffer, index)
    {
        if (entryptr->buffptr != NULL)
        {
            kfree(entryptr->buffptr);
            entryptr->buffptr = NULL;
        }
    }

    if (aesd_device.new_entry.buffptr != NULL)
    {
        kfree(aesd_device.new_entry.buffptr);
    }

    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
