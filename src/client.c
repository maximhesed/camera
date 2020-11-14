/* This code doesn't have any copyrights and licenses.
 * Restrictions are dogmas. */

/* This is client, which receives frames, from the camera. */

#include <stdlib.h>
#include <arpa/inet.h>

#include "camera.h"

enum receive_status {
    RECEIVE_STOP = -1,
    RECEIVE_CONTINUE
};

struct source {
    GSource gsrc;
    GPollFD *gpfd;
    gsize bytes_r;
};

struct data {
    unsigned int fr_draw_tid;

    struct sockaddr_in servaddr;
    char *addr; /* A remote address. */
    unsigned int port;

    GSocket *gsock;

    struct source *src;

    uint8_t *buf; /* The raw frame. */
    GtkWidget *area;

    gboolean is_recv;
};

static gboolean      gsrc_dispatch(GSource *gsrc,
                                   GSourceFunc cb,
                                   gpointer data);
static int           receive_prepare(struct data *d);
static void          receive_start(GtkWidget *widget, gpointer data);
static void          receive_stop(GtkWidget *widget, gpointer data);
static int           frame_receive(struct data *d);
static gboolean      frame_draw(gpointer data);
static gboolean      area_redraw(GtkWidget *widget,
                                 cairo_t *cr,
                                 gpointer data);
static void          error_throw(GError *err);
static struct data * data_alloc(void);
static void          data_free(struct data *d);
static void          app_quit(GtkWidget *widget, gpointer data);

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
    GtkWidget *btn_rc_start;
    GtkWidget *btn_rc_stop;
    GtkWidget *grid;

    struct data *data = data_alloc();

    if (argc != 3) {
        printf("Usage: %s addr port.\n", argv[0]);

        return -1;
    }

    gtk_init(NULL, NULL);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Client");
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
    g_signal_connect(window, "destroy", G_CALLBACK(app_quit), data);

    area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, PIX_WIDTH, PIX_HEIGHT);
    g_signal_connect(area, "draw", G_CALLBACK(area_redraw), data);

    /* Start a frames receiving. */
    btn_rc_start = gtk_button_new_with_label("Start receiving");
    g_signal_connect(btn_rc_start, "clicked", G_CALLBACK(receive_start),
        data);

    /* Stop a frames receiving. */
    btn_rc_stop = gtk_button_new_with_label("Stop receiving");
    g_signal_connect(btn_rc_stop, "clicked", G_CALLBACK(receive_stop),
        data);

    grid = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(window), grid);

    gtk_grid_attach(GTK_GRID(grid), area,         0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_rc_start, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), btn_rc_stop,  1, 1, 1, 1);

    data->addr = strdup(argv[1]);
    data->port = atoi(argv[2]);
    data->area = area;

    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}

static gboolean
gsrc_dispatch(GSource *gsrc, GSourceFunc cb, gpointer data)
{
    struct source *src = (struct source *) gsrc;

    if (src->gpfd != NULL) {
        if (src->gpfd->revents & G_IO_IN) {
            if (frame_receive(data) == -1) {
                receive_stop(NULL, data);

                return G_SOURCE_REMOVE;
            }
        }
    }

    g_main_context_iteration(NULL, TRUE);

    return G_SOURCE_CONTINUE;
}

static int
receive_prepare(struct data *d)
{
    GSocketAddress *gaddr;
    int status;
    GError *err = NULL;

    d->gsock = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM,
        G_SOCKET_PROTOCOL_TCP, &err);
    if (d->gsock == NULL) {
        printf("Failed to create the socket.\n");
        error_throw(err);

        return -1;
    }

    memset(&d->servaddr, 0, sizeof(d->servaddr));
    d->servaddr.sin_family = AF_INET;
    d->servaddr.sin_port = htons(d->port);

    status = inet_pton(AF_INET, (const char *) d->addr,
        &d->servaddr.sin_addr);
    if (status <= 0) {
        printf("Failed to convert the address.\n");

        return -1;
    }

    gaddr = g_socket_address_new_from_native(&d->servaddr,
        sizeof(d->servaddr));
    if (gaddr == NULL) {
        printf("Failed to convert a native to GSocketAddress.\n");

        return -1;
    }

    if (!g_socket_connect(d->gsock, gaddr, NULL, &err)) {
        printf("Failed to connect to the socket.\n");
        error_throw(err);

        return -1;
    }

    g_object_unref(gaddr);

    return 0;
}

