/* This is client, which receives the frames, from the camera.
 *
 * This code doesn't have any copyrights and licenses.
 * Restrictions - is dogmas. Be open. */

#include <arpa/inet.h>

#include "camera.h"

static void receive_start(GtkWidget *widget, gpointer data);
static void receive_stop(GtkWidget *widget, gpointer data);
static gint frame_receive(gpointer data);
static gboolean t_area_redraw(gpointer data);
static gboolean frame_process(GtkWidget *widget, cairo_t *cr, gpointer data);
static void frame_process_terminate(gpointer data);
static void error_send(GError *err);
static void data_free(GtkWidget *widget, gpointer data);
static void app_quit(GtkWidget *widget, gpointer data);

int main(int argc, char *argv[])
{
    GtkWidget *wnd;
    GtkWidget *area;
    GtkWidget *btn_rc_start;
    GtkWidget *btn_rc_stop;
    GtkWidget *grd;

    struct data *data = g_slice_new0(struct data);

    GOptionEntry entries[] = {
        {
            "address",
            'a',
            0,
            G_OPTION_ARG_STRING,
            &data->addr,
            "Address",
            "ADDRESS"
        },

        {NULL}
    };
    GOptionContext *context;
    GError *err = NULL;

    /* Get the address from the arguments. */
    context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));
    if (!g_option_context_parse(context, &argc, &argv, &err)) {
        error_send(err);

        return -1;
    }

    if (data->addr == NULL)
        data->addr = g_strdup("127.0.0.1");

    gtk_init(NULL, NULL);

    wnd = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(wnd), "Client");
    gtk_window_set_position(GTK_WINDOW(wnd), GTK_WIN_POS_CENTER);
    g_signal_connect(wnd, "destroy", G_CALLBACK(data_free), data);
    g_signal_connect(wnd, "destroy", G_CALLBACK(app_quit), NULL);

    area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, PIX_WIDTH, PIX_HEIGHT);
    g_signal_connect(area, "draw", G_CALLBACK(frame_process), data);

    /* Start a frames receiving. */
    btn_rc_start = gtk_button_new_with_label("Start receiving");
    g_signal_connect(
        btn_rc_start,
        "clicked",
        G_CALLBACK(receive_start),
        data);

    /* Stop a frames receiving. */
    btn_rc_stop = gtk_button_new_with_label("Stop receiving");
    g_signal_connect(
        btn_rc_stop,
        "clicked",
        G_CALLBACK(receive_stop),
        data);

    grd = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(wnd), grd);

    gtk_grid_attach(GTK_GRID(grd), area,         0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grd), btn_rc_start, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grd), btn_rc_stop,  1, 1, 1, 1);

    data->area = area;

    gtk_widget_show_all(wnd);

    gtk_main();

    return 0;
}

static void receive_start(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    if (d->tid > 0) {
        g_print("A frames receiving is already started.\n");

        return;
    }

    /* Try to receive the frame, because the
     * server may not be started. */
    if (frame_receive(data) == -1)
        return;

    d->tid = g_timeout_add(20, t_area_redraw, data);
}

static void receive_stop(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    if (d->tid == 0) {
        g_print("A frames receiving is not started.\n");

        return;
    }

    frame_process_terminate(data);
}

static gint frame_receive(gpointer data)
{
    struct data *d = data;
    GSocket *gsock;
    GSocketAddress *gaddr;
    gint status;
    gchar *recv_data = g_malloc0_n(YUV_RAW_LEN, sizeof(gchar));
    gssize bytes;
    gssize bytes_r = 0; /* The bytes received. */
    GError *err = NULL;

    gsock = g_socket_new(
        G_SOCKET_FAMILY_IPV4,
        G_SOCKET_TYPE_STREAM,
        G_SOCKET_PROTOCOL_TCP,
        &err);
    if (gsock == NULL) {
        g_print("Failed to create a socket.\n");
        error_send(err);

        return -1;
    }

    memset(&d->servaddr, 0, sizeof(d->servaddr));
    d->servaddr.sin_family = AF_INET;
    d->servaddr.sin_port = htons(22221);

    status = inet_pton(
        AF_INET,
        (const gchar *) d->addr,
        &d->servaddr.sin_addr);
    if (status <= 0) {
        g_print("Failed to convert the address.\n");

        return -1;
    }

    gaddr = g_socket_address_new_from_native(
        &d->servaddr,
        sizeof(d->servaddr));
    if (gaddr == NULL) {
        g_print("Failed to convert a native to GSocketAddress.\n");

        return -1;
    }

    if (!g_socket_connect(gsock, gaddr, NULL, &err)) {
        g_print("Failed to connect to the socket.\n");
        g_print("Possibly, the server isn't started.\n");
        error_send(err);

        return -1;
    }

    while (bytes_r < YUV_RAW_LEN) {
        bytes = g_socket_receive(
            gsock,
            recv_data + bytes_r,
            YUV_RAW_LEN - bytes_r,
            NULL,
            &err);
        if (bytes == -1 || bytes == 0) {
            g_print("Failed to get data from the socket.\n");
            error_send(err);

            return -1;
        }

        bytes_r += bytes;
    }

    d->storage = (guint8 *) recv_data;

    if (!g_socket_close(gsock, &err)) {
        g_print("Failed to close the socket.\n");
        error_send(err);

        return -1;
    }

    g_object_unref(gsock);

    return 0;
}

static gboolean t_area_redraw(gpointer data)
{
    struct data *d = data;

    gtk_widget_queue_draw(d->area);

    return TRUE;
}

/* TODO: Split by frame_draw() and frame_receive(). */
static gboolean frame_process(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GdkPixbuf *pixbuf;
    guint8 *rgb_raw;
    const GdkRectangle rect = {
        .x = 0,
        .y = 0,
        .width = PIX_WIDTH,
        .height = PIX_HEIGHT
    };
    struct data *d = data;

    gdk_cairo_rectangle(cr, &rect);

    if (d->tid == 0) {
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

    if (frame_receive(data)) {
        g_print("Failed to receive a frame.\n");
        g_print("Possibly, the server has been stopped.\n");

        frame_process_terminate(data);

        return FALSE;
    }

    /* Draw the received frame. */
    rgb_raw = yuv_to_rgb(d->storage);

    pixbuf = gdk_pixbuf_new_from_data(
        rgb_raw,
        GDK_COLORSPACE_RGB,
        FALSE,
        8,
        PIX_WIDTH,
        PIX_HEIGHT,
        3 * PIX_WIDTH,
        NULL,
        NULL
    );

    gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);

    cairo_fill(cr);

    g_free(rgb_raw);

    return FALSE;
}

static void frame_process_terminate(gpointer data)
{
    struct data *d = data;

    g_source_remove(d->tid);
    d->tid = 0;

    gtk_widget_queue_draw(d->area);
}

static void error_send(GError *err)
{
    g_print("Error: %s\n", err->message);

    g_error_free(err);
}

static void data_free(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    g_free(d->storage);
    g_free(d->addr);

    g_slice_free(struct data, d);
}

static void app_quit(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}
