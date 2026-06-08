#include "camera_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#define CAMERA_NB_BUFFER 4
#define CAMERA_TAG "[CAMERA] "
#define cam_log(fmt, ...) printf(CAMERA_TAG fmt "\n", ##__VA_ARGS__)
#define cam_err(fmt, ...) fprintf(stderr, CAMERA_TAG "ERROR: " fmt "\n", ##__VA_ARGS__)

typedef struct {
    int fd; int width; int height; unsigned int pixelformat;
    int buf_count; int buf_max_len; int cur_buf_index;
    unsigned char *buffers[CAMERA_NB_BUFFER];
    int initialized; int streaming;
} camera_context_t;

static camera_context_t g_camera = {
    .fd = -1, .width = 640, .height = 480,
    .pixelformat = V4L2_PIX_FMT_MJPEG,
    .initialized = 0, .streaming = 0
};

static int camera_start_stream(void);
static int camera_stop_stream(void);

int camera_init(const char *device) {
    int fd, i, ret;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;

    if (g_camera.initialized) { cam_log("Already initialized"); return 0; }
    const char *dev = device ? device : CAMERA_DEFAULT_DEVICE;

    fd = open(dev, O_RDWR);
    if (fd < 0) { cam_err("Failed to open %s: %s", dev, strerror(errno)); return -1; }

    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) { cam_err("VIDIOC_QUERYCAP failed"); close(fd); return -1; }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) { cam_err("Not a capture device"); close(fd); return -1; }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) { cam_err("No streaming support"); close(fd); return -1; }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = g_camera.width;
    fmt.fmt.pix.height = g_camera.height;
    fmt.fmt.pix.pixelformat = g_camera.pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) { cam_err("VIDIOC_S_FMT failed"); close(fd); return -1; }
    g_camera.width = fmt.fmt.pix.width;
    g_camera.height = fmt.fmt.pix.height;
    g_camera.pixelformat = fmt.fmt.pix.pixelformat;
    cam_log("Format: %dx%d", g_camera.width, g_camera.height);

    memset(&req, 0, sizeof(req));
    req.count = CAMERA_NB_BUFFER; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) { cam_err("VIDIOC_REQBUFS failed"); close(fd); return -1; }
    g_camera.buf_count = req.count;

    for (i = 0; i < g_camera.buf_count; i++) {
        memset(&buf, 0, sizeof(buf)); buf.index = i; buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) { cam_err("VIDIOC_QUERYBUF failed"); goto err; }
        g_camera.buf_max_len = buf.length;
        g_camera.buffers[i] = mmap(NULL, buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (g_camera.buffers[i] == MAP_FAILED) { cam_err("mmap failed"); goto err; }
    }
    for (i = 0; i < g_camera.buf_count; i++) {
        memset(&buf, 0, sizeof(buf)); buf.index = i; buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) { cam_err("VIDIOC_QBUF failed"); goto err; }
    }
    g_camera.fd = fd; g_camera.initialized = 1;
    cam_log("Initialized: %s (%dx%d)", dev, g_camera.width, g_camera.height);
    return 0;
err:
    for (i = 0; i < g_camera.buf_count; i++)
        if (g_camera.buffers[i] && g_camera.buffers[i] != MAP_FAILED) { munmap(g_camera.buffers[i], g_camera.buf_max_len); g_camera.buffers[i] = NULL; }
    close(fd); return -1;
}

static int camera_start_stream(void) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (g_camera.streaming) return 0;
    if (ioctl(g_camera.fd, VIDIOC_STREAMON, &type) < 0) { cam_err("STREAMON failed"); return -1; }
    g_camera.streaming = 1; return 0;
}

static int camera_stop_stream(void) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (!g_camera.streaming) return 0;
    ioctl(g_camera.fd, VIDIOC_STREAMOFF, &type);
    g_camera.streaming = 0; return 0;
}

