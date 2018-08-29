#include "timeout.h"

bool auto_mode = false;

int in_fd = -1;
int in_watch = -1;
char in_buffer[IN_BUF_SIZE];

int num_dev = 0;
Device** device = NULL;
struct input_event event_buffer[EVENT_BUF_LEN];

int lightfd = -1;
char light = LIGHT_ON;

int timeout = DEFAULT_TIMEOUT;

struct timespec sleepTime = { .tv_sec = 0, .tv_nsec = SLEEP_NS }; 


int main(int argc, char* argv[])
{
    int i;

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage();
        }
        else {
            parse_timeout(argv[1]);
        }
    }

    if (argc > 2) {
        for (i = 2; i < argc; i++) {
            add_device(argv[i]);
        }
    }
    else {
        auto_mode = true;
        enumerate_devices();
    }

    setup_inotify();
    
    open_devices();

    open_light();

    printf("Starting...\n");

    time_t now = time(NULL);
    time_t last_event = now;

    while(1) {
        now = time(NULL);
                
        if (check_light()) {
            if (light == LIGHT_ON) {
                printf("Power enabled externally - Timeout reset\n");
            }
            if (light == LIGHT_OFF) {
                printf("Power disabled externally\n");
            }
            last_event = now;
        }

        update_inotify();

        for (i = 0; i < num_dev; i++) {
            if (check_device(i)) {
                last_event = now;
                set_light(LIGHT_ON);
            }
        }

        if(difftime(now, last_event) > timeout) {
            set_light(LIGHT_OFF);
        }

        nanosleep(&sleepTime, NULL);
    }
}

void print_usage()
{
    printf("\nUsage: timeout <timeout_sec> [<device>...]\n");
    printf("    Use lsinput to see input devices.\n");
    printf("    Device to use is shown as %s<device>\n\n", PATH_PREFIX);
    printf("    If no devices are specified, all eventX devices\n");
    printf("    are enumerated and monitored.\n\n");
    exit(1);
}

void parse_timeout(char* val)
{
    int i;
    int tlen = strlen(val);
    for (i = 0; i < tlen; i++) {
        if (!isdigit(val[i])) {
            printf ("Entered timeout value is not a number\n");
            print_usage();
        }
    }
    timeout = atoi(val);
}



//////////////////////////////////////////////////////////////////
// Inotify functions
//////////////////////////////////////////////////////////////////

void setup_inotify()
{
    in_fd = inotify_init1(IN_NONBLOCK);
    if (in_fd < 0) {
        printf("Error initializing inotify\n");
        exit(1);
    }

    in_watch = inotify_add_watch(in_fd, PATH_PREFIX, IN_CREATE | IN_DELETE);
    if (in_watch < 0) {
        printf("Error adding watch to %s\n", PATH_PREFIX);
        exit(1);
    }
}

void update_inotify()
{
    int i = 0;
    int size = read(in_fd, in_buffer, IN_BUF_SIZE);
    while (i < size) {
        struct inotify_event* event = (struct inotify_event*) &in_buffer[i];
        if (event->len) {
            if (strstr(event->name, "event")) {
                if (auto_mode) {
                    if (event->mask & IN_CREATE) {
                        printf("Device connected: %s\n", event->name);
                        add_device(event->name);
                    }
                    else if (event->mask & IN_DELETE) {
                        printf("Device disconnected: %s\n", event->name);
                        remove_device(event->name);
                    }
                }
                else if (event->mask & IN_DELETE) {
                    close_device_name(event->name);
                }
            }
        }
        i += IN_EVENT_SIZE + event->len;
    }
}



//////////////////////////////////////////////////////////////////
// Device list functions
//////////////////////////////////////////////////////////////////

int get_device(char* name)
{
    int i;
    for (i = 0; i < num_dev; i++) {
        if (strcmp(device[i]->name, name) == 0) {
            return i;
        }
    }
    return -1;
}

void add_device(char* name)
{
    num_dev++;

    if (num_dev == 1) {
        device = malloc(sizeof(Device*));
    }
    else {
        device = realloc(device, sizeof(Device*) * num_dev);
    }

    device[num_dev - 1] = malloc(sizeof(Device));
    device[num_dev - 1]->fd = -1;
    strcpy(device[num_dev - 1]->name, name);
}

void remove_device(char* name)
{
    if (num_dev == 0) return;

    int index = get_device(name);
    if (index == -1) return;
    
    close_device(index);
    free(device[index]);

    int i;
    for (i = index; i < num_dev - 1; i++) {
        device[i] = device[i + 1];
    }

    num_dev--;

    if (num_dev == 0) {
        free(device);
        device = NULL;
    }
    else {
        device = realloc(device, sizeof(Device*) * num_dev);
    }
}

void enumerate_devices()
{
    DIR* d;
    struct dirent* dir;
    d = opendir(PATH_PREFIX);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strstr(dir->d_name, "event")) {
                add_device(dir->d_name);
            }
        }
    }
}



//////////////////////////////////////////////////////////////////
// Device functions
//////////////////////////////////////////////////////////////////

void open_device(int index)
{
    char name[32] = PATH_PREFIX;
    strcat(name, device[index]->name);
    int event_dev = open(name, O_RDONLY | O_NONBLOCK);
    device[index]->fd = event_dev;

    if (event_dev != -1) {
        printf("Opened device %s\n", device[index]->name);
    }
}

void open_devices()
{
    int i;
    for (i = 0; i < num_dev; i++) {
        open_device(i);
        if (!auto_mode && device[i]->fd == -1) {
            printf("Error opening device %s\n", device[i]->name);
            exit(1);
        }
    }
}

void close_device(int index)
{
    close(device[index]->fd);
    device[index]->fd = -1;

    printf("Closed device %s\n", device[index]->name);   
}

void close_device_name(char* name)
{
    int index = get_device(name);
    if (index >= 0) {
        close_device(index);
    }
}

void close_devices()
{
    int i;
    for (i = 0; i < num_dev; i++) {
        close_device(i);
    }
}

bool check_device(int index)
{
    if (device[index]->fd == -1) {
        open_device(index);
    }

    int size = read(device[index]->fd, event_buffer, sizeof(struct input_event)*EVENT_BUF_LEN);
    if(size > 0) {
        printf("%s Value: %d, Code: %x\n", device[index]->name, event_buffer[0].value, event_buffer[0].code);
        return true;
    }
    return false;
}



//////////////////////////////////////////////////////////////////
// Backlight functions
//////////////////////////////////////////////////////////////////

bool check_light()
{
    char read_val;
    lseek(lightfd, 0, SEEK_SET);
    int size = read(lightfd, &read_val, sizeof(char));
    if (size > 0 && read_val != light) {
        light = read_val;
        return true;
    }
    return false;
}

void open_light()
{
    lightfd = open("/sys/class/backlight/rpi_backlight/bl_power", O_RDWR | O_NONBLOCK);

    if(lightfd == -1) {
        printf("Error opening backlight file: %d", errno);
        exit(1);
    }

    check_light();
}

void set_light(char val)
{
    if (light != val) {
        if (val == LIGHT_ON) {
            printf("Turning On\n");
        }
        if (val == LIGHT_OFF) {
            printf("Turning Off\n");
        }
        light = val;
        lseek(lightfd, 0, SEEK_SET);
        write(lightfd, &light, sizeof(char));
    }
}

