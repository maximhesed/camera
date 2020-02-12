/* This code doesn't have any copyrights and licenses.
 * Restrictions - is dogmas. */

#ifndef __CAMERA_H
#define __CAMERA_H

#include <gtk/gtk.h>

#include <stdio.h>
#include <string.h>

#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <netinet/in.h>

#define PIX_WIDTH 320
#define PIX_HEIGHT 240
#define YUV_RAW_LEN (PIX_WIDTH * PIX_HEIGHT * 2)
#define RGB_RAW_LEN (PIX_WIDTH * PIX_HEIGHT * 3)

struct dev_info {
    int fd;
    uint8_t *buffer; /* The frame in the YUV-packed format. */
    struct v4l2_buffer plane[2];
};

int       device_open(struct dev_info *dinfo);
int       device_streamon(struct dev_info *dinfo);
int       device_streamoff(struct dev_info *dinfo);
int       device_close(int fd);
int       caps_print(int fd);
int       format_set(int fd);
int       buf_req(int fd);
int       buf_alloc(struct dev_info *dinfo);
int       buf_map(struct dev_info *dinfo);
int       buf_capture(struct dev_info *dinfo);
int       buf_unmap(struct dev_info *dinfo);
size_t    buf_get_len(struct dev_info *dinfo);
uint8_t * yuv_to_rgb(const uint8_t *buffer);

#endif /* __CAMERA_H */