int camera_get_frame(camera_frame_t *frame) {
    struct pollfd fds[1]; struct v4l2_buffer buf; int ret;
    if (!g_camera.initialized || !frame) return -1;
    if (!g_camera.streaming && camera_start_stream() < 0) return -1;
    fds[0].fd = g_camera.fd; fds[0].events = POLLIN;
    if (poll(fds, 1, 5000) <= 0) return -1;
    memset(&buf, 0, sizeof(buf)); buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP;
    if (ioctl(g_camera.fd, VIDIOC_DQBUF, &buf) < 0) return -1;
    g_camera.cur_buf_index = buf.index;
    frame->data = g_camera.buffers[buf.index]; frame->width = g_camera.width;
    frame->height = g_camera.height; frame->size = buf.bytesused; frame->pixelformat = g_camera.pixelformat;
    return 0;
}

int camera_release_frame(camera_frame_t *frame) {
    struct v4l2_buffer buf;
    if (!g_camera.initialized || !frame) return -1;
    memset(&buf, 0, sizeof(buf)); buf.index = g_camera.cur_buf_index;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; buf.memory = V4L2_MEMORY_MMAP;
    return ioctl(g_camera.fd, VIDIOC_QBUF, &buf);
}

int camera_capture_jpeg(const char *filename) {
    camera_frame_t frame; FILE *fp;
    if (!g_camera.initialized || !filename) return -1;

    /* 丢弃前5帧：摄像头刚启动时自动曝光/AWB未稳定，前几帧常全黑或偏色 */
    for (int _i = 0; _i < 5; _i++) {
        camera_frame_t discard;
        if (camera_get_frame(&discard) == 0) {
            camera_release_frame(&discard);
        }
    }

    /* 丢弃完成后捕获稳定帧 */
    if (camera_get_frame(&frame) < 0) return -1;
    fp = fopen(filename, "wb");
    if (!fp) { camera_release_frame(&frame); return -1; }
    fwrite(frame.data, frame.size, 1, fp); fclose(fp);
    camera_release_frame(&frame);
    cam_log("Captured: %s (%d bytes)", filename, frame.size);
    return 0;
}

int camera_set_resolution(int w, int h) {
    if (w <= 0 || h > CAMERA_MAX_HEIGHT) return -1;
    g_camera.width = w; g_camera.height = h; return 0;
}

int camera_get_status(void) { return g_camera.initialized; }

void camera_cleanup(void) {
    int i;
    if (!g_camera.initialized) return;
    if (g_camera.streaming) camera_stop_stream();
    for (i = 0; i < g_camera.buf_count; i++)
        if (g_camera.buffers[i] && g_camera.buffers[i] != MAP_FAILED) { munmap(g_camera.buffers[i], g_camera.buf_max_len); g_camera.buffers[i] = NULL; }
    if (g_camera.fd >= 0) { close(g_camera.fd); g_camera.fd = -1; }
    g_camera.initialized = 0;
    cam_log("Cleaned up");
}

static const char b64c[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int camera_encode_base64(const unsigned char *input, int input_len, char *output, int output_size) {
    int i, j, out_len = 4 * ((input_len + 2) / 3);
    if (out_len >= output_size) return -1;
    for (i = 0, j = 0; i < input_len;) {
        unsigned int a = i < input_len ? input[i++] : 0;
        unsigned int b = i < input_len ? input[i++] : 0;
        unsigned int c = i < input_len ? input[i++] : 0;
        unsigned int t = (a << 16) | (b << 8) | c;
        output[j++] = b64c[(t >> 18) & 0x3F];
        output[j++] = b64c[(t >> 12) & 0x3F];
        output[j++] = b64c[(t >> 6) & 0x3F];
        output[j++] = b64c[t & 0x3F];
    }
    for (i = 0; i < (3 - (input_len % 3)) % 3; i++) output[out_len - 1 - i] = '=';
    output[out_len] = '\0';
    return out_len;
}

int camera_capture_base64(char *base64_buf, int buf_size) {
    camera_frame_t frame; int ret;
    if (!g_camera.initialized || !base64_buf || buf_size <= 0) return -1;
    if (camera_get_frame(&frame) < 0) return -1;
    ret = camera_encode_base64(frame.data, frame.size, base64_buf, buf_size);
    camera_release_frame(&frame);
    if (ret < 0) { cam_err("Base64 encode failed"); return -1; }
    return ret;
}
