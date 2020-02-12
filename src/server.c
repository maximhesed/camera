/* This code doesn't have any copyrights and licenses.
 * Restrictions - is dogmas. */

/* This is server, which sends frames, from the camera, to the network. */

#include <stdlib.h>
#include <errno.h>

#include "camera.h"

#define MAX_CONN 1024

enum send_status {
    SEND_STOP = -1,
    SEND_CONTINUE
};

struct source {
    GSource gsrc;
    GPollFD *gpfd[MAX_CONN + 1];
};

struct data {
    unsigned int fr_draw_tid;

    struct sockaddr_in servaddr;
    unsigned int port;

    GSocketService *sservice;
    unsigned long ss_sid; /* A socket service signal ID. */
    GSocketConnection *gconn[MAX_CONN];
    unsigned int conn_q;
    GSocket *gsock[MAX_CONN];

    struct source *src;

    GtkWidget *area;

    gboolean is_bcast;
};

struct box {
    struct data *d;
    struct dev_info *dinfo;
};

static void         broadcast_start(GtkWidget *widget, gpointer data);
static void         broadcast_stop(GtkWidget *widget, gpointer data);
static int          server_prepare(struct data *d);
static void         server_stop(struct data *d);
static gboolean     gsrc_dispatch(GSource *gsrc,
                                  GSourceFunc cb,
                                  gpointer data);
static gboolean     conn_accept(GSocketService *sservice,
                                GSocketConnection *gconn,
                                GObject *object,
                                gpointer data);
static void         conn_close(struct box *box, unsigned int connid);
static int          device_init(struct dev_info *dinfo);
static int          device_deinit(struct dev_info *dinfo);
static gboolean     frame_draw(gpointer data);
static int          frame_send(struct box *box, unsigned int connid);
static gboolean     area_redraw(GtkWidget *widget,
                                cairo_t *cr,
                                gpointer data);
static void         error_throw(GError *err);
static struct box * box_alloc(void);
static void         box_free(struct box *box);
static void         app_quit(GtkWidget *widget, gpointer data);

static GSourceFuncs gsf = {
    .prepare = NULL,
    .check = NULL,
    .dispatch = gsrc_dispatch,
    .finalize = NULL
};

int main(int argc, char *argv[])
{
    GtkWidget *window;
    GtkWidget *area;
    GtkWidget *btn_bd_start;
    GtkWidget *btn_bd_stop;
    GtkWidget *grid;

    struct box *box = box_alloc();

    if (argc != 2) {
        printf("Usage: %s port.\n", argv[0]);

        return -1;
    }

    gtk_init(NULL, NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Server");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    g_signal_connect(window, "destroy", G_CALLBACK(app_quit), box);

    area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, PIX_WIDTH, PIX_HEIGHT);
    g_signal_connect(area, "draw", G_CALLBACK(area_redraw), box);

    /* Start a frames broadcasting. */
    btn_bd_start = gtk_button_new_with_label("Start broadcasting");
    g_signal_connect(btn_bd_start, "clicked", G_CALLBACK(broadcast_start),
        box);

    /* Stop a frames broadcasting. */
    btn_bd_stop = gtk_button_new_with_label("Stop broadcasting");
    g_signal_connect(btn_bd_stop, "clicked", G_CALLBACK(broadcast_stop),
        box);

    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);

    gtk_grid_attach(GTK_GRID(grid), area,         0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_bd_start, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_bd_stop,  1, 1, 1, 1);

    box->d->port = atoi(argv[1]);
    box->d->area = area;

    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}

static void
broadcast_start(GtkWidget *widget, gpointer data)
{
    struct box *box = data;

    if (box->d->is_bcast) {
        printf("The server is already started.\n");

        return;
    }

    if (device_init(box->dinfo) == -1) {
        printf("Failed to init the device.\n");

        return;
    }

    /* Init the custom source. */
    box->d->src = (struct source *) g_source_new(&gsf, sizeof(struct source));
    box->d->src->gpfd[0] = g_source_add_unix_fd((GSource *) box->d->src,
        box->dinfo->fd, G_IO_IN);

    g_source_set_callback((GSource *) box->d->src, NULL, box, NULL);
    g_source_attach((GSource *) box->d->src, NULL);

    box->d->fr_draw_tid = g_timeout_add(40, frame_draw, box->d);

    if (server_prepare(box->d) == -1) {
        printf("Failed to prepare the server.\n");

        return;
    }

    /* Here, we are starting the our server, connecting the signal
     * to the socket service, to catch incoming connections. */
    box->d->ss_sid = g_signal_connect(box->d->sservice, "incoming",
        G_CALLBACK(conn_accept), box);

    g_socket_service_start(box->d->sservice);

    box->d->is_bcast = TRUE;
}

