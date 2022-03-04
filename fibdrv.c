#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>

typedef unsigned __int128 uint128_t;

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 200

/* ktime */
static ktime_t fib_kt;
static ktime_t copy_kt;
static long int fib_kt_ns;
static long int copy_kt_ns;

static ssize_t kobj_fib_show(struct kobject *kobj,
                             struct kobj_attribute *attr,
                             char *buf)
{
    fib_kt_ns = ktime_to_ns(fib_kt);
    return snprintf(buf, 64, "%ld\n", fib_kt_ns);
}

static ssize_t kobj_copy_show(struct kobject *kobj,
                              struct kobj_attribute *attr,
                              char *buf)
{
    copy_kt_ns = ktime_to_ns(copy_kt);
    return snprintf(buf, 64, "%ld\n", copy_kt_ns);
}

static ssize_t kobj_store(struct kobject *kobj,
                          struct kobj_attribute *attr,
                          const char *buf,
                          size_t count)
{
    return count;
}

struct kobject *kobj_ref;
static struct kobj_attribute ktime_fib_attr =
    __ATTR(fib_kt_ns, 0664, kobj_fib_show, kobj_store);
static struct kobj_attribute ktime_copy_attr =
    __ATTR(copy_kt_ns, 0664, kobj_copy_show, kobj_store);
static struct attribute *attrs[] = {
    &ktime_fib_attr.attr,
    &ktime_copy_attr.attr,
    NULL,
};
static struct attribute_group attr_group = {
    .attrs = attrs,
};

/* device */
static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

static uint128_t fib_sequence(long long k)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    uint128_t a = 0, b = 1;
    uint128_t tmp;
    for (long long i = 0; i < k; i++) {
        tmp = a;
        a = a + b;
        b = tmp;
    }
    return a;
}

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    fib_kt = ktime_get();
    uint128_t ret = fib_sequence(*offset);  // uint128 cannot return to user
    fib_kt = ktime_sub(ktime_get(), fib_kt);

    copy_kt = ktime_get();
    copy_to_user(buf, &ret, sizeof(ret));
    copy_kt = ktime_sub(ktime_get(), copy_kt);
    return 0;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }

    /* ktime */
    kobj_ref = kobject_create_and_add("fib_time", kernel_kobj);

    if (!kobj_ref) {
        printk(KERN_ALERT "Failed to create kobject");
        goto failed_device_create;
    }
    if (sysfs_create_group(kobj_ref, &attr_group))
        kobject_put(kobj_ref);

    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);

    kobject_del(kobj_ref);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
