#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/time.h>
#include <linux/timer.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raphael Reyna <raphaelreyna@protonmail.com>");
MODULE_DESCRIPTION("Safely drive a heating coil while using a MAX 6675 IC to read the temperature");

#define CS_PIN 24
#define CLK_PIN 23
#define DATA_PIN 22
#define HEAT_PIN 6

#define TEMP_DEVICE_NAME "heatcoil.temp"
#define STATUS_DEVICE_NAME "heatcoil.status"

#define TEMP_MINOR 0
#define STATUS_MINOR 1

// Temperature limits given in Ticks (1 Tick = 0.25 C)
#define TEMP_LIMIT 2151 // 1000 F = 2151 Ticks
#define TEMP_LIMIT_2 2662 // 1050 F = 2622 Ticks

static int dev_open(struct inode*, struct file*);
static int dev_release(struct inode*, struct file*);
static ssize_t dev_read(struct file*, char*, size_t, loff_t*);
static ssize_t dev_write(struct file*, const char*, size_t, loff_t*);
static u16 temp_data(void);
static void turn_heating_coil_on(void);
static void turn_heating_coil_off(void);

static int major;

// Current state of the coil
static int heating = 0;
static u16 temp;

static struct file_operations fops = {
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
};

static struct task_struct *watchdog_thread;

/******************************************************************************/
/* watchdog_fn is the only process that reads from the MAX6675.               */
/* This function runs on its own thread periodically grabbing the temperature */
/* and making sure it doesn't exceed TEMP_LIMIT_2                             */
/******************************************************************************/
int watchdog_fn(void *v) {
  unsigned long j1;
  allow_signal(SIGKILL);
  while(!kthread_should_stop()) {
    // Take temperature every second
    j1 = jiffies + 1*HZ;
  	temp = temp_data();
    if ((TEMP_LIMIT_2 < temp) && heating ) {
      printk("THERMAL LIMIT EXCEEDED, TURNING OFF HEATING COIL\n");
      turn_heating_coil_off();
    }

    // Schedule something else if we're waiting to take temp again
    while (time_before(jiffies, j1)) {
      if (kthread_should_stop()) {
        return 0;
      } else {
        schedule();
      }
    }
    // Stop this thread if we get a SIGKILL
    if (signal_pending(watchdog_thread)) break;
  }
  return 0;
}

// Grab data from MAX6675 IC.
// Reference: https://datasheets.maximintegrated.com/en/ds/MAX6675.pdf
u16 temp_data(void){
  u16 data = 0;
  int i = 0;
  gpio_set_value(CS_PIN, 0);
  for (i = 0; i <= 15; i++) {
    gpio_set_value(CLK_PIN, 1);
    usleep_range(1000*10,1000*12);
    data = data << 1;
    if (gpio_get_value(DATA_PIN) == 1){
      data = data | 1;
    }
    gpio_set_value(CLK_PIN, 0);
    usleep_range(1000*10, 1000*12);
  }
  gpio_set_value(CS_PIN, 1);
  return ((data >> 3) & 0xFFF);
}

static void gpio_init(void) {
  printk(KERN_INFO "starting GPIO setup ...\n");
  gpio_request(CS_PIN, "CS");
  gpio_request(CLK_PIN, "CLK");
  gpio_request(DATA_PIN, "DATA");
  gpio_request(HEAT_PIN, "HEAT");

  gpio_direction_output(HEAT_PIN, 0);
  gpio_direction_output(CS_PIN, 1);
  gpio_direction_output(CLK_PIN, 0);
  gpio_direction_input(DATA_PIN);

  printk(KERN_INFO "... finished setting up GPIO\n");
}

static void gpio_exit(void) {
  printk(KERN_INFO "freeing GPIO ...\n");
  gpio_free(CS_PIN);
  gpio_free(CLK_PIN);
  gpio_free(DATA_PIN);
  gpio_free(HEAT_PIN);
  printk(KERN_INFO "... GPIO freed\n");
}

static int __init coil_init(void) {
  char threadName[] = "heater_watchdog_thread";
  major = register_chrdev(0, TEMP_DEVICE_NAME, &fops);
  if (major > 0){
    register_chrdev(major, STATUS_DEVICE_NAME, &fops);
  }

  if (major < 0) {
    printk(KERN_ALERT "heater coil module load failed\n");
    return major;
  }

  gpio_init();

  watchdog_thread = kthread_create(watchdog_fn, NULL, threadName);
  if (watchdog_thread) {
	  wake_up_process(watchdog_thread);
  }

  printk(KERN_INFO "heater coil module has been loaded: %d\n", major);
  return 0;
}

static void __exit coil_exit(void) {
  unregister_chrdev(major, TEMP_DEVICE_NAME);
  unregister_chrdev(major, STATUS_DEVICE_NAME);
  kthread_stop(watchdog_thread);
  gpio_exit();
  printk(KERN_INFO "heater coil module has been unloaded\n");
}

static void turn_heating_coil_on(void){
  if (TEMP_LIMIT < temp) return;
  heating = 1;
  gpio_set_value(HEAT_PIN, heating);
  printk("heating coil was turned on\n");
}

static void turn_heating_coil_off(void){
  gpio_set_value(HEAT_PIN, 0);
  heating = 0;
  printk("heating coil was turned off\n");
}

static int dev_open(struct inode *inodep, struct file *filep) {
  int minor;
  minor = iminor(inodep);
  switch (minor) {
  case TEMP_MINOR:
    break;
  case STATUS_MINOR:
    break;
  }
  return 0;
}

static ssize_t dev_write(struct file *filep, const char *buffer,
                         size_t len, loff_t *offset) {
   char *user_input = kmalloc(len, GFP_KERNEL);
   int minor = iminor(filep->f_inode);
   copy_from_user(user_input, buffer, len);
   switch (minor) {
   case TEMP_MINOR:
     break;
   case STATUS_MINOR:
     if (user_input[0] == '1') {
	     turn_heating_coil_on();
     } else {
	     turn_heating_coil_off();
     }
     break;
   }
   kfree(user_input);
   return len;
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int ret = 0;
    int minor = iminor(filep->f_inode);
    char temp_string[5];
    char status_string[2];

    switch (minor) {
    case TEMP_MINOR:
      sprintf(temp_string, "%hu\n", temp);
      ret = copy_to_user(buffer, temp_string, 5);
      return ret == 0 ? 5 : -EFAULT;

    case STATUS_MINOR:
      sprintf(status_string, "%d\n", heating);
      ret = copy_to_user(buffer, status_string, 2);
      return ret == 0 ? 2 : -EFAULT;
    }
    return -EFAULT;
}

static int dev_release(struct inode *inodep, struct file *filep) {
  int minor = iminor(filep->f_inode);
  // Make sure heating coil is off turned off if user is no longer actively controlling
  if (minor == STATUS_MINOR) turn_heating_coil_off();
  return 0;
}

module_init(coil_init);
module_exit(coil_exit);