static void
broadcast_stop(GtkWidget *widget, gpointer data)
{
    struct box *box = data;

    if (!box->d->is_bcast) {
        printf("The server isn't started.\n");

        return;
    }

    /* Stop send the frames. */
    if (box->d->src != NULL)
        g_source_remove(g_source_get_id((GSource *) box->d->src));

    server_stop(box->d);

    /* Stop draw the frames. */
    g_source_remove(box->d->fr_draw_tid);

    if (device_deinit(box->dinfo) == -1) {
        printf("Failed to deinit the device.\n");

        return;
    }

    box->d->is_bcast = FALSE;

    /* Clear the context. */
    gtk_widget_queue_draw(box->d->area);
}

static gboolean
gsrc_dispatch(GSource *gsrc, GSourceFunc cb, gpointer data)
{
    struct source *src = (struct source *) gsrc;
    struct box *box = data;
    unsigned int i;

    if (src->gpfd[0] != NULL) {
        if (src->gpfd[0]->revents & G_IO_IN) {
            if (buf_capture(box->dinfo) == -1) {
                broadcast_stop(NULL, box);

                return G_SOURCE_REMOVE;
            }
        }
    }

    for (i = 1; i <= box->d->conn_q; i++) {
        if (src->gpfd[i] != NULL) {
            if (src->gpfd[i]->revents & G_IO_OUT) {
                if (frame_send(box, i - 1) == -1) {
                    conn_close(box, i - 1);

                    return G_SOURCE_CONTINUE;
                }
            }
        }
    }

    g_main_context_iteration(NULL, TRUE);

    return G_SOURCE_CONTINUE;
}

static gboolean
conn_accept(GSocketService *sservice,
            GSocketConnection *gconn,
            GObject *object,
            gpointer data)
{
    struct box *box = data;

    box->d->gconn[box->d->conn_q] = g_object_ref(gconn);
    box->d->gsock[box->d->conn_q] = g_socket_connection_get_socket(
        box->d->gconn[box->d->conn_q]);
    box->d->src = (struct source *) g_source_new(&gsf,
        sizeof(struct source));

    /* Add the connection's fd to the main event loop. */
    box->d->src->gpfd[box->d->conn_q + 1] = g_source_add_unix_fd(
        (GSource *) box->d->src,
        g_socket_get_fd(box->d->gsock[box->d->conn_q]), G_IO_OUT);

    g_source_set_callback((GSource *) box->d->src, NULL, box, NULL);
    g_source_attach((GSource *) box->d->src, NULL);

    box->d->conn_q++;

    printf("The connection was accepted.\n");

    return TRUE;
}

static void
conn_close(struct box *box, unsigned int connid)
{
    g_clear_object(&box->d->gconn[connid]);
}

static int
server_prepare(struct data *d)
{
    GSocketAddress *gaddr;
    struct sockaddr_in servaddr;
    gboolean status;
    GError *err = NULL;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(d->port);

    d->sservice = g_socket_service_new();

    /* I don't want start the socket service, now. */
    g_socket_service_stop(d->sservice);

    if (servaddr.sin_family == d->servaddr.sin_family
            && servaddr.sin_addr.s_addr == d->servaddr.sin_addr.s_addr
            && servaddr.sin_port == d->servaddr.sin_port)
        /* The address is already listened. So, we don't need to add
         * it to the socket listener again. This is not an error.
         * We just don't add the address to the listener, but
         * continue send a data. */

        return 0;

    d->servaddr = servaddr;

    /* Convert the servaddr to the GSocketAddress. */
    gaddr = g_socket_address_new_from_native(&servaddr, sizeof(servaddr));
    if (gaddr == NULL) {
        printf("Failed to convert a native to GSocketAddress.\n");

        return -1;
    }

    status = g_socket_listener_add_address((GSocketListener *) d->sservice,
        gaddr, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, NULL, NULL,
        &err);
    if (!status) {
        printf("Failed to add the address into the socket listener.\n");
        error_throw(err);

        return -1;
    }

    g_object_unref(gaddr);

    return 0;
}

