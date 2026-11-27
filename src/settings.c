#include "config.h"
#include "wine-tuning.h"
#include "image-clipboard.h"
#include <gtk/gtk.h>

typedef struct {
    GtkWindow *window;
    GtkEntry *wine;
    GtkEntry *prefix;
    GtkEntry *client;
    GtkDropDown *scale;
    guint dpi_values[7];
    GtkCheckButton *downloads;
    GtkCheckButton *autostart;
    GtkCheckButton *attention;
    GtkCheckButton *korean_profile;
    GtkLabel *status;
    GCancellable *image_request;
} Settings;

static void status(Settings *settings, const char *text)
{
    gtk_label_set_text(settings->status, text);
}

static void save(GtkButton *button, gpointer data)
{
    (void)button;
    Settings *settings = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) config = bridge_config_load(&error);
    if (!config) {
        status(settings, error->message);
        return;
    }
    const char *wine = gtk_editable_get_text(GTK_EDITABLE(settings->wine));
    const char *prefix = gtk_editable_get_text(GTK_EDITABLE(settings->prefix));
    const char *client = gtk_editable_get_text(GTK_EDITABLE(settings->client));
    guint scale = gtk_drop_down_get_selected(settings->scale);
    guint dpi = scale < G_N_ELEMENTS(settings->dpi_values) ? settings->dpi_values[scale] : 0;
    if ((*prefix && !g_path_is_absolute(prefix)) || (*client && !g_path_is_absolute(client))) {
        status(settings, "Enter absolute paths for the Wine prefix and KakaoTalk.");
        return;
    }
    g_key_file_set_string(config, "Bridge", "Wine", wine);
    g_key_file_set_string(config, "Bridge", "Prefix", prefix);
    g_key_file_set_string(config, "Bridge", "Client", client);
    g_key_file_set_integer(config, "Bridge", "DPI", dpi);
    g_key_file_set_boolean(config, "Bridge", "MapDownloads",
                           gtk_check_button_get_active(settings->downloads));
    g_key_file_set_boolean(config, "Bridge", "WatchAttention",
                           gtk_check_button_get_active(settings->attention));
    g_key_file_set_boolean(config, "Bridge", BRIDGE_KOREAN_PROFILE,
                           gtk_check_button_get_active(settings->korean_profile));
    if (!bridge_config_save(config, &error)) {
        status(settings, error->message);
        return;
    }
    if (!bridge_autostart_set(gtk_check_button_get_active(settings->autostart), &error)) {
        g_autofree char *message = g_strdup_printf("Settings saved. Login startup: %s", error->message);
        status(settings, message);
        return;
    }
    status(settings, "Saved. Changes apply at the next KakaoTalk start.");
}

static char *bridge_executable(GError **error)
{
    g_autofree char *self = g_file_read_link("/proc/self/exe", error);
    if (!self)
        return NULL;
    g_autofree char *dir = g_path_get_dirname(self);
    return g_build_filename(dir, BRIDGE_EXE, NULL);
}

static void run_command(Settings *settings, const char *command, const char *file)
{
    g_autoptr(GError) error = NULL;
    g_autofree char *bridge = bridge_executable(&error);
    const char *args[] = { bridge, command, file, NULL };
    if (!bridge || !g_spawn_async(NULL, (char **)args, NULL, 0, NULL, NULL, NULL, &error))
        status(settings, error->message);
    else
        status(settings, "Request sent.");
}

static void integration_done(GObject *object, GAsyncResult *result, gpointer data)
{
    g_autoptr(GtkWindow) window = data;
    g_autoptr(GError) error = NULL;
    g_autofree char *output = NULL;
    if (g_subprocess_communicate_utf8_finish(G_SUBPROCESS(object), result, &output, NULL, &error) &&
        output && *output) {
        g_strchomp(output);
    }
    Settings *settings = g_object_get_data(G_OBJECT(window), "settings");
    if (gtk_widget_get_visible(GTK_WIDGET(window)))
        status(settings, error ? error->message : (output && *output ? output : "No Wine integration output."));
}

