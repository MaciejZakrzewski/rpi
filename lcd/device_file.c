#include "device_file.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/delay.h>

#include <linux/types.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <asm/uaccess.h>

/**
 * LCD PIN NUMBERS CONNECTED to RaspberryPi
 */
#define LCD_RS          26
#define LCD_EN          19
#define LCD_D4          13
#define LCD_D5          6
#define LCD_D6          5
#define LCD_D7          11

/**
 * Device driver name and its corresponding class name.
 */
#define DEVICE_NAME     "rpilcd"
#define CLASS_NAME      "rpilcd_class"

/**
 * Global variable for the first device number
 */
static dev_t  gst_dev;
/**
 * Global variable for the device class
 */
static struct class* gpst_rpilcd_class = (struct class *)NULL;
/**
 * Global variable for the character device structure
 */
static struct cdev c_dev;

/**
 * Operations provided by this device driver
 */
int rpilcd_open(struct inode *inode, struct file *filp);
int rpilcd_release(struct inode *inode, struct file *filp);
ssize_t rpilcd_read(struct file *filp, char __user *buff, size_t count, loff_t *offp);
ssize_t rpilcd_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp);

static struct file_operations rpilcd_fops = {
  .owner      = THIS_MODULE,
  .read       = rpilcd_read,
  .write      = rpilcd_write,
  .open       = rpilcd_open,
  .release    = rpilcd_release,
};

/* representation of the device */
static struct rpilcd_dev_t {
  struct cdev st_cdev;            /* char device structure */
};

/*
 * three defined values for the flags (asm-generic/gpio.h) :
 * GPIOF_IN                     => GPIO defined as input
 * GPIOF_OUT_INIT_LOW   => GPIO defined as output, initial level LOW
 * GPIOF_OUT_INIT_HIGH  => GPIO defined as output, initial level HIGH
 */
static struct gpio rpilcd_gpios[] = {
        { LCD_RS, GPIOF_OUT_INIT_LOW, "LCD_RS" },
        { LCD_EN, GPIOF_OUT_INIT_LOW, "LCD_EN" },
        { LCD_D4, GPIOF_OUT_INIT_LOW, "LCD_D4" },
        { LCD_D5, GPIOF_OUT_INIT_LOW, "LCD_D5" },
        { LCD_D6, GPIOF_OUT_INIT_LOW, "LCD_D6" },
        { LCD_D7, GPIOF_OUT_INIT_LOW, "LCD_D7" },
};

struct rpilcd_dev_t * pst_rpilcd = (struct rpilcd_dev_t *)NULL;

/*===============================================================================================*/
/*
 * write a byte to lcd HD44780 controller
 */
void rpilcd_write_byte(const unsigned char /* in */ ui8_byte) {
  gpio_set_value(LCD_D4, (ui8_byte >> 4) & 0x01);
  gpio_set_value(LCD_D5, (ui8_byte >> 5) & 0x01);
  gpio_set_value(LCD_D6, (ui8_byte >> 6) & 0x01);
  gpio_set_value(LCD_D7, (ui8_byte >> 7) & 0x01);

  gpio_set_value(LCD_EN, 1);
  udelay(1);
  gpio_set_value(LCD_EN, 0);
  if(gpio_get_value(LCD_RS) == 1) {
    udelay(200);
  }
  else {
    usleep_range(4500, 5500);
  }
  gpio_set_value(LCD_D4, ui8_byte & 0x01);
  gpio_set_value(LCD_D5, (ui8_byte >> 1) & 0x01);
  gpio_set_value(LCD_D6, (ui8_byte >> 2) & 0x01);
  gpio_set_value(LCD_D7, (ui8_byte >> 3) & 0x01);

  gpio_set_value(LCD_EN, 1);
  udelay(1);
  gpio_set_value(LCD_EN, 0);
  if(gpio_get_value(LCD_RS) == 1) {
    udelay(200);
  }
  else {
    usleep_range(4500, 5500);
  }
}

/*===============================================================================================*/
/**
 * set current cursor position. Starts from row=1 and column=1
 */
