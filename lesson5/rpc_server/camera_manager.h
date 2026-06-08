#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#define CAMERA_MAX_WIDTH  1280
#define CAMERA_MAX_HEIGHT 960
#define CAMERA_DEFAULT_DEVICE "/dev/video1"
#define CAMERA_MAX_BASE64_SIZE (64 * 1024)

typedef struct {
    unsigned char *data;
    int width;
    int height;
    int size;
    unsigned int pixelformat;
} camera_frame_t;

int camera_init(const char *device);
int camera_capture_jpeg(const char *filename);
int camera_get_frame(camera_frame_t *frame);
int camera_release_frame(camera_frame_t *frame);
int camera_set_resolution(int width, int height);
int camera_get_status(void);
int camera_encode_base64(const unsigned char *input, int input_len,
                         char *output, int output_size);
int camera_capture_base64(char *base64_buf, int buf_size);
void camera_cleanup(void);

#endif
