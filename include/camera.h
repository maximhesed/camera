/* This code doesn't have any copyrights and licenses.
 * Restrictions - is dogmas. Be open. */

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

struct data {
    guint tid; /* A timer ID. */

    gint fd;
    guint8 *storage; /* The frame in the YUV packed format. */
    struct v4l2_buffer buf[2];

    gulong ss_sid; /* A socket service signal ID. */
    GSocketService *sservice;
    struct sockaddr_in servaddr;
    gchar *addr; /* A remote address. */

    GtkWidget *area;
};

gint device_open(struct data *d);
gint caps_print(gint fd);
gint format_set(gint fd);
gint buf_req(gint fd);
gint buf_alloc(struct data *d);
gint buf_map(struct data *d);
gint frame_streamon(struct data *d);
gint frame_capture(struct data *d);
gint frame_streamoff(struct data *d);
gint buf_unmap(struct data *d);
gint device_close(gint fd);
guint8 * yuv_to_rgb(const guint8 *buffer);

#endif /* __CAMERA_H */