void rpilcd_set_cursor(const unsigned char /* in */ ui8_row,
                       const unsigned char /* in */ ui8_column) {
  uint8_t ui8_command = 0x80;
  gpio_set_value(LCD_RS, 0);
  switch(ui8_row) {
    case 1:
      ui8_command += ui8_column - 1;
      break;
    case 2:
      ui8_command += 0x40 + (ui8_column - 1);
      break;
    default:
      break;
  }

  rpilcd_write_byte(ui8_command);
}

/*===============================================================================================*/

/*
 * Write a string of chars to the LCD
 */
int rpilcd_put_string(const char * /* in */ sz_string) {
  if(sz_string != (char *)NULL) {
    gpio_set_value(LCD_RS, 1);     // write characters
    while(*sz_string != '\0') {
      rpilcd_write_byte(*sz_string++);
    }
    gpio_set_value(LCD_RS, 0);
    return 0;
  }
  else {
    return -1;
  }
}

/*===============================================================================================*/
/*
 * Write one character to the LCD
 */
void rpilcd_put_char(const char /* in */ i8_char) {
  gpio_set_value(LCD_RS, 1);     // write character
  rpilcd_write_byte(i8_char);
  gpio_set_value(LCD_RS, 0);
}

/*===============================================================================================*/
/*
 * clear HD44780 lcd controller
 */
void rpilcd_clear_display(void) {
  gpio_set_value(LCD_RS, 0);
  rpilcd_write_byte(0x01);
}

/*===============================================================================================*/
/*
 * initialise HD44780 lcd controller
 * voir http://www.mjmwired.net/kernel/Documentation/timers/timers-howto.txt
 */
int rpilcd_init_display(void) {
  int i32_ret = 0;
  /* Wait for more than 15 ms after VCC rises to 4.5 V */
  usleep_range(15000, 16000);

  /*
   *  RS R/W DB7 DB6 DB5 DB4
   * 0   0   0   0   1   1
   */
  gpio_set_value(LCD_D4, 1);
  gpio_set_value(LCD_D5, 1);
  gpio_set_value(LCD_D6, 0);
  gpio_set_value(LCD_D7, 0);

  gpio_set_value(LCD_EN, 1);
  udelay(1);
  gpio_set_value(LCD_EN, 0);

  /* Wait for more than 4.1 ms */
  usleep_range(4200, 5000);

  gpio_set_value(LCD_EN, 1);
  udelay(1);
  gpio_set_value(LCD_EN, 0);

  /* Wait for more than 100 μs */
  udelay(200);
  gpio_set_value(LCD_EN, 1);
  udelay(1);
  gpio_set_value(LCD_EN, 0);
  udelay(200);

  // RS R/W DB7 DB6 DB5 DB4
  // 0   0   0   0   1   0  => interface four bits mode
  gpio_set_value(LCD_D4, 0);
  gpio_set_value(LCD_D5, 1);
  gpio_set_value(LCD_D6, 0);
  gpio_set_value(LCD_D7, 0);

  gpio_set_value(LCD_EN, 1);
  udelay(1);
  gpio_set_value(LCD_EN, 0);

  usleep_range(4200, 5000);

  /* => Set interface length - 4 bits, 2 lines
   * RS R/W DB7 DB6 DB5 DB4
   * 0   0   0   0   1   0
   * 0   0   N   F   *   *
   */
  rpilcd_write_byte(0x28);

  /* => Display On, Cursor On, Cursor Blink Off
   * RS R/W DB7 DB6 DB5 DB4
   * 0   0   0   0   0   0
   * 0   0   1   0   0   0
   */
  rpilcd_write_byte(0x08);

  /* => Clear screen
   * RS R/W DB7 DB6 DB5 DB4
   * 0   0   0   0   0   0
   * 0   0   0   0   0   1
   */
  rpilcd_write_byte(0x01);

  /* => Set entry Mode
   * RS R/W DB7 DB6 DB5 DB4
   * 0   0   0   0   0   0
   * 0   0   0   1   D   S
   */
  rpilcd_write_byte(0x06);

  /* RS R/W DB7 DB6 DB5 DB4 - on LCD without cursor
   * 0   0   0   0   0   0
   * 0   0   1   D   C   B
   */
  //rpilcd_write_byte(0x0C);
  /* RS R/W DB7 DB6 DB5 DB4 - on LCD cursor on, blinking on
   * 0   0   0   0   0   0
   * 0   0   1   1   1   1
   */
  //rpilcd_write_byte(0x0F);
  /* RS R/W DB7 DB6 DB5 DB4 - on LCD cursor on, blinking off
   * 0   0   0   0   0   0
   * 0   0   1   1   1   1
   */
  rpilcd_write_byte(0x0E);


  return i32_ret;
}

