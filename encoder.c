#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <asm/atomic.h>

#define SIGS_PER_ROT	192
#define CIRCUMFERENCE	204204	/* in micrometers */
#define DIST_PER_SIG	1064	/* in micrometers */

struct encoder_data {
	atomic_t sigs;
	atomic_t rots;
};

/* show and store functions declarations */
static ssize_t encoder_show_distance(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	unsigned long long distance;
	struct encoder_data *data =
		platform_get_drvdata(to_platform_device(dev));

	distance = atomic_read(&data->rots) * CIRCUMFERENCE +
		atomic_read(&data->sigs) * DIST_PER_SIG;

	/* distance micrometers */
	return sprintf(buf, "%llu", distance);
}

static ssize_t encoder_store_reset(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int error;
	unsigned long tmp;
	struct encoder_data *data =
		platform_get_drvdata(to_platform_device(dev));

	error = kstrtoul(buf, 10, &tmp);
	if (error)
		return error;

	if (tmp != 1)
		return -EINVAL;

	atomic_set(&data->sigs, 0);
	atomic_set(&data->rots, 0);

	return count;
}

static DEVICE_ATTR(distance, S_IRUGO, encoder_show_distance, NULL);
static DEVICE_ATTR(reset, S_IWUSR, NULL, encoder_store_reset);

static struct attribute *encoder_attributes[] = {
	&dev_attr_distance.attr,
	&dev_attr_reset.attr,
	NULL,
};

static const struct attribute_group encoder_group = {
	.attrs = encoder_attributes,
};

/* interrupt service routines */
static irqreturn_t encoder_irq_handler(int irq, void *dev_id)
{
	struct encoder_data *data = dev_id;

	atomic_inc(&data->sigs);
	if (atomic_read(&data->sigs) == SIGS_PER_ROT) {
		atomic_inc(&data->rots);
		atomic_set(&data->sigs, 0);
	}

	return IRQ_HANDLED;
}

static int encoder_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pinctrl *pinctrl;
	struct device_node *node = dev->of_node;
	struct encoder_data *data;
	int irq = 0;
	int error = 0;

	if (node == NULL) {
		dev_err(dev, "Non DT platforms not supported\n");
		return -EINVAL;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	/* Select pins that are in use */
	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl))
		dev_warn(&pdev->dev, "Unable to select pin group\n");

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return irq;

	error = devm_request_irq(&pdev->dev, irq, encoder_irq_handler,
			0, pdev->name, data);
	if (error != 0) {
		dev_err(&pdev->dev, "request_irq failed\n");
		return error;
	}

	error = sysfs_create_group(&pdev->dev.kobj, &encoder_group);
	if (error) {
		dev_err(&pdev->dev, "sysfs_create_group() failed (%d)\n",
				error);
		return error;
	}

	atomic_set(&data->sigs, 0);
	atomic_set(&data->rots, 0);

	return 0;
}

static int encoder_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &encoder_group);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id encoder_match[] = {
	{ .compatible = "dagu,hall-encoder", },
	{ },
};
MODULE_DEVICE_TABLE(of, encoder_match);
#endif

static struct platform_driver encoder_driver = {
	.probe	= encoder_probe,
	.remove = encoder_remove,
	.driver = {
		.name	= "encoder",
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(encoder_match),
#endif
	},
};
module_platform_driver(encoder_driver);

MODULE_AUTHOR("Adam Olek, Maciej Sobkowski <maciejjo@maciejjo.pl>");
MODULE_DESCRIPTION("Driver for interrupt-driven hall effect rotary encoder");
MODULE_LICENSE("GPL");
