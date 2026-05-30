#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int fd;

void led_init(void) {
  fd = open("/dev/100ask_led", O_RDWR);
  if (fd < 0) {
  }
}

void led_control(int on) {
  char buf[2];
  buf[0] = 0;

  if (on) {
    buf[1] = 0;
  } else {
    buf[1] = 1;
  }
  write(fd, buf, 2);
}