/*===============================================================================================*/
/*
 * same function as pow(x,p)
 */
int rpilcd_pow(const int i32_value, const int i32_power) {
  int i32_idx = 0;
  int i32_ret = 1;
  if(i32_value > 0) {
    i32_ret = i32_power;
  }
  /* else nothing to do */

  for(i32_idx = 1; i32_idx < i32_value; i32_idx ++) {
    i32_ret *= i32_power;
  }
  return i32_ret;
}

/*===============================================================================================*/
/*
 * convert number from ascii buf to integer value
 */
int rpilcd_atoi(const char * const buf, const size_t count, int * const pi32_value) {
  const char * pst_buf = &buf[0];
  int i32_idx = 0;
  *pi32_value = 0;
  for(i32_idx = (int)count - 1; i32_idx >= 0; i32_idx --) {
    if((pst_buf[i32_idx] >= 0x30) && (pst_buf[i32_idx] <= 0x39)) {
      *pi32_value += (pst_buf[i32_idx] - 0x30) * (rpilcd_pow(((int)(count - 1) - i32_idx), 10));
    } else { /* error byte is not a number */
      return -1;
    }
  }
  return 0;
}

/*===============================================================================================*/
/*
 * Open method
 */
int rpilcd_open(struct inode *inode, struct file *filp) {
  struct rpilcd_dev_t * pst_rpilcd = (struct rpilcd_dev_t *)NULL;
  printk(KERN_INFO "[RPILCD] rpilcd_open\n");
  pst_rpilcd = container_of(inode->i_cdev, struct rpilcd_dev_t, st_cdev);
  /* store pointer to it in the private_data field of the file structure
* for easier access in the future */
  filp->private_data = pst_rpilcd;
  return 0;
}

/*===============================================================================================*/
/*
 * Release method
 */
int rpilcd_release(struct inode *inode, struct file *filp) {
  printk(KERN_INFO "[RPILCD] rpilcd_release\n");
  return 0;
}

/*===============================================================================================*/
/*
 * Read method
 */
ssize_t rpilcd_read(struct file *filp, char __user *buff, size_t count, loff_t *offp) {
  /* retreive pointer from private data */
  struct rpilcd_dev_t * pst_rpilcd = filp->private_data;
  printk(KERN_INFO "[RPILCD] rpilcd_read\n");
  size_t i32_ret = -1;

  //~ i32_ret = gpio_get_value(PIN_LED);
  //~ pst_rpilcd->ui8_value = (unsigned char)i32_ret;
  //~ snprintf(sz_value, sizeof(sz_value), "%d\n", pst_rpilcd->ui8_value);
  //~ i32_ret = sizeof(sz_value) + 1;
  //~
  //~ if(i32_ret > count) {
          //~ return -ENOMEM;
  //~ }
  //~ /* else nothing to do */

  //~ if(copy_to_user(buff, sz_value, i32_ret)) {
          //~ return -EFAULT;
  //~ }
  //~ /* else nothing to do */

  return i32_ret;
}

/*===============================================================================================*/
#define MAX_LEN 16
int curRow = 1;
int curCol = 1;
int col1len = 0;
int col2len = 0;
char line1[MAX_LEN+1] = "";
char line2[MAX_LEN+1] = "";

