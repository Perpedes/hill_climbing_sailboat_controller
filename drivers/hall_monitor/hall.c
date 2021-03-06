/*
 * hall_new.c
 *
 *  Created on: Nov 18, 2013
 *      Author: Bjørn Smith @ SDU.dk
 */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>

#include <linux/types.h>	// dev_t data type
#include <linux/kdev_t.h>	// dev_t: Major() and Minor() functions
#include <linux/fs.h>		// chrdev regirstration: alloc_chardev_region()
#include <linux/device.h>	// udev support: class_create() and device_create()
#include <linux/cdev.h>		// VFS registration: cdev_init() and cdev_add()
#include <linux/uaccess.h>	// copy_to_user() and read_from_user()

#include <linux/gpio.h>		// gpio kernel library
#include <linux/interrupt.h>	// interrupt functions
#include <linux/time.h>


#define GPIO_HALLA		174     	//white or purple
#define GPIO_HALLB		175			//white or purple
#define GPIO_IN			172			//Green wire
#define GPIO_OUT		173			//Yellow wire
#define DEBOUNCE_DELAY 	1
#define HALL_MAX		870		//The length of sail actuator!

static dev_t first; // Global variable for the first device number
static struct cdev c_dev; //, c_dev2; // Global variable for the character device structure
static struct class *cl; // Global variable for the device class

static int init_result;
static int hall_a_irq = 0;
static int hall_b_irq = 0;
static int hall_in_irq = 0;
static int hall_out_irq = 0;
static unsigned int hall_length = 0; // irq count
//static int hall_state = 0;

unsigned int last_interrupt_time = 0;
static uint64_t epochMilli;

/*************IRQ state flow**********************
 * A:____|-----|____|-----|____|-----|__
 * B:______|-----|____|-----|____|-----|
 *
 * S:    0 1   2 3  0 1   2 3
 *
 * Concept: Check state before and after to deside direction of movement.
 *
 * Interrupts needed for both falling and rising edges
 * State | HallA  | HallB  |
 * S0    | rising | low    |
 * S1    | high   | rising |
 * S2    | falling| high   |
 * S3    | low    | falling|
 *
 * Due too only ONE irq per GPIO (dafaq?!?!?) a new worse tactic is needed...
 * now we will only use rising edge trigger, and at the same time test the otherpin.
 * this WILL affect precision, and I will advice for doing periodic endstop tests.
 *
 * direction one:
 * A_trig + B_low -> B_trig + A_high ..... forward
 * diretion two:
 * A_trig + B_high -> B_trig + A_low ..... backwards!
 */

/*
 * Timer for interrupt debounce, borrowed from
 * http://raspberrypi.stackexchange.com/questions/8544/gpio-interrupt-debounce
 */

unsigned int millis(void) {
	struct timeval tv;
	uint64_t now;

	do_gettimeofday(&tv);
	now = (uint64_t) tv.tv_sec * (uint64_t) 1000
			+ (uint64_t)(tv.tv_usec / 1000);

	return (uint32_t)(now - epochMilli);
}

/*
 * The interrupt handler functions
 */

