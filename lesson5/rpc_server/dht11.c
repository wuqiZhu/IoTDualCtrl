#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int fd;
static char g_humi, g_temp;
static pthread_t g_dht11_thread_id;
static int g_dht11_thread_created = 0;

void *dht11_thread(void *arg) {
  (void)arg;
  char buf[2];
  while (1) {
    pthread_testcancel();
    if (2 == read(fd, buf, 2)) {
      g_humi = buf[0];
      g_temp = buf[1];
      sleep(1);
    }
  }
}

void dht11_init(void) {
  fd = open("/dev/mydht11", O_RDWR | O_NONBLOCK);
  if (fd >= 0) {
    if (pthread_create(&g_dht11_thread_id, NULL, dht11_thread, NULL) == 0) {
      g_dht11_thread_created = 1;
    }
  }
}

void dht11_cleanup(void) {
  if (g_dht11_thread_created) {
    pthread_cancel(g_dht11_thread_id);
    pthread_join(g_dht11_thread_id, NULL);
    g_dht11_thread_created = 0;
  }
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
}

int dht11_read(char *humi, char *temp) {
  *humi = g_humi;
  *temp = g_temp;
  return 0;
}