ssize_t rpilcd_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp) {
  bool ctrl = false;
  int buffCount = count;

  printk(KERN_INFO "[RPILCD] write (%d) %s\n", count, buff);
  printk(KERN_INFO "[RPILCD] BEFORE WRITE Line1(%d, %d) %s\n", col1len, curCol, line1);
  printk(KERN_INFO "[RPILCD] BEFORE WRITE Line2(%d, %d) %s\n", col2len, curCol, line2);
  //rpilcd_clear_display();
  //rpilcd_set_cursor(1, 1);

  if (count == 3) {
    printk(KERN_INFO "[RPILCD] potentially control character: %s\n", buff);
    if (strncmp("\\n", buff, 2) == 0) {
      if (curRow == 1) {
        curRow = 2;
        curCol = 1 + col2len;
      }
      ctrl = true;
    }
    if (strncmp("\\p", buff, 2) == 0) {
      if (curRow == 2) {
        curRow = 1;
        curCol = 1 + col1len;
      }
      ctrl = true;
    }
    if (strncmp("\\r", buff, 2) == 0) {
      if ((curRow == 1 && curCol) < col1len+1 || (curRow == 2 && curCol) < col2len+1) {
        curCol += 1;
      }
      ctrl = true;
    }
    if (strncmp("\\l", buff, 2) == 0) {
      if (curCol > 1) {
        curCol -= 1;
      }
      ctrl = true;
    }
    if (strncmp("\\d", buff, 2) == 0) {
      if (curCol > 1) {
        if (curRow == 1) {
          if (curCol-1==col1len) {
            line1[curCol-2] = '\0';
            col1len -= 1;
            curCol -= 1;
          }
          else {
            strcpy(line1+curCol-1,line1+curCol);
            col1len -= 1;
          }
        }
        else if (curRow == 2) {
          if (curCol-1==col2len) {
            line2[curCol-2] = '\0';
            col2len -= 1;
            curCol -= 1;
          }
          else {
            strcpy(line2+curCol-1,line2+curCol);
            col2len -= 1;
          }
        }
      }
      else {
        if (curRow == 1) {
          line1[curCol-1] = '\0';
        }
        else {
          line2[curCol-1] = '\0';
        }
      }
      ctrl = true;
    }
    if (strncmp("\\c", buff, 2) == 0) {
      strcpy(line1, "");
      col1len = 0;
      strcpy(line2, "");
      col2len = 0;
      curRow = 1;
      curCol = 1;
      ctrl = true;
    }
  }

  if (!ctrl) {
    char msg[MAX_LEN*2+1] = "";
    if (count > MAX_LEN) {
      count = MAX_LEN;
    }
    if (copy_from_user(msg, buff, count) != 0) {
      printk(KERN_ALERT "[RPILCD] Copy string failed\n");
      return -EFAULT;
    }
    msg[count] = '\0';
    size_t msglen = strlen(msg);
    printk(KERN_INFO "[Simple-driver] Buffer (len: %d): %s\n", msglen, msg);
    if (curRow == 2 && curCol+msglen-1 > MAX_LEN) {
      msg[MAX_LEN-curCol+1] = '\0';
    }
    else if (curRow == 1 && curCol+msglen-1 > MAX_LEN*2) {
      msg[MAX_LEN*2-curCol+1] = '\0';
    }
    if (curRow == 1) {
      if (curCol+msglen-1 > MAX_LEN) {
        int ctoline1 = MAX_LEN-(curCol-1);
        strncpy(line1+curCol-1, msg, ctoline1);
        curRow = 2;
        curCol = 1;
        strcpy(line2, msg+ctoline1);
        curCol = msglen-ctoline1;
      }
      else {
        strcpy(line1+curCol-1, msg);
        curCol += strlen(msg) - 1;
        col1len = strlen(line1) - 1;
      }
    }
    else if (curRow == 2) {
      strcpy(line2+curCol-1, msg);
      curCol += strlen(msg) - 1;
      col2len = strlen(line2) -1;
    }
  }

  printk(KERN_INFO "[RPILCD] AFTER WRITE current ROW: %d\n", curRow);
  printk(KERN_INFO "[RPILCD] AFTER WRITE Line1(len=%d, curCol=%d) %s\n", col1len, curCol, line1);
  printk(KERN_INFO "[RPILCD] AFTER WRITE Line2(len=%d, curCol=%d) %s\n", col2len, curCol, line2);

  rpilcd_clear_display();
  rpilcd_set_cursor(1, 1);
  rpilcd_put_string(line1);
  rpilcd_set_cursor(2, 1);
  rpilcd_put_string(line2);
  rpilcd_set_cursor(curRow, curCol);

  return buffCount;
}
/*===============================================================================================*/
/*
 * Initialize the driver.
 */
