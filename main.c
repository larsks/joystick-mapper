#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifndef USB_VENDOR
#define USB_VENDOR 0x00DD
#define USB_PRODUCT 0x0001
#define USB_VERSION 1
#endif // USB_VENDOR

const char *prog_name;
int loglevel = 0;

// Function to check if a string matches a glob pattern
bool matches_glob(const char *str, const char *pattern) {
  return 0 == fnmatch(pattern, str, 0);
}

void setup_uinput_device(int uinput_fd) {
  struct uinput_setup usetup;

  // Enable key events
  ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
  ioctl(uinput_fd, UI_SET_KEYBIT, KEY_UP);
  ioctl(uinput_fd, UI_SET_KEYBIT, KEY_DOWN);
  ioctl(uinput_fd, UI_SET_KEYBIT, KEY_LEFT);
  ioctl(uinput_fd, UI_SET_KEYBIT, KEY_RIGHT);

  // Clear the uidev structure
  memset(&usetup, 0, sizeof(usetup));

  // Set up uinput device for keyboard events
  snprintf(usetup.name, UINPUT_MAX_NAME_SIZE, "jsmapper");
  usetup.id.bustype = BUS_USB;
  usetup.id.vendor = USB_VENDOR;
  usetup.id.product = USB_PRODUCT;
  usetup.id.version = USB_VERSION;

  if (ioctl(uinput_fd, UI_DEV_SETUP, &usetup) < 0) {
    perror("failed to setup uinput device");
    exit(EXIT_FAILURE);
  }

  // Create the uinput device
  if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
    perror("failed to create uinput device");
    exit(EXIT_FAILURE);
  }
}

void emit(int uinput_fd, int type, int code, int value) {
  struct input_event ev;

  memset(&ev, 0, sizeof(struct input_event));
  ev.type = type;
  ev.code = code;
  ev.value = value;

  if (write(uinput_fd, &ev, sizeof(ev)) < 0) {
    perror("failed to write input event");
    exit(1);
  }
}

void send_key_event(int uinput_fd, int key, int value) {
  emit(uinput_fd, EV_KEY, key, value);
  emit(uinput_fd, EV_SYN, SYN_REPORT, 0);
}

int find_joystick_device(char *pattern) {
  struct dirent *entry;
  char device_path[256];
  char name[256];
  int fd;

  DIR *dp = opendir("/dev/input");
  if (dp == NULL) {
    perror("failed to open /dev/input");
    return -1;
  }

  while ((entry = readdir(dp)) != NULL) {
    if (entry->d_type == DT_CHR && strncmp(entry->d_name, "event", 5) == 0) {
      snprintf(device_path, sizeof(device_path), "/dev/input/%s",
               entry->d_name);
      fd = open(device_path, O_RDONLY);
      if (fd >= 0) {
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (matches_glob(name, pattern)) {
          closedir(dp);
          if (loglevel > 0)
            fprintf(stderr, "found device %s\n", device_path);
          return fd;
        }
        close(fd);
      }
    }
  }

  closedir(dp);
  return -1;
}

void usage(FILE *out) {
  fprintf(out, "%s: usage: %s [-h] pattern\n", prog_name, prog_name);
}

int main(int argc, char *argv[]) {
  int js_fd, uinput_fd;
  struct input_event ev;
  int ch;

  prog_name = argv[0];

  while (-1 != (ch = getopt(argc, argv, "hv"))) {
    switch (ch) {
    case 'h':
      usage(stdout);
      exit(0);
    case 'v':
      loglevel++;
      break;
    default:
      usage(stderr);
      exit(2);
    }
  }

  if (argc - optind < 1) {
    fprintf(stderr, "missing device path\n");
    usage(stderr);
    exit(2);
  }

  char *pattern = argv[optind];

  // Find the joystick device by name
  js_fd = find_joystick_device(pattern);
  if (js_fd < 0) {
    fprintf(stderr, "failed to locate device matching \"%s\"\n", pattern);
    return EXIT_FAILURE;
  }

  // Open the uinput device
  uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
  if (uinput_fd < 0) {
    perror("failed to open /dev/uinput");
    close(js_fd);
    return EXIT_FAILURE;
  }

  // Set up the uinput device
  setup_uinput_device(uinput_fd);

  // Read joystick events and map to keystrokes
  int last_x_key;
  int last_y_key;
  while (1) {
    if (read(js_fd, &ev, sizeof(struct input_event)) < 0) {
      perror("failed to read input event");
      break;
    }

    if (loglevel > 1)
      fprintf(stderr, "type %d code %d value %d\n", ev.type, ev.code, ev.value);

    if (ev.type == EV_ABS) {
      switch (ev.code) {
      case ABS_X:
        switch (ev.value) {
        case 0:
          last_x_key = KEY_RIGHT;
          send_key_event(uinput_fd, KEY_RIGHT, 1);
          break;
        case 255:
          last_x_key = KEY_LEFT;
          send_key_event(uinput_fd, KEY_LEFT, 1);
          break;
        case 127:
          send_key_event(uinput_fd, last_x_key, 0);
          break;
        }
        break;
      case ABS_Y:
        switch (ev.value) {
        case 0:
          last_y_key = KEY_DOWN;
          send_key_event(uinput_fd, last_y_key, 1);
          break;
        case 255:
          last_y_key = KEY_UP;
          send_key_event(uinput_fd, last_y_key, 1);
          break;
        case 127:
          send_key_event(uinput_fd, last_y_key, 0);
          break;
        }
        break;
      }
    }
  }

  // Clean up
  ioctl(uinput_fd, UI_DEV_DESTROY);
  close(uinput_fd);
  close(js_fd);

  return EXIT_SUCCESS;
}