static void
server_stop(struct data *d)
{
    unsigned int i;

    g_socket_service_stop(d->sservice);
    g_socket_listener_close((GSocketListener *) d->sservice);
    g_signal_handler_disconnect(d->sservice, d->ss_sid);
    d->ss_sid = 0;

    memset(&d->servaddr, 0, sizeof(struct sockaddr_in));

    for (i = 0; i < d->conn_q; i++)
        g_clear_object(&d->gconn[i]);

    d->conn_q = 0;

    g_clear_object(&d->sservice);
}

static int
device_init(struct dev_info *dinfo)
{
    if (device_open(dinfo) == -1)
        return -1;

    if (caps_print(dinfo->fd) == -1)
        return -1;

    if (format_set(dinfo->fd) == -1)
        return -1;

    if (buf_req(dinfo->fd) == -1)
        return -1;

    if (buf_alloc(dinfo) == -1)
        return -1;

    if (buf_map(dinfo) == -1)
        return -1;

    if (device_streamon(dinfo) == -1)
        return -1;

    return 0;
}

static int
device_deinit(struct dev_info *dinfo)
{
    if (device_streamoff(dinfo) == -1)
        return -1;

    if (buf_unmap(dinfo) == -1)
        return -1;

    if (device_close(dinfo->fd) == -1)
        return -1;

    return 0;
}

static gboolean
frame_draw(gpointer data)
{
    struct data *d = data;

    gtk_widget_queue_draw(d->area);

    return TRUE;
}

static int
frame_send(struct box *box, unsigned int connid)
{
    gssize bytes;
    GError *err = NULL;

    bytes = g_socket_send(box->d->gsock[connid],
        (const gchar *) box->dinfo->buffer, YUV_RAW_LEN, NULL, &err);
    if (bytes == -1) {
        if (errno == EPIPE) {
            printf("The connection was closed.\n");

            if (err)
                g_error_free(err);

            return SEND_STOP;
        }

        printf("Failed to send the frame.\n");
        error_throw(err);

        return SEND_STOP;
    }

    return SEND_CONTINUE;
}

static gboolean
area_redraw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct box *box = data;
    GdkPixbuf *pixbuf;
    guint8 *rgb_raw;
    const GdkRectangle rect = {
        .x = 0,
        .y = 0,
        .width = PIX_WIDTH,
        .height = PIX_HEIGHT
    };

    gdk_cairo_rectangle(cr, &rect);

    if (!box->d->is_bcast || buf_get_len(box->dinfo) < YUV_RAW_LEN) {
        const GdkRGBA bg = {
            .red = 0,
            .green = 0,
            .blue = 0,
            .alpha = 0
        };

        gdk_cairo_set_source_rgba(cr, &bg);

        cairo_fill(cr);

        return FALSE;
    }

    /* Draw the captured frame. */
    rgb_raw = yuv_to_rgb(box->dinfo->buffer);

    pixbuf = gdk_pixbuf_new_from_data(rgb_raw, GDK_COLORSPACE_RGB, FALSE,
        8, PIX_WIDTH, PIX_HEIGHT, 3 * PIX_WIDTH, NULL, NULL);

    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);

    cairo_fill(cr);

    return FALSE;
}

static void
error_throw(GError *err)
{
    if (err) {
        printf("%s\n", err->message);

        g_error_free(err);
    }
}

static struct box *
box_alloc(void)
{
    struct box *box = g_malloc0(sizeof(struct box));

    box->d = g_malloc0(sizeof(struct data));
    box->d->conn_q = 0;
    box->d->is_bcast = FALSE;

    box->dinfo = g_malloc0(sizeof(struct dev_info));

    return box;
}

static void
box_free(struct box *box)
{
    unsigned int i;

    /* Unmap the frames storage. */
    if (box->dinfo->buffer != NULL) {
        if (buf_unmap(box->dinfo) == -1)
            return;
    }

    for (i = 1; i <= box->d->conn_q; i++) {
        if (box->d->gconn[i] != NULL)
            g_object_unref(box->d->gconn[i]);
    }

    g_free(box->d);
    g_free(box->dinfo);
    g_free(box);
}

static void
app_quit(GtkWidget *widget, gpointer data)
{
    struct box *box = data;

    if (box->d->is_bcast)
        broadcast_stop(NULL, box);

    box_free(box);

    gtk_main_quit();
}