int __init rpilcd_register_device(void) {
  int i32_ret = -1;
  int result = 0;

  printk(KERN_NOTICE "[RPILCD] init_rpilcd is called." );

  result = alloc_chrdev_region(&gst_dev, 0, 1, DEVICE_NAME);

  if (0 > result) {
    printk(KERN_ALERT "[RPILCD] device registration failed\n" );
    return -1;
  }

  if ((gpst_rpilcd_class = class_create(THIS_MODULE, CLASS_NAME) ) == NULL) {
    printk(KERN_ALERT "[RPILCD] class creation failed\n" );
    unregister_chrdev_region(gst_dev, 1);
    return -1;
  }

  if (device_create(gpst_rpilcd_class, NULL, gst_dev, NULL, DEVICE_NAME) == NULL) {
    printk(KERN_ALERT "[RPILCD] device creation failed\n" );
    class_destroy(gpst_rpilcd_class);
    unregister_chrdev_region(gst_dev, 1 );
    return -1;
  }

  cdev_init(&c_dev, &rpilcd_fops);

  if (cdev_add(&c_dev, gst_dev, 1 ) == -1) {
    printk(KERN_ALERT "[RPILCD] device addition failed\n" );
    device_destroy(gpst_rpilcd_class, gst_dev);
    class_destroy(gpst_rpilcd_class);
    unregister_chrdev_region(gst_dev, 1 );
    return -1;
  }

  //---- INIT WITHOUT DEVICE CREATION
  /* allocate a private structure and reference it as driver’s data */
  /*pst_rpilcd = (struct rpilcd_dev_t *)kmalloc(sizeof(struct rpilcd_dev_t), GFP_KERNEL);
  if(pst_rpilcd == (struct rpilcd_dev_t *)NULL) {
          printk(KERN_WARNING "[RPILCD] error mem struct\n");
          return -1;
  }

  result = register_chrdev( 0, DEVICE_NAME, &rpilcd_fops );
  if( result < 0 )
  {
    printk( KERN_WARNING "[RPILCD]:  can\'t register character device with errorcode = %i", result );
    return result;
  }*/
  //----

  // request multiple GPIOs
  i32_ret = gpio_request_array(rpilcd_gpios, ARRAY_SIZE(rpilcd_gpios));
  if(i32_ret != 0) {
    printk(KERN_WARNING "[RPILCD] Error request multiple gpios\n");
    device_destroy(gpst_rpilcd_class, gst_dev);
    return -1;
  }

  // init lcd screen
  rpilcd_init_display();
  rpilcd_clear_display();
  rpilcd_set_cursor(1, 1);

  printk(KERN_ALERT "[RPILCD] LOADED\n");

  return 0;
}

/*===============================================================================================*/
/*
 * Cleanup and unregister the driver.
 */
void __exit rpilcd_unregister_device(void) {
    /* release multiple GPIOs */
    gpio_free_array(rpilcd_gpios, ARRAY_SIZE(rpilcd_gpios));

    cdev_del(&c_dev);
    device_destroy(gpst_rpilcd_class, gst_dev);
    class_destroy(gpst_rpilcd_class);
    kfree(pst_rpilcd);
    /* unregistred driver from the kernel */
    unregister_chrdev_region(gst_dev, 1);
}