static void integration_confirmed(GObject *object, GAsyncResult *result, gpointer data)
{
    g_autoptr(GtkWindow) window = data;
    if (gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(object), result, NULL) != 1)
        return;

    Settings *settings = g_object_get_data(G_OBJECT(window), "settings");
    g_autoptr(GError) error = NULL;
    g_autofree char *bridge = bridge_executable(&error);
    const char *command = g_object_get_data(object, "command");
    const char *args[] = { bridge, command, NULL };
    g_autoptr(GSubprocess) process = bridge ? g_subprocess_newv(
        args, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE, &error) : NULL;
    if (!process) {
        status(settings, error->message);
        return;
    }
    status(settings, "Applying Wine settings…");
    g_subprocess_communicate_utf8_async(process, NULL, NULL, integration_done, g_object_ref(window));
}

static void integrate_clicked(GtkButton *button, gpointer data)
{
    Settings *settings = data;
    const char *command = g_object_get_data(G_OBJECT(button), "command");
    gboolean fonts = g_str_equal(command, "integrate-fonts");
    g_autoptr(GtkAlertDialog) dialog = gtk_alert_dialog_new(
        "%s", fonts ? "Apply Korean font fallbacks?" : "Open KakaoTalk as a separate window?");
    gtk_alert_dialog_set_detail(dialog,
        "Quit KakaoTalk and other Wine apps in the selected prefix first. This updates the prefix and saves a registry backup.");
    const char *buttons[] = { "Cancel", "Apply", NULL };
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 0);
    g_object_set_data(G_OBJECT(dialog), "command", (gpointer)command);
    gtk_alert_dialog_choose(dialog, settings->window, NULL, integration_confirmed,
                            g_object_ref(settings->window));
}

static void command_clicked(GtkButton *button, gpointer data)
{
    run_command(data, g_object_get_data(G_OBJECT(button), "command"), NULL);
}

static void installer_chosen(GObject *dialog, GAsyncResult *result, gpointer data)
{
    g_autoptr(GtkWindow) window = data;
    g_autoptr(GError) error = NULL;
    g_autoptr(GFile) file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(dialog), result, &error);
    if (!gtk_widget_get_visible(GTK_WIDGET(window)))
        return;
    Settings *settings = g_object_get_data(G_OBJECT(window), "settings");
    if (!file) {
        if (!g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
            !g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_CANCELLED))
            status(settings, error->message);
        return;
    }
    g_autofree char *path = g_file_get_path(file);
    if (path)
        run_command(settings, "install-client", path);
    else
        status(settings, "Select a local Windows installer.");
}

static void choose_installer(GtkButton *button, gpointer data)
{
    (void)button;
    Settings *settings = data;
    g_autoptr(GtkFileDialog) dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Select the official KakaoTalk installer");
    gtk_file_dialog_set_accept_label(dialog, "Run Installer");
    g_autoptr(GtkFileFilter) filter = gtk_file_filter_new();
    gtk_file_filter_add_suffix(filter, "exe");
    g_autoptr(GListStore) filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));
    gtk_file_dialog_open(dialog, settings->window, NULL, installer_chosen, g_object_ref(settings->window));
}

static GdkClipboard *clipboard(Settings *settings)
{
    return gtk_widget_get_clipboard(GTK_WIDGET(settings->window));
}

static void copy_text(GtkButton *button, gpointer data)
{
    (void)button;
    gdk_clipboard_set_text(clipboard(data), "Kakao Linux Bridge — 한글 입력 확인");
    status(data, "Test text copied. Paste it in KakaoTalk.");
}

static void copy_image(GtkButton *button, gpointer data)
{
    (void)button;
    static const guint8 pixels[] = {
        49, 91, 175, 255, 255, 200, 46, 255,
        255, 200, 46, 255, 49, 91, 175, 255
    };
    g_autoptr(GBytes) bytes = g_bytes_new_static(pixels, sizeof pixels);
    g_autoptr(GdkTexture) image = gdk_memory_texture_new(2, 2, GDK_MEMORY_R8G8B8A8, bytes, 8);
    bridge_clipboard_offer_image(clipboard(data), image);
    status(data, "Test image copied. Paste it in KakaoTalk.");
}

