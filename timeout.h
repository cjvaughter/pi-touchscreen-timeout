/* timeout - a little program to blank the RPi touchscreen and unblank it
   on touch.  Original by https://github.com/timothyhollabaugh

   2018-04-16 - Joe Hartley, https://github.com/JoeHartley3
     Added command line parameters for input device and timeout in seconds
     Added nanosleep() to the loop to bring CPU usage from 100% on a single core to around 1%

   Note that when not running X Windows, the console framebuffer may blank and not return on touch.
   Use one of the following fixes:

   * Raspbian Jessie
     Add the following line to /etc/rc.local (on the line before the final exit 0) and reboot:
         sh -c "TERM=linux setterm -blank 0 >/dev/tty0"
     Even though /dev/tty0 is used, this should propagate across all terminals.

   * Raspbian Wheezy
     Edit /etc/kbd/config and change the values for the variable shown below, then reboot:
       BLANK_TIME=0

   2018-04-23 - Moved nanosleep() outside of last if statement, fixed help screen to be consistent with binary name

   2018-08-22 - CJ Vaughter, https://github.com/cjvaughter
     Added support for multiple input devices
     Added external backlight change detection
*/

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <time.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/inotify.h>

#define PATH_PREFIX "/dev/input/"
#define EVENT_BUF_LEN 64
#define IN_BUF_LEN 64
#define IN_EVENT_SIZE (sizeof(struct inotify_event) + 16)
#define IN_BUF_SIZE (IN_BUF_LEN * IN_EVENT_SIZE)

#define DEFAULT_TIMEOUT 300 // 5 minutes
#define SLEEP_NS 100000000L // 1 second
#define LIGHT_OFF '1'
#define LIGHT_ON '0'

typedef struct {
    int fd;
    char name[32];
} Device;

void print_usage();
void parse_timeout();

// Inotify functions
void setup_inotify();
void update_inotify();

// Device list function
int get_device(char* name);
void add_device(char* name);
void remove_device(char* name);
void enumerate_devices();

// Device functions
void open_device(int index);
void open_devices();
void close_device(int index);
void close_device_name(char* name);
void close_devices();
bool check_device(int index);

// Backlight functions
void open_light();
void close_light();
void set_light(char val);
bool check_light();