static void
receive_start(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    if (d->is_recv) {
        printf("A frames receiving is already started.\n");

        return;
    }

    if (receive_prepare(d) == -1) {
        printf("Failed to prepare a frames receiving.\n");

        return;
    }

    /* Init the custom source. */
    d->src = (struct source *) g_source_new(&gsf, sizeof(struct source));
    d->src->gpfd = g_source_add_unix_fd((GSource *) d->src,
        g_socket_get_fd(d->gsock), G_IO_IN);
    d->src->bytes_r = 0;

    g_source_set_callback((GSource *) d->src, NULL, d, NULL);
    g_source_attach((GSource *) d->src, NULL);

    d->fr_draw_tid = g_timeout_add(1, frame_draw, d);

    d->is_recv = TRUE;
}

static void
receive_stop(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    if (!d->is_recv) {
        printf("A frames receiving is not started.\n");

        return;
    }

    /* Stop receive the frames. */
    if (d->src != NULL)
        g_source_destroy((GSource *) d->src);

    g_socket_close(d->gsock, NULL);
    g_clear_object(&d->gsock);

    /* Stop draw the frames. */
    g_source_remove(d->fr_draw_tid);

    d->is_recv = FALSE;

    /* Clear the context. */
    gtk_widget_queue_draw(d->area);
}

static int
frame_receive(struct data *d)
{
    gssize bytes;
    GError *err = NULL;

    bytes = g_socket_receive(d->gsock, (gchar *) d->buf + d->src->bytes_r,
        YUV_RAW_LEN - d->src->bytes_r, NULL, &err);
    if (bytes == -1) {
        printf("Failed to read a data from the socket.\n");
        error_throw(err);

        return RECEIVE_STOP;
    } else if (bytes == 0) {
        printf("The connection was closed.\n");

        return RECEIVE_STOP;
    }

    d->src->bytes_r += bytes;

    if (d->src->bytes_r < YUV_RAW_LEN)
        return RECEIVE_CONTINUE;

    d->src->bytes_r = 0;

    return RECEIVE_CONTINUE;
}

static gboolean
frame_draw(gpointer data)
{
    struct data *d = data;

    gtk_widget_queue_draw(d->area);

    return TRUE;
}

static gboolean
area_redraw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct data *d = data;
    GdkPixbuf *pixbuf;
    uint8_t *rgb_raw;
    const GdkRectangle rect = {
        .x = 0,
        .y = 0,
        .width = PIX_WIDTH,
        .height = PIX_HEIGHT
    };

    gdk_cairo_rectangle(cr, &rect);

    if (!d->is_recv || d->buf == NULL) {
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

    /* Draw the received frame. */
    rgb_raw = yuv_to_rgb(d->buf);

    pixbuf = gdk_pixbuf_new_from_data(rgb_raw, GDK_COLORSPACE_RGB, FALSE,
        8, PIX_WIDTH, PIX_HEIGHT, 3 * PIX_WIDTH, NULL, NULL);

    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);

    cairo_fill(cr);

    g_free(rgb_raw);

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

static struct data *
data_alloc(void)
{
    struct data *data = g_malloc0(sizeof(struct data));

    data->buf = g_malloc0_n(YUV_RAW_LEN, sizeof(uint8_t));
    data->is_recv = FALSE;

    return data;
}

static void
data_free(struct data *d)
{
    g_free(d->buf);
    g_free(d->addr);

    if (d->gsock != NULL)
        g_object_unref(d->gsock);

    g_free(d);
}

static void
app_quit(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    if (d->is_recv)
        receive_stop(NULL, d);

    data_free(d);

    gtk_main_quit();
}
