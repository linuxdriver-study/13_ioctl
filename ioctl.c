#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/types.h>
#include <linux/ide.h>
#include <linux/timer.h>

#define DEVICE_CNT      1
#define DEVICE_NAME     "led"

#define CLOSE_CMD       (_IO(0xEF, 0x01))       /* 关闭定时器 */
#define OPEN_CMD        (_IO(0xEF, 0x02))       /* 打开定时器 */
#define SETPERIOD_CMD   (_IO(0xEF, 0x03))       /* 设置定时器周期指令 */

struct led_device_struct {
        int major;
        int minor;
        dev_t devid;
        struct cdev led_cdev;
        struct class *class;
        struct device *device;
        struct device_node *nd;
        int led_gpio;
        struct timer_list timer;
        atomic_t timeperiod;
};
static struct led_device_struct led_dev;

static int led_open(struct inode *inode, struct file *file);
static long timer_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int led_release(struct inode *inode, struct file *file);

static struct file_operations ops = {
        .owner = THIS_MODULE,
        .open = led_open,
        .unlocked_ioctl = timer_unlocked_ioctl,
        .release = led_release,
};

static int led_open(struct inode *inode, struct file *file)
{
        file->private_data = &led_dev;
        return 0;
}

static long timer_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
        int ret = 0;
        int value[1] = {0};
        struct led_device_struct *dev = filp->private_data;
        int command = 0;
        
        ret = copy_from_user(&command, (int *)cmd, sizeof(int));
        if (ret < 0) {
                goto error;
        }

        switch (command) {
        case CLOSE_CMD:
                del_timer_sync(&dev->timer);
                break;
        case OPEN_CMD:
                mod_timer(&dev->timer, jiffies
                          + msecs_to_jiffies(atomic_read(&dev->timeperiod)));
                break;
        case SETPERIOD_CMD: 
#if 1
                ret = copy_from_user(value, (int *)arg, sizeof(int));
                if (ret < 0) {
                        goto error;
                }
                printk("value = %d\n", value[0]);
                atomic_set(&dev->timeperiod, value[0]);
#else
                atomic_set(&dev->timeperiod, arg);
#endif
                mod_timer(&dev->timer, jiffies
                          + msecs_to_jiffies(atomic_read(&dev->timeperiod)));
                break;
        }
        
error:
        return ret;
}

static int led_release(struct inode *inode, struct file *file)
{
        file->private_data = NULL;
        return 0;
}

void led_timer(unsigned long arg)
{
        static int status = 0;
        struct led_device_struct *dev = (struct led_device_struct *)arg;

        gpio_set_value(dev->led_gpio, status);
        status = !status;

        mod_timer(&dev->timer, jiffies 
                  + msecs_to_jiffies(atomic_read(&dev->timeperiod)));
}

int led_io_config(struct led_device_struct *dev)
{
        int ret = 0;

        dev->nd = of_find_node_by_path("/gpioled");
        if (dev->nd == NULL) {
                printk("find node error!\n");
                ret = -EINVAL;
                goto fail_find_node;
        }

        dev->led_gpio = of_get_named_gpio(dev->nd, "led-gpios", 0);
        if (dev->led_gpio < 0) {
                printk("get named error!\n");
                ret = -EINVAL;
                goto fail_get_named;
        }

        ret = gpio_request(dev->led_gpio, "led");
        if (ret != 0) {
                printk("gpio request error!\n");
                ret = -EINVAL;
                goto fail_gpio_request;
        }

        ret = gpio_direction_output(dev->led_gpio, 1);
        if (ret != 0) {
                printk("set dir error!\n");
                ret = -EINVAL;
                goto fail_gpio_set_dir;
        }

fail_gpio_set_dir:
        gpio_free(dev->led_gpio);
fail_gpio_request:
fail_get_named:
fail_find_node:
        return ret;
}

static int __init led_init(void)
{
        int ret = 0;
        if (led_dev.major) {
                led_dev.devid = MKDEV(led_dev.major, led_dev.minor);
                ret = register_chrdev_region(led_dev.devid, DEVICE_CNT, DEVICE_NAME);
        } else {
                ret = alloc_chrdev_region(&led_dev.devid, 0, DEVICE_CNT, DEVICE_NAME);
        }
        if (ret < 0) {
                printk("chrdev region error!\n");
                goto fail_chrdev_region;
        }
        led_dev.major = MAJOR(led_dev.devid);
        led_dev.minor = MINOR(led_dev.devid);
        printk("major:%d minor:%d\n", led_dev.major, led_dev.minor);

        cdev_init(&led_dev.led_cdev, &ops);
        ret = cdev_add(&led_dev.led_cdev, led_dev.devid, DEVICE_CNT);
        if (ret < 0) {
                printk("cdev add error!\n");
                goto fail_cdev_add;
        }
        led_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
        if (IS_ERR(led_dev.class)) {
                printk("class create error!\n");
                ret = -EINVAL;
                goto fail_class_create;
        }
        led_dev.device = device_create(led_dev.class, NULL,
                                       led_dev.devid, NULL, DEVICE_NAME);
        if (IS_ERR(led_dev.device)) {
                printk("device create error!\n");
                ret = -EINVAL;
                goto fail_device_create;
        }
        ret = led_io_config(&led_dev);
        if (ret < 0) {
                goto fail_io_config;
        }

        atomic_set(&led_dev.timeperiod, 500);
        init_timer(&led_dev.timer);
        led_dev.timer.expires = jiffies 
                + msecs_to_jiffies(atomic_read(&led_dev.timeperiod));
        led_dev.timer.function = led_timer;
        led_dev.timer.data = (unsigned long)&led_dev;
        add_timer(&led_dev.timer);

        goto success;
        
fail_io_config:
        device_destroy(led_dev.class, led_dev.devid);
fail_device_create:
        class_destroy(led_dev.class);
fail_class_create:
        cdev_del(&led_dev.led_cdev);
fail_cdev_add:
        unregister_chrdev_region(led_dev.devid, DEVICE_CNT);
fail_chrdev_region:
success:
        return ret;
}

static void __exit led_exit(void)
{
        del_timer(&led_dev.timer);
        gpio_set_value(led_dev.led_gpio, 1);
        gpio_free(led_dev.led_gpio);
        device_destroy(led_dev.class, led_dev.devid);
        class_destroy(led_dev.class);
        cdev_del(&led_dev.led_cdev);
        unregister_chrdev_region(led_dev.devid, DEVICE_CNT);
}

module_init(led_init);
module_exit(led_exit);
MODULE_AUTHOR("wanglei");
MODULE_LICENSE("GPL");
