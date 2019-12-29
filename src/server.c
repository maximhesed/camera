/* This is server, which sends the frames, from the camera,
 * to the network.
 *
 * For the video transmit, I used TCP, because UDP, possibly,
 * is requires a separately thread, in GTK. Maybe, the client code
 * will be rewrited, in future... Nonetheless, think that there
 * will not be much difference in a transmit quality.
 *
 * This code doesn't have any copyrights and licenses.
 * Restrictions - is dogmas. Be open. */

#include "camera.h"

static void broadcast_start(GtkWidget *widget, gpointer data);
static void broadcast_stop(GtkWidget *widget, gpointer data);
static gint server_prepare(struct data *d);
static gboolean frame_send(GSocketService *sservice,
                           GSocketConnection *gconn,
                           GObject *object,
                           gpointer data);
static gboolean t_frame_draw(gpointer data);
static gboolean frame_process(GtkWidget *widget, cairo_t *cr, gpointer data);
static void error_send(GError *err);
static void data_free(GtkWidget *widget, gpointer data);
static void app_quit(GtkWidget *widget, gpointer data);

int main(void)
{
    GtkWidget *wnd;
    GtkWidget *area;
    GtkWidget *btn_bd_start;
    GtkWidget *btn_bd_stop;
    GtkWidget *grd;

    struct data *data = g_slice_new0(struct data);

    gtk_init(NULL, NULL);

    wnd = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(wnd), "Server");
    gtk_window_set_position(GTK_WINDOW(wnd), GTK_WIN_POS_CENTER);
    g_signal_connect(wnd, "destroy", G_CALLBACK(data_free), data);
    g_signal_connect(wnd, "destroy", G_CALLBACK(app_quit), NULL);

    area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, PIX_WIDTH, PIX_HEIGHT);
    g_signal_connect(area, "draw", G_CALLBACK(frame_process), data);

    /* Start a frames broadcasting. */
    btn_bd_start = gtk_button_new_with_label("Start broadcasting");
    g_signal_connect(
        btn_bd_start,
        "clicked",
        G_CALLBACK(broadcast_start),
        data);

    /* Stop a frames broadcasting. */
    btn_bd_stop = gtk_button_new_with_label("Stop broadcasting");
    g_signal_connect(
        btn_bd_stop,
        "clicked",
        G_CALLBACK(broadcast_stop),
        data);

    grd = gtk_grid_new();
    gtk_container_add(GTK_CONTAINER(wnd), grd);

    gtk_grid_attach(GTK_GRID(grd), area,         0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grd), btn_bd_start, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grd), btn_bd_stop,  1, 1, 1, 1);

    /* Fill the data. */
    data->area = area;

    gtk_widget_show_all(wnd);

    gtk_main();

    return 0;
}

static void broadcast_start(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    if (d->ss_sid > 0) {
        g_print("Server is already started.\n");

        return;
    }

    /* Open the device. */
    if (device_open(d) == -1)
        return;

    if (caps_print(d->fd) == -1)
        return;

    if (format_set(d->fd) == -1)
        return;

    if (buf_req(d->fd) == -1)
        return;

    if (buf_alloc(d) == -1)
        return;

    if (buf_map(d) == -1)
        return;

    /* Start a frame streaming. */
    if (frame_streamon(d) == -1)
        return;

    /* Start a frame capturing. */
    d->tid = g_timeout_add(20, t_frame_draw, data);

    if (server_prepare(data) == -1) {
        g_print("Failed to prepare a server.\n");

        return;
    }

    /* Here, we are starting the our server, connecting the signal
     * to the socket service, to catch incoming connections. */
    d->ss_sid = g_signal_connect(d->sservice, "incoming",
        G_CALLBACK(frame_send), data);

    g_socket_service_start(d->sservice);
}