static irqreturn_t halla_rising_handler(int irq, void *dev_id) {
	unsigned int interrupt_time = millis();

	if (interrupt_time - last_interrupt_time < DEBOUNCE_DELAY) {
		//printk(KERN_NOTICE "button: Ignored Interrupt! \n");
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;
//	printk(KERN_INFO "halla: I got rising interrupt.\n");

	 if(gpio_get_value(GPIO_HALLB)==0)
	 {
		 if(hall_length==HALL_MAX)
		 {

		 }
		 else
		 {
			 hall_length++;
		 }
	 }
	 else
	 {
		 if(hall_length==0)
		 {

		 }
		 else
		 {
			 hall_length--;
		 }
	 }

	return IRQ_HANDLED;
}
static irqreturn_t hallb_rising_handler(int irq, void *dev_id) {
	unsigned int interrupt_time = millis();

	if (interrupt_time - last_interrupt_time < DEBOUNCE_DELAY) {
		//printk(KERN_NOTICE "button: Ignored Interrupt! \n");
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;
//	printk(KERN_INFO "hallb: I got rising interrupt.\n");

	 if(gpio_get_value(GPIO_HALLA)==0)
	 {
		 if(hall_length==0)
		 {

		 }
		 else
		 {
			 hall_length--;
		 }
	 }
	 else
	 {
		 if(hall_length==HALL_MAX)
		 {

		 }
		 else
		 {
			 hall_length++;
		 }
		 hall_length++;
	 }

	return IRQ_HANDLED;
}
static irqreturn_t hallin_rising_handler(int irq, void *dev_id) {
	unsigned int interrupt_time = millis();

	if (interrupt_time - last_interrupt_time < DEBOUNCE_DELAY) {
		//printk(KERN_NOTICE "button: Ignored Interrupt! \n");
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;
	printk(KERN_INFO "hallin:  IN-END-STOP hit.\n");

	hall_length = 0;

	return IRQ_HANDLED;
}
static irqreturn_t hallout_rising_handler(int irq, void *dev_id) {
	unsigned int interrupt_time = millis();

	if (interrupt_time - last_interrupt_time < DEBOUNCE_DELAY) {
		//printk(KERN_NOTICE "button: Ignored Interrupt! \n");
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;
	printk(KERN_INFO "hallout: OUT-END-STOP hit.\n");

	hall_length = HALL_MAX;

	return IRQ_HANDLED;
}

/*static irqreturn_t halla_rising_handler(int irq, void *dev_id) {
	unsigned int interrupt_time = millis();

	if (interrupt_time - last_interrupt_time < DEBOUNCE_DELAY) {
		//printk(KERN_NOTICE "button: Ignored Interrupt! \n");
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;
	printk(KERN_INFO "halla: I got rising interrupt.\n");

	if (hall_state == 1) {
		hall_length--;
	} else if (hall_state == 3) {
		hall_length++;
	}
	hall_state = 0;

	return IRQ_HANDLED;
}*/

/*static irqreturn_t halla_falling_handler(int irq, void *dev_id) {
	unsigned int interrupt_time = millis();

	if (interrupt_time - last_interrupt_time < DEBOUNCE_DELAY) {
		//printk(KERN_NOTICE "button: Ignored Interrupt! \n");
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;

	printk(KERN_INFO "halla: I got falling interrupt.\n");

	if (hall_state == 1) {
		hall_length++;
	} else if (hall_state == 3) {
		hall_length--;
	}
	hall_state = 2;

	return IRQ_HANDLED;
}*/
/*static irqreturn_t hallb_rising_handler(int irq, void *dev_id) {
	unsigned int interrupt_time = millis();

	if (interrupt_time - last_interrupt_time < DEBOUNCE_DELAY) {
		//printk(KERN_NOTICE "button: Ignored Interrupt! \n");
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;

	printk(KERN_INFO "hallb: I got rising interrupt.\n");

	if (hall_state == 0) {
		hall_length++;
	} else if (hall_state == 2) {
		hall_length--;
	}
	hall_state = 1;

	return IRQ_HANDLED;
}*/

/*static irqreturn_t hallb_falling_handler(int irq, void *dev_id) {
	unsigned int interrupt_time = millis();

	if (interrupt_time - last_interrupt_time < DEBOUNCE_DELAY) {
		//printk(KERN_NOTICE "button: Ignored Interrupt! \n");
		return IRQ_HANDLED;
	}
	last_interrupt_time = interrupt_time;

	printk(KERN_INFO "hallb: I got falling interrupt.\n");

	if (hall_state == 2) {
		hall_length++;
	} else if (hall_state == 0) {
		hall_length--;
	}
	hall_state = 3;

	return IRQ_HANDLED;
}*/

/*
 * .read
 */
static ssize_t hall_read(struct file* F, char *buf, size_t count, loff_t *f_pos) {
//	printk(KERN_INFO "hall length: %u\n", hall_length);

	char buffer[10];

	unsigned int temp = hall_length;

	count = sprintf(buffer, "%u", temp);

	if (copy_to_user(buf, buffer, count)) {
		return -EFAULT;
	}

	if (*f_pos == 0) {
		*f_pos += count;
		return count;
	} else {
		return 0;
	}
}
/*
 * .write
 */
static ssize_t hall_write(struct file* F, const char *buf, size_t count, loff_t *f_pos) {
//	printk(KERN_INFO "hall: Executing WRITE.\n");

	switch (buf[0]) {
	case '0':
		hall_length = 0;
		break;
	case '1':
		hall_length = HALL_MAX;
		break;

	default:
		printk("hall: Wrong option.\n");
		break;
	}

	return count;
}

/*
 * / .open
 */
static int hall_open(struct inode *inode, struct file *file) {
	return 0;
}

/*
 * .close
 */
static int hall_close(struct inode *inode, struct file *file) {
	return 0;
}

static struct file_operations FileOps =
{
		.owner 		= THIS_MODULE,
		.open 		= hall_open,
		.read 		= hall_read,
		.write 		= hall_write,
		.release 	= hall_close
};

/*
 * Module init function.
 */
static int __init hall_init(void)
{
	struct timeval tv;

	do_gettimeofday(&tv);
	epochMilli = (uint64_t)tv.tv_sec * (uint64_t)1000 + (uint64_t)(tv.tv_usec / 1000);

	init_result = alloc_chrdev_region( &first, 0, 2, "hall_driver" );

	if( 0 > init_result )
	{
		printk( KERN_ALERT "hall: Device Registration failed\n" );
		return -1;
	}
	else
	{
		printk( KERN_ALERT "hall: Major number is: %d\n",init_result );
		//return 0;
	}

	if ( (cl = class_create( THIS_MODULE, "chardev" ) ) == NULL )
	{
		printk( KERN_ALERT "hall: Class creation failed\n" );
		unregister_chrdev_region( first, 1 );
		return -1;
	}

	if( device_create( cl, NULL, first, NULL, "hall" ) == NULL )
	{
		printk( KERN_ALERT "hall: Device creation failed\n" );
		class_destroy(cl);
		unregister_chrdev_region( first, 2 );
		return -1;
	}
	cdev_init( &c_dev, &FileOps );

	if( cdev_add( &c_dev, first, 1 ) == -1)
	{
		printk( KERN_ALERT "hall device addition failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 2 );
		return -1;
	}

	if(gpio_request(GPIO_HALLA, "halla"))
	{
		printk( KERN_ALERT "gpio request failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 2 );
		return -1;
	}
	if(gpio_request(GPIO_HALLB, "hallb"))
	{
		printk( KERN_ALERT "gpio request failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 2 );
		return -1;
	}
	if(gpio_request(GPIO_IN, "hallin"))
	{
		printk( KERN_ALERT "gpio request failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 2 );
		return -1;
	}
	if(gpio_request(GPIO_OUT, "hallout"))
	{
		printk( KERN_ALERT "gpio request failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 2 );
		return -1;
	}

	if((hall_a_irq = gpio_to_irq(GPIO_HALLA)) < 0)
	{
		printk( KERN_ALERT "gpio to irq failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 2 );
		return -1;
	}
	if((hall_b_irq = gpio_to_irq(GPIO_HALLB)) < 0)
	{
		printk( KERN_ALERT "gpio to irq failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 2 );
		return -1;
	}
	if((hall_in_irq = gpio_to_irq(GPIO_IN)) < 0)
		{
			printk( KERN_ALERT "gpio to irq failed\n" );
			device_destroy( cl, first );
			class_destroy( cl );
			unregister_chrdev_region( first, 2 );
			return -1;
		}
	if((hall_out_irq = gpio_to_irq(GPIO_OUT)) < 0)
		{
			printk( KERN_ALERT "gpio to irq failed\n" );
			device_destroy( cl, first );
			class_destroy( cl );
			unregister_chrdev_region( first, 2 );
			return -1;
		}

	if(request_irq(hall_a_irq, halla_rising_handler, IRQF_TRIGGER_RISING | IRQF_DISABLED, "gpiomod#hall", NULL ) == -1)
	{
		printk( KERN_ALERT "hall device interrupt handle failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 1 );

		return -1;
	}
	else
	{
		printk( KERN_ALERT "hall: Device irq number is %d\n", hall_a_irq );
	}
/*	if(request_irq(hall_a_irq, halla_falling_handler, IRQF_TRIGGER_FALLING | IRQF_DISABLED, "gpiomod#hall", NULL ) == -1)
	{
		printk( KERN_ALERT "hall device interrupt handle failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 1 );

		return -1;
	}
	else
	{
		printk( KERN_ALERT "hall: Device irq number is %d\n", hall_a_irq );
	}*/
	if(request_irq(hall_b_irq, hallb_rising_handler, IRQF_TRIGGER_RISING | IRQF_DISABLED, "gpiomod#hall", NULL ) == -1)
	{
		printk( KERN_ALERT "hall device interrupt handle failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 1 );

		return -1;
	}
	else
	{
		printk( KERN_ALERT "hall: Device irq number is %d\n", hall_b_irq );
	}
/*	if(request_irq(hall_b_irq, hallb_falling_handler, IRQF_TRIGGER_FALLING | IRQF_DISABLED, "gpiomod#hall", NULL ) == -1)
	{
		printk( KERN_ALERT "hall device interrupt handle failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 1 );

		return -1;
	}
	else
	{
		printk( KERN_ALERT "hall: Device irq number is %d\n", hall_b_irq );
	}*/
	if(request_irq(hall_in_irq, hallin_rising_handler, IRQF_TRIGGER_RISING | IRQF_DISABLED, "gpiomod#hall", NULL ) == -1)
	{
		printk( KERN_ALERT "hall device interrupt handle failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 1 );

		return -1;
	}
	else
	{
		printk( KERN_ALERT "hall: Device irq number is %d\n", hall_in_irq );
	}
	if(request_irq(hall_out_irq, hallout_rising_handler, IRQF_TRIGGER_RISING | IRQF_DISABLED, "gpiomod#hall", NULL ) == -1)
	{
		printk( KERN_ALERT "hall device interrupt handle failed\n" );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 1 );

		return -1;
	}
	else
	{
		printk( KERN_ALERT "hall: Device irq number is %d\n", hall_out_irq );
	}


	return 0;
}
	/*
	 * Module exit function
	 */
static void __exit hall_cleanup(void)
	{
		cdev_del( &c_dev );
		device_destroy( cl, first );
		class_destroy( cl );
		unregister_chrdev_region( first, 1 );

		printk(KERN_ALERT "Hall: Device unregistered\n");

		free_irq(hall_a_irq, NULL);
		free_irq(hall_b_irq, NULL);
		free_irq(hall_in_irq, NULL);
		free_irq(hall_out_irq, NULL);
		gpio_free(GPIO_HALLA);
		gpio_free(GPIO_HALLB);
		gpio_free(GPIO_IN);
		gpio_free(GPIO_OUT);
	}

	module_init(hall_init);
	module_exit(hall_cleanup);

	MODULE_AUTHOR("Bjorn Smith");
	MODULE_LICENSE("GPL");
	MODULE_DESCRIPTION("Actuator module monitoring hall signals from a Linak LA36");