typedef struct {
    GtkWindow *window;
    GCancellable *cancellable;
    gulong changed;
} ImageRequest;

static void image_clipboard_changed(GdkClipboard *clipboard, gpointer data)
{
    (void)clipboard;
    ImageRequest *request = data;
    g_cancellable_cancel(request->cancellable);
}

static void image_read(GObject *object, GAsyncResult *result, gpointer data)
{
    GdkClipboard *clipboard = GDK_CLIPBOARD(object);
    ImageRequest *request = data;
    GtkWindow *window = request->window;
    Settings *settings = g_object_get_data(G_OBJECT(window), "settings");
    g_signal_handler_disconnect(clipboard, request->changed);
    g_autoptr(GError) error = NULL;
    g_autoptr(GdkTexture) image = gdk_clipboard_read_texture_finish(clipboard, result, &error);
    if (settings->image_request == request->cancellable) {
        g_clear_object(&settings->image_request);
        if (gtk_widget_get_visible(GTK_WIDGET(window))) {
            if (g_cancellable_is_cancelled(request->cancellable))
                status(settings, "Clipboard contents changed. Try again with the new image.");
            else if (!image)
                status(settings, "Copy an image in Ubuntu or KakaoTalk first.");
            else {
                bridge_clipboard_offer_image(clipboard, image);
                status(settings, "Image ready in PNG and BMP. Keep this window open while pasting.");
            }
        }
    }
    g_object_unref(request->cancellable);
    g_object_unref(window);
    g_free(request);
}

static void prepare_clipboard(GtkButton *button, gpointer data)
{
    (void)button;
    Settings *settings = data;
    if (settings->image_request)
        return;
    GdkClipboard *source = clipboard(settings);
    settings->image_request = g_cancellable_new();
    ImageRequest *request = g_new(ImageRequest, 1);
    request->window = g_object_ref(settings->window);
    request->cancellable = g_object_ref(settings->image_request);
    request->changed = g_signal_connect(source, "changed", G_CALLBACK(image_clipboard_changed), request);
    gdk_clipboard_read_texture_async(source, request->cancellable, image_read, request);
}

static gboolean cancel_image_request(GtkWindow *window, gpointer data)
{
    (void)window;
    Settings *settings = data;
    if (settings->image_request)
        g_cancellable_cancel(settings->image_request);
    return FALSE;
}

static void inspect_clipboard(GtkButton *button, gpointer data)
{
    (void)button;
    GdkContentFormats *formats = gdk_clipboard_get_formats(clipboard(data));
    g_autofree char *text = gdk_content_formats_to_string(formats);
    status(data, text && *text ? text : "No clipboard formats are currently advertised.");
}

static GtkEntry *path_entry(GtkGrid *grid, int row, const char *name, const char *value)
{
    GtkWidget *label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Detect automatically");
    gtk_editable_set_text(GTK_EDITABLE(entry), value ? value : "");
    gtk_grid_attach(grid, label, 0, row, 1, 1);
    gtk_grid_attach(grid, entry, 1, row, 1, 1);
    return GTK_ENTRY(entry);
}

static GtkWidget *add_button(GtkWidget *box, const char *label, GCallback callback, Settings *settings)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_box_append(GTK_BOX(box), button);
    g_signal_connect(button, "clicked", callback, settings);
    return button;
}

static void add_command(GtkWidget *box, const char *label, const char *command, Settings *settings)
{
    GtkWidget *button = add_button(box, label, G_CALLBACK(command_clicked), settings);
    g_object_set_data(G_OBJECT(button), "command", (gpointer)command);
}

