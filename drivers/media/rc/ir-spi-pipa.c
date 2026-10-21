// SPDX-License-Identifier: GPL-2.0-only
/* Pipa SPI infrared transmitter userspace interface. */

#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/limits.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <uapi/linux/lirc.h>

#define IR_SPI_PIPA_DRIVER_NAME	"ir-spi-pipa"
#define IR_SPI_PIPA_DEFAULT_FREQ	1920000
#define IR_SPI_PIPA_BITS_PER_WORD	32
#define IR_SPI_PIPA_BUFFER_SIZE	150000

struct ir_spi_pipa {
	struct kref refcount;
	struct mutex lock;
	struct spi_device *spi;
	struct miscdevice miscdev;
	struct spi_transfer xfer;
	u8 *buffer;
	size_t buffer_size;
	unsigned int open_count;
	bool removing;
};

static void ir_spi_pipa_free(struct kref *ref)
{
	struct ir_spi_pipa *idata = container_of(ref, struct ir_spi_pipa,
						 refcount);

	kfree(idata->buffer);
	kfree(idata);
}

static int ir_spi_pipa_open(struct inode *inode, struct file *file)
{
	struct miscdevice *miscdev = file->private_data;
	struct ir_spi_pipa *idata = container_of(miscdev, struct ir_spi_pipa,
							 miscdev);
	int ret = 0;

	if (!kref_get_unless_zero(&idata->refcount))
		return -ENODEV;

	mutex_lock(&idata->lock);
	if (idata->removing) {
		ret = -ENODEV;
	} else if (idata->open_count == UINT_MAX) {
		ret = -EMFILE;
	} else {
		idata->open_count++;
		file->private_data = idata;
	}
	mutex_unlock(&idata->lock);

	if (ret)
		kref_put(&idata->refcount, ir_spi_pipa_free);

	return ret;
}

static int ir_spi_pipa_release(struct inode *inode, struct file *file)
{
	struct ir_spi_pipa *idata = file->private_data;

	mutex_lock(&idata->lock);
	if (idata->open_count)
		idata->open_count--;

	if (!idata->open_count) {
		idata->xfer.len = 0;
		idata->xfer.speed_hz = IR_SPI_PIPA_DEFAULT_FREQ;
	}
	mutex_unlock(&idata->lock);

	kref_put(&idata->refcount, ir_spi_pipa_free);
	return 0;
}

static ssize_t ir_spi_pipa_write(struct file *file, const char __user *buf,
				 size_t len, loff_t *offset)
{
	struct ir_spi_pipa *idata = file->private_data;
	u8 *tx_buf = NULL;
	bool temporary = false;
	int ret;

	if (!len)
		return 0;

	mutex_lock(&idata->lock);
	if (idata->removing || !idata->spi) {
		ret = -ENODEV;
		goto out_unlock;
	}

	if (idata->xfer.len && idata->xfer.len != len) {
		ret = -EINVAL;
		goto out_unlock;
	}

	if (idata->xfer.len) {
		if (!idata->buffer || len > idata->buffer_size) {
			ret = -EINVAL;
			goto out_unlock;
		}
		tx_buf = idata->buffer;
	} else {
		tx_buf = kmalloc(len, GFP_KERNEL);
		if (!tx_buf) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		temporary = true;
		idata->xfer.len = len;
	}

	if (copy_from_user(tx_buf, buf, len)) {
		ret = -EFAULT;
		goto out_free;
	}

	idata->xfer.tx_buf = tx_buf;
	ret = spi_sync_transfer(idata->spi, &idata->xfer, 1);
	idata->xfer.tx_buf = NULL;
	if (ret)
		dev_err(&idata->spi->dev, "unable to deliver the signal\n");

out_free:
	if (temporary) {
		idata->xfer.len = 0;
		kfree(tx_buf);
	}
out_unlock:
	mutex_unlock(&idata->lock);
	return ret ? ret : len;
}

