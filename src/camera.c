/* This code doesn't have any copyrights and licenses.
 * Restrictions are dogmas. */

#include "camera.h"

int
device_open(struct dev_info *dinfo)
{
    dinfo->fd = open("/dev/video0", O_RDWR);
    if (dinfo->fd < 0) {
        perror("Failed to open the device");

        return -1;
    }

    return 0;
}

int
device_streamon(struct dev_info *dinfo)
{
    int type;

    memset(&dinfo->plane[1], 0, sizeof(dinfo->plane[1]));

    dinfo->plane[1].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dinfo->plane[1].memory = V4L2_MEMORY_MMAP;
    dinfo->plane[1].index = 0;

    if (ioctl(dinfo->fd, VIDIOC_QBUF, &dinfo->plane[1]) < 0) {
        perror("Failed to put the buffer into the queue");

        return -1;
    }

    type = dinfo->plane[1].type;
    if (ioctl(dinfo->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to activate a frame streaming");

        return -1;
    }

    return 0;
}

int
device_streamoff(struct dev_info *dinfo)
{
    int type;

    type = dinfo->plane[1].type;
    if (ioctl(dinfo->fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Failed to deactivate a frame streaming");

        return -1;
    }

    return 0;
}

int
device_close(int fd)
{
    if (close(fd) == -1) {
        perror("Failed to close the device");

        return -1;
    }

    return 0;
}

int
caps_print(int fd)
{
    struct v4l2_capability caps;

    if (ioctl(fd, VIDIOC_QUERYCAP, &caps) < 0) {
        perror("Failed to get the device's capabilities");

        return -1;
    }

    printf("Device info:\n");
    printf("  Driver: %s\n", caps.driver);
    printf("  Card: %s\n", caps.card);
    printf("  Bus: %s\n", caps.bus_info);
    printf("  Version: %d.%d\n\n",
        (caps.version >> 16) && 0xff,
        (caps.version >> 24) && 0xff);

    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        perror("A video capturing is not supported");

        return -1;
    }

    printf("A video capturing is supported.\n");

    if (!(caps.capabilities & V4L2_CAP_STREAMING)) {
        perror("The frame streaming is not supported");

        return -1;
    }

    printf("A frame streaming is supported.\n\n");

    return 0;
}

int
format_set(int fd)
{
    struct v4l2_format fmt;

    memset(&fmt, 0, sizeof(fmt));

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.width = PIX_WIDTH;
    fmt.fmt.pix.height = PIX_HEIGHT;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("Failed to set a frame's format");

        return -1;
    }

    printf("A format info:\n");
    printf("  Width: %d\n", PIX_WIDTH);
    printf("  Height: %d\n\n", PIX_HEIGHT);

    return 0;
}

int
buf_req(int fd)
{
    struct v4l2_requestbuffers rb;

    memset(&rb, 0, sizeof(rb));

    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    rb.count = 1;

    if (ioctl(fd, VIDIOC_REQBUFS, &rb) < 0) {
        perror("Failed to request the buffer");

        return -1;
    }

    return 0;
}

int
buf_alloc(struct dev_info *dinfo)
{
    memset(&dinfo->plane[0], 0, sizeof(dinfo->plane[0]));

    dinfo->plane[0].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dinfo->plane[0].memory = V4L2_MEMORY_MMAP;
    dinfo->plane[0].index = 0;

    if (ioctl(dinfo->fd, VIDIOC_QUERYBUF, &dinfo->plane[0]) < 0) {
        perror("Failed to query the buffer");

        return -1;
    }

    return 0;
}

int
buf_map(struct dev_info *dinfo)
{
    dinfo->buffer = mmap(NULL, dinfo->plane[0].length,
        PROT_READ | PROT_WRITE, MAP_SHARED, dinfo->fd,
        dinfo->plane[0].m.offset);
    if (dinfo->buffer == MAP_FAILED) {
        perror("Failed to init the device");

        return -1;
    }

    memset(dinfo->buffer, 0, dinfo->plane[0].length);

    return 0;
}

int
buf_capture(struct dev_info *dinfo)
{
    /* Get the buffer. */
    if (ioctl(dinfo->fd, VIDIOC_DQBUF, &dinfo->plane[1]) < 0) {
        perror("Failed to retrieve the buffer");

        return -1;
    }

    dinfo->plane[1].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dinfo->plane[1].memory = V4L2_MEMORY_MMAP;

    if (ioctl(dinfo->fd, VIDIOC_QBUF, &dinfo->plane[1]) < 0) {
        perror("Failed to put the buffer into the queue");

        return -1;
    }

    return 0;
}

int
buf_unmap(struct dev_info *dinfo)
{
    if (munmap(dinfo->buffer, dinfo->plane[0].length) == -1) {
        perror("Failed to deinit the device");

        return -1;
    }

    return 0;
}

size_t
buf_get_len(struct dev_info *dinfo)
{
    size_t len;

    len = dinfo->plane[1].length;

    return len;
}

/* Thank you very much, Redek (https://cutt.ly/2ryV2lz). */
uint8_t *
yuv_to_rgb(const uint8_t *buffer)
{
    uint8_t *rgb_raw = g_malloc0_n(RGB_RAW_LEN, sizeof(uint8_t));
    int y;
    int cr;
    int cb;
    double r;
    double g;
    double b;
    int i;
    int j;

    for (i = 0, j = 0; i < RGB_RAW_LEN; i += 6, j += 4) {
        y = buffer[j];
        cb = buffer[j + 1];
        cr = buffer[j + 3];

        r = y + (1.4065 * (cr - 128));
        g = y - (0.3455 * (cb - 128)) - (0.7169 * (cr - 128));
        b = y + (1.7790 * (cb - 128));

        if (r < 0)
            r = 0;
        else if (r > 255)
            r = 255;

        if (g < 0)
            g = 0;
        else if (g > 255)
            g = 255;

        if (b < 0)
            b = 0;
        else if (b > 255)
            b = 255;

        rgb_raw[i] = (uint8_t) r;
        rgb_raw[i + 1] = (uint8_t) g;
        rgb_raw[i + 2] = (uint8_t) b;

        y = buffer[j + 2];
        cb = buffer[j + 1];
        cr = buffer[j + 3];

        r = y + (1.4065 * (cr - 128));
        g = y - (0.3455 * (cb - 128)) - (0.7169 * (cr - 128));
        b = y + (1.7790 * (cb - 128));

        if (r < 0)
            r = 0;
        else if (r > 255)
            r = 255;

        if (g < 0)
            g = 0;
        else if (g > 255)
            g = 255;

        if (b < 0)
            b = 0;
        else if (b > 255)
            b = 255;

        rgb_raw[i + 3] = (uint8_t) r;
        rgb_raw[i + 4] = (uint8_t) g;
        rgb_raw[i + 5] = (uint8_t) b;
    }

    return rgb_raw;
}