static void broadcast_stop(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    if (d->ss_sid == 0) {
        g_print("Server isn't started.\n");

        return;
    }

    g_source_remove(d->tid);
    d->tid = 0;

    /* Stop a frame streaming. */
    if (frame_streamoff(d) == -1)
        return;

    if (buf_unmap(d) == -1)
        return;

    if (device_close(d->fd) == -1)
        return;

    /* Clear the context. */
    gtk_widget_queue_draw(d->area);

    g_socket_service_stop(d->sservice);
    g_socket_listener_close((GSocketListener *) d->sservice);
    g_signal_handler_disconnect(d->sservice, d->ss_sid);
    d->ss_sid = 0;

    memset(&d->servaddr, 0, sizeof(struct sockaddr_in));
    g_clear_object(&d->sservice);
}

static gint server_prepare(struct data *d)
{
    GSocketAddress *gaddr;
    struct sockaddr_in servaddr;
    gboolean status;
    GError *err = NULL;

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(22221);

    d->sservice = g_socket_service_new();

    /* I don't want start the socket service, now. */
    g_socket_service_stop(d->sservice);

    if (servaddr.sin_family == d->servaddr.sin_family &&
        servaddr.sin_addr.s_addr == d->servaddr.sin_addr.s_addr &&
        servaddr.sin_port == d->servaddr.sin_port)
        /* An address is already listened. So, we don't need to add
         * it to the socket listener again. This is not an error.
         * We just don't add the address to the listener, but
         * continue to send a data. */

        return 0;

    d->servaddr = servaddr;

    /* Convert the servaddr to the GSocketAddress. */
    gaddr = g_socket_address_new_from_native(&servaddr, sizeof(servaddr));
    if (gaddr == NULL) {
        g_print("Failed to convert a native to GSocketAddress.\n");

        return -1;
    }

    status = g_socket_listener_add_address(
        (GSocketListener *) d->sservice,
        gaddr,
        G_SOCKET_TYPE_STREAM,
        G_SOCKET_PROTOCOL_TCP,
        NULL,
        NULL,
        &err);
    if (!status) {
        g_print("Failed to add the address into the socket listener.\n");
        error_send(err);

        return -1;
    }

    g_object_unref(gaddr);

    return 0;
}

static gboolean frame_send(GSocketService *sservice,
                           GSocketConnection *gconn,
                           GObject *object,
                           gpointer data)
{
    struct data *d = data;
    GSocket *gsock;
    gssize bytes;
    GError *err = NULL;

    if (d->storage == NULL)
        return FALSE;

    gsock = g_socket_connection_get_socket(gconn);

    /* Write the data to the socket. */
    bytes = g_socket_send(
        gsock,
        (const gchar *) d->storage,
        YUV_RAW_LEN,
        NULL,
        &err);
    if (bytes == -1) {
        g_print("Failed to write a data into the accepted socket.\n");
        error_send(err);

        return FALSE;
    }

    if (!g_socket_close(gsock, &err)) {
        g_print("Failed to close the socket.\n");
        error_send(err);

        return FALSE;
    }

    /* At first sight, it might seem, that the socket's references must be
     * dropped to 0, here. But, no. Yes, g_object_unref(socket) need to
     * call, when we finished work with the socket. However, only if a
     * socket was created by g_socket_new(), g_socket_new_from_fd() or
     * g_socket_accept(). */

    return TRUE;
}

static gboolean t_frame_draw(gpointer data)
{
    struct data *d = data;

    gtk_widget_queue_draw(d->area);

    return TRUE;
}

/* TODO: Split by frame_draw() and frame_capture(). */
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

    if (frame_capture(data) == -1)
        return FALSE;

    /* Draw the captured frame. */
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

    return FALSE;
}

static void error_send(GError *err)
{
    g_print("Error: %s\n", err->message);

    g_error_free(err);
}

static void data_free(GtkWidget *widget, gpointer data)
{
    struct data *d = data;

    /* Unmap the frames storage. */
    if (d->storage != NULL) {
        if (buf_unmap(d) == -1)
            return;
    }

    /* Stop and destroy the socket service. */
    if (d->ss_sid > 0) {
        g_socket_service_stop(d->sservice);
        g_socket_listener_close((GSocketListener *) d->sservice);
        g_signal_handler_disconnect(d->sservice, d->ss_sid);

        g_object_unref(d->sservice);
    }

    g_slice_free(struct data, d);
}

static void app_quit(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}