static long ir_spi_pipa_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	struct ir_spi_pipa *idata = file->private_data;
	u32 len;
	u8 *buffer;
	int ret;

	if (cmd != LIRC_SET_SEND_MODE)
		return -EINVAL;

	if (get_user(len, (u32 __user *)arg))
		return -EFAULT;

	mutex_lock(&idata->lock);
	if (idata->removing) {
		ret = -ENODEV;
		goto out_unlock;
	}
	if (idata->xfer.len == len) {
		ret = 0;
		goto out_unlock;
	}
	if (idata->open_count > 1) {
		ret = -EPERM;
		goto out_unlock;
	}

	if (!len) {
		kfree(idata->buffer);
		idata->buffer = NULL;
		idata->buffer_size = 0;
	} else {
		buffer = krealloc(idata->buffer, len, GFP_KERNEL);
		if (!buffer) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		idata->buffer = buffer;
		idata->buffer_size = len;
	}
	idata->xfer.len = len;
	ret = 0;

out_unlock:
	mutex_unlock(&idata->lock);
	return ret;
}

static const struct file_operations ir_spi_pipa_fops = {
	.owner = THIS_MODULE,
	.open = ir_spi_pipa_open,
	.release = ir_spi_pipa_release,
	.write = ir_spi_pipa_write,
	.llseek = no_llseek,
	.unlocked_ioctl = ir_spi_pipa_ioctl,
	.compat_ioctl = ir_spi_pipa_ioctl,
};

static int ir_spi_pipa_probe(struct spi_device *spi)
{
	struct ir_spi_pipa *idata;
	int ret;

	idata = kzalloc(sizeof(*idata), GFP_KERNEL);
	if (!idata)
		return -ENOMEM;

	kref_init(&idata->refcount);
	mutex_init(&idata->lock);
	idata->spi = spi;
	idata->xfer.bits_per_word = IR_SPI_PIPA_BITS_PER_WORD;
	idata->xfer.speed_hz = IR_SPI_PIPA_DEFAULT_FREQ;
	idata->buffer_size = IR_SPI_PIPA_BUFFER_SIZE;
	idata->buffer = kmalloc(idata->buffer_size, GFP_KERNEL);
	if (!idata->buffer) {
		ret = -ENOMEM;
		goto err_put;
	}

	idata->miscdev.minor = MISC_DYNAMIC_MINOR;
	idata->miscdev.name = "ir_spi";
	idata->miscdev.fops = &ir_spi_pipa_fops;
	idata->miscdev.parent = &spi->dev;
	idata->miscdev.mode = 0666;

	ret = misc_register(&idata->miscdev);
	if (ret)
		goto err_put;

	spi_set_drvdata(spi, idata);
	return 0;

err_put:
	kref_put(&idata->refcount, ir_spi_pipa_free);
	return ret;
}

static int ir_spi_pipa_remove(struct spi_device *spi)
{
	struct ir_spi_pipa *idata = spi_get_drvdata(spi);

	misc_deregister(&idata->miscdev);

	mutex_lock(&idata->lock);
	idata->removing = true;
	idata->spi = NULL;
	mutex_unlock(&idata->lock);

	kref_put(&idata->refcount, ir_spi_pipa_free);
	return 0;
}

static const struct of_device_id ir_spi_pipa_of_match[] = {
	{ .compatible = "ir-spi" },
	{}
};
MODULE_DEVICE_TABLE(of, ir_spi_pipa_of_match);

static struct spi_driver ir_spi_pipa_driver = {
	.probe = ir_spi_pipa_probe,
	.remove = ir_spi_pipa_remove,
	.driver = {
		.name = IR_SPI_PIPA_DRIVER_NAME,
		.of_match_table = ir_spi_pipa_of_match,
	},
};
module_spi_driver(ir_spi_pipa_driver);

MODULE_DESCRIPTION("Pipa SPI IR transmitter");
MODULE_LICENSE("GPL v2");