static GtkWidget *section(GtkWidget *parent, const char *title)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_widget_add_css_class(card, "bridge-card");
    gtk_box_append(GTK_BOX(parent), card);
    GtkWidget *label = gtk_label_new(title);
    gtk_widget_add_css_class(label, "heading");
    gtk_label_set_xalign(GTK_LABEL(label), 0);
    gtk_box_append(GTK_BOX(card), label);
    return card;
}

static void check_connection(GtkButton *button, gpointer data)
{
    (void)button;
    Settings *settings = data;
    g_autoptr(GError) error = NULL;
    g_autofree char *bridge = bridge_executable(&error);
    const char *args[] = { bridge, "doctor-network", NULL };
    g_autoptr(GSubprocess) process = bridge ? g_subprocess_newv(
        args, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE, &error) : NULL;
    if (!process) {
        status(settings, error->message);
        return;
    }
    status(settings, "Checking Ubuntu DNS…");
    g_subprocess_communicate_utf8_async(process, NULL, NULL, integration_done,
                                       g_object_ref(settings->window));
}

static void activate(GtkApplication *app, gpointer unused)
{
    (void)unused;
    GtkWindow *existing = gtk_application_get_active_window(app);
    if (existing) {
        gtk_window_present(existing);
        return;
    }
    Settings *settings = g_new0(Settings, 1);
    settings->window = GTK_WINDOW(gtk_application_window_new(app));
    g_object_set_data_full(G_OBJECT(settings->window), "settings", settings, g_free);
    g_signal_connect(settings->window, "close-request", G_CALLBACK(cancel_image_request), settings);
    gtk_window_set_title(settings->window, "Kakao Linux Bridge");
    gtk_window_set_default_size(settings->window, 690, 720);
    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(settings->window, layout);
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(layout), scroll);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(box, 20);
    gtk_widget_set_margin_bottom(box, 20);
    gtk_widget_set_margin_start(box, 20);
    gtk_widget_set_margin_end(box, 20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), box);
    GtkWidget *connection = section(box, "Connection");
    GtkWidget *network = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(connection), network);
    add_button(network, "Check DNS", G_CALLBACK(check_connection), settings);
    add_command(network, "Start without tuning", "start-baseline", settings);
    GtkWidget *description = gtk_label_new("For error 11002, quit KakaoTalk before trying an untuned start.");
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_xalign(GTK_LABEL(description), 0);
    gtk_widget_add_css_class(description, "dim-label");
    gtk_box_append(GTK_BOX(connection), description);
    GtkWidget *wine_card = section(box, "Wine setup");
    GtkWidget *paths = gtk_expander_new("Wine paths");
    gtk_box_append(GTK_BOX(wine_card), paths);
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 8);
    gtk_grid_set_column_spacing(grid, 16);
    gtk_expander_set_child(GTK_EXPANDER(paths), GTK_WIDGET(grid));

    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) config = bridge_config_load(&error);
    if (!config)
        config = g_key_file_new();
    g_autofree char *wine = g_key_file_get_string(config, "Bridge", "Wine", NULL);
    g_autofree char *prefix = g_key_file_get_string(config, "Bridge", "Prefix", NULL);
    g_autofree char *client = g_key_file_get_string(config, "Bridge", "Client", NULL);
    Settings *s = settings;
    s->wine = path_entry(grid, 0, "Wine executable", wine);
    s->prefix = path_entry(grid, 1, "Wine prefix", prefix);
    s->client = path_entry(grid, 2, "KakaoTalk executable", client);
    GtkWidget *scale_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_append(GTK_BOX(wine_card), scale_row);
    GtkWidget *scale_label = gtk_label_new("KakaoTalk display scale");
    gtk_label_set_xalign(GTK_LABEL(scale_label), 0);
    gtk_widget_set_hexpand(scale_label, TRUE);
    gtk_box_append(GTK_BOX(scale_row), scale_label);
    const char *scales[] = { "Keep Wine setting", "100%", "125%", "150%", "175%", "200%", NULL };
    const guint dpi_values[] = { 0, 96, 120, 144, 168, 192 };
    g_autoptr(GtkStringList) scale_options = gtk_string_list_new(scales);
    gint current_dpi = g_key_file_get_integer(config, "Bridge", "DPI", NULL);
    guint selected = 0;
    for (guint i = 0; i < G_N_ELEMENTS(dpi_values); i++) {
        s->dpi_values[i] = dpi_values[i];
        if (current_dpi == (gint)dpi_values[i])
            selected = i;
    }
    if (!selected && current_dpi >= 96 && current_dpi <= 384) {
        selected = G_N_ELEMENTS(dpi_values);
        s->dpi_values[selected] = (guint)current_dpi;
        g_autofree char *custom = g_strdup_printf("Custom (%d DPI)", current_dpi);
        gtk_string_list_append(scale_options, custom);
    }
    s->scale = GTK_DROP_DOWN(gtk_drop_down_new(G_LIST_MODEL(g_object_ref(scale_options)), NULL));
    gtk_drop_down_set_selected(s->scale, selected);
    gtk_widget_set_tooltip_text(GTK_WIDGET(s->scale), "Save, close Wine apps in this prefix, then start KakaoTalk. Other apps in the same prefix also use this scale.");
    gtk_box_append(GTK_BOX(scale_row), GTK_WIDGET(s->scale));
    s->korean_profile = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Ubuntu Korean input (IBus + X11/XWayland)"));
    gtk_check_button_set_active(s->korean_profile, g_key_file_get_boolean(config, "Bridge", BRIDGE_KOREAN_PROFILE, NULL));
    gtk_box_append(GTK_BOX(wine_card), GTK_WIDGET(s->korean_profile));

    GtkWidget *preferences = section(box, "Desktop integration");
    s->downloads = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Use Ubuntu Downloads (Bridge-managed prefix)"));
    gtk_check_button_set_active(s->downloads, g_key_file_get_boolean(config, "Bridge", "MapDownloads", NULL));
    gtk_box_append(GTK_BOX(preferences), GTK_WIDGET(s->downloads));
    g_autofree char *autostart = bridge_autostart_path();
    s->autostart = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Start KakaoTalk at login"));
    gtk_check_button_set_active(s->autostart, g_file_test(autostart, G_FILE_TEST_EXISTS));
    gtk_box_append(GTK_BOX(preferences), GTK_WIDGET(s->autostart));
    s->attention = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Desktop alerts for taskbar flashes (experimental)"));
    gtk_check_button_set_active(s->attention, g_key_file_get_boolean(config, "Bridge", "WatchAttention", NULL));
    gtk_box_append(GTK_BOX(preferences), GTK_WIDGET(s->attention));

    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(wine_card), tools);
    add_button(tools, "Install KakaoTalk…", G_CALLBACK(choose_installer), s);
    add_command(tools, "Open Downloads", "downloads", s);

    GtkWidget *integration = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(wine_card), integration);
    GtkWidget *separate = add_button(integration, "Use Separate KakaoTalk Window", G_CALLBACK(integrate_clicked), s);
    g_object_set_data(G_OBJECT(separate), "command", "integrate-window");
    GtkWidget *fonts = add_button(integration, "Fix Korean Text Rendering", G_CALLBACK(integrate_clicked), s);
    g_object_set_data(G_OBJECT(fonts), "command", "integrate-fonts");

    GtkWidget *tests = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *clipboard_card = section(box, "Clipboard and notification tests");
    gtk_box_append(GTK_BOX(clipboard_card), tests);
    add_button(tests, "Copy Test Text", G_CALLBACK(copy_text), s);
    add_button(tests, "Copy Test Image", G_CALLBACK(copy_image), s);
    add_button(tests, "Prepare Image Paste", G_CALLBACK(prepare_clipboard), s);
    GtkWidget *more_tests = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(clipboard_card), more_tests);
    add_button(more_tests, "Clipboard Formats", G_CALLBACK(inspect_clipboard), s);
    add_command(more_tests, "Test Notification", "test-notification", s);

    s->status = GTK_LABEL(gtk_label_new(NULL));
    gtk_label_set_wrap(s->status, TRUE);
    gtk_label_set_selectable(s->status, TRUE);
    gtk_label_set_xalign(s->status, 0);
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_add_css_class(footer, "bridge-footer");
    gtk_box_append(GTK_BOX(layout), footer);
    gtk_widget_add_css_class(GTK_WIDGET(s->status), "dim-label");
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(s->status));
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(footer), controls);
    GtkWidget *quit = add_button(controls, "Quit KakaoTalk", G_CALLBACK(command_clicked), s);
    g_object_set_data(G_OBJECT(quit), "command", "stop");
    gtk_widget_add_css_class(quit, "bridge-quit");
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(controls), spacer);
    GtkWidget *save_button = add_button(controls, "Save", G_CALLBACK(save), s);
    gtk_widget_add_css_class(save_button, "bridge-save");
    GtkWidget *start = add_button(controls, "Start", G_CALLBACK(command_clicked), s);
    g_object_set_data(G_OBJECT(start), "command", "start");
    gtk_widget_add_css_class(start, "bridge-start");
    g_autofree char *detected_wine = bridge_find_wine(config);
    g_autofree char *detected_prefix = bridge_find_prefix(config);
    g_autofree char *detected_client = detected_prefix ? bridge_find_client(config, detected_prefix) : NULL;
    g_autofree char *detected = g_strdup_printf("Wine: %s\nPrefix: %s\nKakaoTalk: %s",
        detected_wine ? detected_wine : "missing", detected_prefix ? detected_prefix : "invalid",
        detected_client ? detected_client : "missing");
    GtkWidget *details = gtk_label_new(detected);
    gtk_label_set_xalign(GTK_LABEL(details), 0);
    gtk_label_set_wrap(GTK_LABEL(details), TRUE);
    gtk_label_set_selectable(GTK_LABEL(details), TRUE);
    gtk_widget_add_css_class(details, "dim-label");
    gtk_box_append(GTK_BOX(wine_card), details);
    status(s, error ? error->message : (detected_client && detected_wine ? "Wine and KakaoTalk detected." : "Check Wine setup."));
    g_autoptr(GtkCssProvider) theme = gtk_css_provider_new();
    gtk_css_provider_load_from_string(theme,
        ".bridge-card { border: 1px solid alpha(currentColor,0.18); border-radius: 12px; padding: 14px; }"
        ".bridge-footer { padding: 12px 20px 16px; border-top: 1px solid alpha(currentColor,0.20); }"
        "button.bridge-start, button.bridge-save, button.bridge-quit { color: white; background-image: none; border-color: transparent; box-shadow: none; text-shadow: none; padding: 6px 17px; }"
        "button.bridge-start { background-color: #176b49; } button.bridge-start:hover { background-color: #1b7753; }"
        "button.bridge-save { background-color: #285da8; } button.bridge-save:hover { background-color: #306abe; }"
        "button.bridge-quit { background-color: #9b3545; } button.bridge-quit:hover { background-color: #ac4051; }");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(GTK_WIDGET(s->window)),
                                               GTK_STYLE_PROVIDER(theme), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_window_present(s->window);
}

static void quit_settings(GSimpleAction *action, GVariant *parameter, gpointer data)
{
    (void)action; (void)parameter;
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(data));
    if (window)
        gtk_window_close(window);
}

int main(int argc, char **argv)
{
    g_autoptr(GtkApplication) app = gtk_application_new(BRIDGE_ID ".Settings", G_APPLICATION_DEFAULT_FLAGS);
    const GActionEntry actions[] = { { .name = "quit", .activate = quit_settings } };
    const char *accelerators[] = { "<Control>q", NULL };
    g_action_map_add_action_entries(G_ACTION_MAP(app), actions, G_N_ELEMENTS(actions), app);
    gtk_application_set_accels_for_action(app, "app.quit", accelerators);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
