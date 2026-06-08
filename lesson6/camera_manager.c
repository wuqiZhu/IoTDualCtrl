#include "camera_manager.h"
#include "log.h"

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
#define CAMERA_LOG_TAG "CAMERA"

typedef struct {
    int fd;
    int width;
    int height;
    unsigned int pixelformat;
    int buf_count;
    int buf_max_len;
    int cur_buf_index;
    unsigned char *buffers[CAMERA_NB_BUFFER];
    int initialized;
    int streaming;
} camera_context_t;

static camera_context_t g_camera = {
    .fd = -1,
    .width = 640,
    .height = 480,
    .pixelformat = V4L2_PIX_FMT_MJPEG,
    .initialized = 0,
    .streaming = 0
};

static int camera_start_stream(void);
static int camera_stop_stream(void);

int camera_init(const char *device)
{
    int fd;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_requestbuffers req;
    struct v4l2_buffer buf;
    int i, ret;

    if (g_camera.initialized) {
        LOG_WARN("Camera already initialized");
        return 0;
    }

    const char *dev = device ? device : CAMERA_DEFAULT_DEVICE;

    fd = open(dev, O_RDWR);
    if (fd < 0) {
        LOG_ERROR("Failed to open %s: %s", dev, strerror(errno));
        return -1;
    }

    memset(&cap, 0, sizeof(cap));
    ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (ret < 0) {
        LOG_ERROR("VIDIOC_QUERYCAP failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOG_ERROR("%s is not a video capture device", dev);
        close(fd);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        LOG_ERROR("%s does not support streaming", dev);
        close(fd);
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = g_camera.width;
    fmt.fmt.pix.height = g_camera.height;
    fmt.fmt.pix.pixelformat = g_camera.pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;

    ret = ioctl(fd, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        LOG_ERROR("VIDIOC_S_FMT failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    g_camera.width = fmt.fmt.pix.width;
    g_camera.height = fmt.fmt.pix.height;
    g_camera.pixelformat = fmt.fmt.pix.pixelformat;

    LOG_INFO("Format: %dx%d, pixelformat=0x%x",
             g_camera.width, g_camera.height, g_camera.pixelformat);

    memset(&req, 0, sizeof(req));
    req.count = CAMERA_NB_BUFFER;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fd, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        LOG_ERROR("VIDIOC_REQBUFS failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    g_camera.buf_count = req.count;

    for (i = 0; i < g_camera.buf_count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);
        if (ret < 0) {
            LOG_ERROR("VIDIOC_QUERYBUF failed: %s", strerror(errno));
            goto err_cleanup;
        }

        g_camera.buf_max_len = buf.length;
        g_camera.buffers[i] = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                                   MAP_SHARED, fd, buf.m.offset);
        if (g_camera.buffers[i] == MAP_FAILED) {
            LOG_ERROR("mmap failed: %s", strerror(errno));
            goto err_cleanup;
        }
    }

    for (i = 0; i < g_camera.buf_count; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        ret = ioctl(fd, VIDIOC_QBUF, &buf);
        if (ret < 0) {
            LOG_ERROR("VIDIOC_QBUF failed: %s", strerror(errno));
            goto err_cleanup;
        }
    }

    g_camera.fd = fd;
    g_camera.initialized = 1;

    LOG_INFO("Camera initialized: %s (%dx%d)", dev, g_camera.width, g_camera.height);
    return 0;

err_cleanup:
    for (i = 0; i < g_camera.buf_count; i++) {
        if (g_camera.buffers[i] && g_camera.buffers[i] != MAP_FAILED) {
            munmap(g_camera.buffers[i], g_camera.buf_max_len);
            g_camera.buffers[i] = NULL;
        }
    }
    close(fd);
    return -1;
}

static int camera_start_stream(void)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    if (g_camera.streaming) {
        return 0;
    }

    ret = ioctl(g_camera.fd, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        LOG_ERROR("VIDIOC_STREAMON failed: %s", strerror(errno));
        return -1;
    }

    g_camera.streaming = 1;
    return 0;
}

static int camera_stop_stream(void)
{
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    if (!g_camera.streaming) {
        return 0;
    }

    ret = ioctl(g_camera.fd, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        LOG_ERROR("VIDIOC_STREAMOFF failed: %s", strerror(errno));
        return -1;
    }

    g_camera.streaming = 0;
    return 0;
}

int camera_get_frame(camera_frame_t *frame)
{
    struct pollfd fds[1];
    struct v4l2_buffer buf;
    int ret;

    if (!g_camera.initialized || !frame) {
        return -1;
    }

    if (!g_camera.streaming) {
        ret = camera_start_stream();
        if (ret < 0) {
            return -1;
        }
    }

    fds[0].fd = g_camera.fd;
    fds[0].events = POLLIN;

    ret = poll(fds, 1, 5000);
    if (ret <= 0) {
        LOG_ERROR("poll timeout or error: %s", strerror(errno));
        return -1;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(g_camera.fd, VIDIOC_DQBUF, &buf);
    if (ret < 0) {
        LOG_ERROR("VIDIOC_DQBUF failed: %s", strerror(errno));
        return -1;
    }

    g_camera.cur_buf_index = buf.index;

    frame->data = g_camera.buffers[buf.index];
    frame->width = g_camera.width;
    frame->height = g_camera.height;
    frame->size = buf.bytesused;
    frame->pixelformat = g_camera.pixelformat;

    return 0;
}

int camera_release_frame(camera_frame_t *frame)
{
    struct v4l2_buffer buf;

    if (!g_camera.initialized || !frame) {
        return -1;
    }

    memset(&buf, 0, sizeof(buf));
    buf.index = g_camera.cur_buf_index;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    return ioctl(g_camera.fd, VIDIOC_QBUF, &buf);
}

int camera_capture_jpeg(const char *filename)
{
    camera_frame_t frame;
    FILE *fp;
    int ret;

    if (!g_camera.initialized || !filename) {
        return -1;
    }

    ret = camera_get_frame(&frame);
    if (ret < 0) {
        return -1;
    }

    fp = fopen(filename, "wb");
    if (!fp) {
        LOG_ERROR("Failed to open %s: %s", filename, strerror(errno));
        camera_release_frame(&frame);
        return -1;
    }

    fwrite(frame.data, frame.size, 1, fp);
    fclose(fp);

    camera_release_frame(&frame);

    LOG_INFO("Captured: %s (%d bytes)", filename, frame.size);
    return 0;
}

int camera_set_resolution(int width, int height)
{
    if (width <= 0 || height <= 0 || width > CAMERA_MAX_WIDTH || height > CAMERA_MAX_HEIGHT) {
        return -1;
    }

    g_camera.width = width;
    g_camera.height = height;
    return 0;
}

int camera_get_status(void)
{
    return g_camera.initialized;
}

void camera_cleanup(void)
{
    int i;

    if (!g_camera.initialized) {
        return;
    }

    if (g_camera.streaming) {
        camera_stop_stream();
    }

    for (i = 0; i < g_camera.buf_count; i++) {
        if (g_camera.buffers[i] && g_camera.buffers[i] != MAP_FAILED) {
            munmap(g_camera.buffers[i], g_camera.buf_max_len);
            g_camera.buffers[i] = NULL;
        }
    }

    if (g_camera.fd >= 0) {
        close(g_camera.fd);
        g_camera.fd = -1;
    }

    g_camera.initialized = 0;
    LOG_INFO("Camera cleaned up");
}

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int camera_encode_base64(const unsigned char *input, int input_len,
                         char *output, int output_size)
{
    int i, j;
    int output_len = 4 * ((input_len + 2) / 3);

    if (output_len >= output_size) {
        return -1;
    }

    for (i = 0, j = 0; i < input_len;) {
        unsigned int octet_a = i < input_len ? input[i++] : 0;
        unsigned int octet_b = i < input_len ? input[i++] : 0;
        unsigned int octet_c = i < input_len ? input[i++] : 0;
        unsigned int triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        output[j++] = base64_chars[(triple >> 18) & 0x3F];
        output[j++] = base64_chars[(triple >> 12) & 0x3F];
        output[j++] = base64_chars[(triple >> 6) & 0x3F];
        output[j++] = base64_chars[triple & 0x3F];
    }

    for (i = 0; i < (3 - (input_len % 3)) % 3; i++) {
        output[output_len - 1 - i] = '=';
    }

    output[output_len] = '\0';
    return output_len;
}

int camera_capture_base64(char *base64_buf, int buf_size)
{
    camera_frame_t frame;
    int ret;

    if (!g_camera.initialized || !base64_buf || buf_size <= 0) {
        return -1;
    }

    ret = camera_get_frame(&frame);
    if (ret < 0) {
        return -1;
    }

    ret = camera_encode_base64(frame.data, frame.size, base64_buf, buf_size);

    camera_release_frame(&frame);

    if (ret < 0) {
        LOG_ERROR("Base64 encoding failed, buffer too small");
        return -1;
    }

    LOG_INFO("Captured and encoded: %d bytes -> %d base64 chars",
             frame.size, ret);
    return ret;
}
