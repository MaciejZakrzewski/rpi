#include "device_file.h"
#include <linux/init.h>       /* module_init, module_exit */
#include <linux/module.h> /* version info, MODULE_LICENSE, MODULE_AUTHOR, printk() */

/*===============================================================================================*/
static int rpilcd_driver_init(void) {
  int result = 0;
  printk(KERN_NOTICE "[RPILCD]: Initialization started");

  result = rpilcd_register_device();
  return result;
}

static void rpilcd_driver_exit(void) {
  printk(KERN_NOTICE "[RPILCD]: Exiting");
  rpilcd_unregister_device();
}

/*===============================================================================================*/
module_init(rpilcd_driver_init);
module_exit(rpilcd_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Maciej Zakrzewski");
MODULE_DESCRIPTION("LCD HD44780 driver for raspberry PI");
