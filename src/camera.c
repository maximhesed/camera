/* This code doesn't have any copyrights and licenses.
 * Restrictions - is dogmas. Be open. */

#include "camera.h"

gint device_open(struct data *d)
{
    d->fd = open("/dev/video0", O_RDWR);
    if (d->fd < 0) {
        perror("Failed to open the device");

        return -1;
    }

    return 0;
}

gint caps_print(gint fd)
{
    struct v4l2_capability caps;

    if (ioctl(fd, VIDIOC_QUERYCAP, &caps) < 0) {
        perror("Failed to get a device's capabilities");

        return -1;
    }

    g_print("Device info:\n");
    g_print("  Driver: %s\n", caps.driver);
    g_print("  Card: %s\n", caps.card);
    g_print("  Bus: %s\n", caps.bus_info);
    g_print("  Version: %d.%d\n\n",
        (caps.version >> 16) && 0xff,
        (caps.version >> 24) && 0xff);

    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        perror("Video capturing is not supported");

        return -1;
    }

    g_print("Video capturing is supported.\n");

    if (!(caps.capabilities & V4L2_CAP_STREAMING)) {
        perror("Frame streaming is not supported");

        return -1;
    }

    g_print("Frame streaming is supported.\n\n");

    return 0;
}

gint format_set(gint fd)
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

    g_print("Format info:\n");
    g_print("  Width: %d\n", PIX_WIDTH);
    g_print("  Height: %d\n\n", PIX_HEIGHT);

    return 0;
}

gint buf_req(gint fd)
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

gint buf_alloc(struct data *d)
{
    memset(&d->buf[0], 0, sizeof(d->buf[0]));

    d->buf[0].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    d->buf[0].memory = V4L2_MEMORY_MMAP;
    d->buf[0].index = 0;

    if (ioctl(d->fd, VIDIOC_QUERYBUF, &d->buf[0]) < 0) {
        perror("Failed to query the buffer");

        return -1;
    }

    return 0;
}

gint buf_map(struct data *d)
{
    d->storage = mmap(
        NULL,
        d->buf[0].length,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        d->fd,
        d->buf[0].m.offset);
    if (d->storage == MAP_FAILED) {
        perror("Failed to init the device");

        return -1;
    }

    memset(d->storage, 0, d->buf[0].length);

    return 0;
}

gint frame_streamon(struct data *d)
{
    gint type;

    memset(&d->buf[1], 0, sizeof(d->buf[1]));

    d->buf[1].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    d->buf[1].memory = V4L2_MEMORY_MMAP;
    d->buf[1].index = 0;

    if (ioctl(d->fd, VIDIOC_QBUF, &d->buf[1]) < 0) {
        perror("Failed to put the buffer into the queue");

        return -1;
    }

    type = d->buf[1].type;
    if (ioctl(d->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("Failed to activate the frame streaming");

        return -1;
    }

    return 0;
}

gint frame_capture(struct data *d)
{
    /* Get the buffer. */
    if (ioctl(d->fd, VIDIOC_DQBUF, &d->buf[1]) < 0) {
        perror("Failed to retrieve the buffer");

        return -1;
    }

    d->buf[1].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    d->buf[1].memory = V4L2_MEMORY_MMAP;

    if (ioctl(d->fd, VIDIOC_QBUF, &d->buf[1]) < 0) {
        perror("Failed to put the buffer into the queue");

        return -1;
    }

    return 0;
}

gint frame_streamoff(struct data *d)
{
    gint type;

    type = d->buf[1].type;
    if (ioctl(d->fd, VIDIOC_STREAMOFF, &type) < 0) {
        perror("Failed to deactivate the frame streaming");

        return -1;
    }

    return 0;
}

gint buf_unmap(struct data *d)
{
    if (munmap(d->storage, d->buf[0].length) == -1) {
        perror("Failed to deinit the device");

        return -1;
    }

    return 0;
}

gint device_close(gint fd)
{
    if (close(fd) == -1) {
        perror("Failed to close the device");

        return -1;
    }

    return 0;
}

/* Thank you very much, Redek (https://cutt.ly/2ryV2lz). */
guint8 * yuv_to_rgb(const guint8 *buffer)
{
    guint8 *rgb_raw = g_malloc0_n(RGB_RAW_LEN, sizeof(guint8));
    gint y;
    gint cr;
    gint cb;
    gdouble r;
    gdouble g;
    gdouble b;
    gint i;
    gint j;

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

        rgb_raw[i] = (guint8) r;
        rgb_raw[i + 1] = (guint8) g;
        rgb_raw[i + 2] = (guint8) b;

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

        rgb_raw[i + 3] = (guint8) r;
        rgb_raw[i + 4] = (guint8) g;
        rgb_raw[i + 5] = (guint8) b;
    }

    return rgb_raw;
}
