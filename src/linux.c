#include "core.h"
#include <gtk/gtk.h>

static Options options;
static Action visible[ACTION_COUNT];
static int count, selected;
static gboolean busy;
static GtkWidget *window, *buttons[ACTION_COUNT], *footer;
static GDBusConnection *system_bus, *session_bus;
typedef struct {
  const char *service, *path, *interface, *method;
  int argument;
} Endpoint;
static Endpoint lock_endpoint, logout_endpoint;
static const char *login_service = "org.freedesktop.login1";
static const char *login_path = "/org/freedesktop/login1";
static const char *login_interface = "org.freedesktop.login1.Manager";

static gboolean has_method(Endpoint endpoint) {
  if (!session_bus)
    return FALSE;
  GVariant *reply = g_dbus_connection_call_sync(
      session_bus, endpoint.service, endpoint.path,
      "org.freedesktop.DBus.Introspectable", "Introspect", NULL,
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 1000, NULL, NULL);
  if (!reply)
    return FALSE;
  const char *xml;
  g_variant_get(reply, "(&s)", &xml);
  GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(xml, NULL);
  GDBusInterfaceInfo *interface =
      node ? g_dbus_node_info_lookup_interface(node, endpoint.interface) : NULL;
  gboolean found = interface && g_dbus_interface_info_lookup_method(
                                    interface, endpoint.method);
  if (node)
    g_dbus_node_info_unref(node);
  g_variant_unref(reply);
  return found;
}
static gboolean capability(const char *method) {
  if (!system_bus)
    return FALSE;
  GVariant *reply = g_dbus_connection_call_sync(
      system_bus, login_service, login_path, login_interface, method, NULL,
      G_VARIANT_TYPE("(s)"), G_DBUS_CALL_FLAGS_NONE, 1500, NULL, NULL);
  if (!reply)
    return FALSE;
  const char *value;
  g_variant_get(reply, "(&s)", &value);
  gboolean available =
      !g_strcmp0(value, "yes") || !g_strcmp0(value, "challenge");
  g_variant_unref(reply);
  return available;
}
static void discover(void) {
  system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
  session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
  Endpoint locks[] = {{"org.freedesktop.ScreenSaver", "/ScreenSaver",
                       "org.freedesktop.ScreenSaver", "Lock", -1},
                      {"org.freedesktop.ScreenSaver",
                       "/org/freedesktop/ScreenSaver",
                       "org.freedesktop.ScreenSaver", "Lock", -1},
                      {"org.gnome.ScreenSaver", "/org/gnome/ScreenSaver",
                       "org.gnome.ScreenSaver", "Lock", -1}};
  Endpoint logouts[] = {
      {"org.gnome.SessionManager", "/org/gnome/SessionManager",
       "org.gnome.SessionManager", "Logout", 0},
      {"org.kde.Shutdown", "/Shutdown", "org.kde.Shutdown", "logout", -1},
      {"org.xfce.SessionManager", "/org/xfce/SessionManager",
       "org.xfce.Session.Manager", "Logout", 1}};
  for (size_t i = 0; i < G_N_ELEMENTS(locks); i++)
    if (has_method(locks[i])) {
      lock_endpoint = locks[i];
      break;
    }
  for (size_t i = 0; i < G_N_ELEMENTS(logouts); i++)
    if (has_method(logouts[i])) {
      logout_endpoint = logouts[i];
      break;
    }
  if (capability("CanSuspend"))
    visible[count++] = SUSPEND;
  if (lock_endpoint.service)
    visible[count++] = LOCK;
  if (logout_endpoint.service)
    visible[count++] = LOGOUT;
  if (capability("CanPowerOff"))
    visible[count++] = SHUTDOWN;
  if (capability("CanReboot"))
    visible[count++] = RESTART;
  if (capability("CanHibernate"))
    visible[count++] = HIBERNATE;
}
static void run_action(GTask *task, gpointer source, gpointer task_data,
                       GCancellable *cancel) {
  (void)source;
  (void)cancel;
  Action action = GPOINTER_TO_INT(task_data);
  char message[512] = {0};
  if (mock_action(options, action, message, sizeof(message))) {
    if (message[0])
      g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s",
                              message);
    else
      g_task_return_boolean(task, TRUE);
    return;
  }
  GError *error = NULL;
  GVariant *reply;
  if (action == LOCK || action == LOGOUT) {
    Endpoint endpoint = action == LOCK ? lock_endpoint : logout_endpoint;
    GVariant *args = endpoint.argument == 0 ? g_variant_new("(u)", 0u)
                     : endpoint.argument == 1
                         ? g_variant_new("(bb)", TRUE, TRUE)
                         : NULL;
    reply = g_dbus_connection_call_sync(
        session_bus, endpoint.service, endpoint.path, endpoint.interface,
        endpoint.method, args, NULL, G_DBUS_CALL_FLAGS_NONE, 120000, NULL,
        &error);
  } else {
    const char *method = action == SUSPEND     ? "Suspend"
                         : action == HIBERNATE ? "Hibernate"
                         : action == RESTART   ? "Reboot"
                                               : "PowerOff";
    reply = g_dbus_connection_call_sync(
        system_bus, login_service, login_path, login_interface, method,
        g_variant_new("(b)", TRUE), NULL,
        G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION, 120000, NULL,
        &error);
  }
  if (!reply)
    g_task_return_error(task, error);
  else {
    g_variant_unref(reply);
    g_task_return_boolean(task, TRUE);
  }
}
static void restore_footer(void) {
  gtk_label_set_text(
      GTK_LABEL(footer),
      options.simulate_error ? "TEST MODE · selections show a simulated error"
      : options.dry_run
          ? "DRY RUN · selections close without taking action"
          : "Arrows / Tab navigate · Enter select · Esc / q quit");
}
static void select_button(int index) {
  selected = index;
  for (int i = 0; i < count; i++) {
    GtkStyleContext *style = gtk_widget_get_style_context(buttons[i]);
    if (i == selected)
      gtk_style_context_add_class(style, "selected");
    else
      gtk_style_context_remove_class(style, "selected");
  }
  gtk_widget_grab_focus(buttons[selected]);
}
static void action_finished(GObject *source, GAsyncResult *result,
                            gpointer data) {
  (void)source;
  GError *error = NULL;
  if (g_task_propagate_boolean(G_TASK(result), &error)) {
    gtk_main_quit();
    return;
  }
  GtkWidget *dialog = gtk_message_dialog_new(
      GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s failed",
      actions[GPOINTER_TO_INT(data)].label);
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s",
                                           error->message);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  g_error_free(error);
  busy = FALSE;
  for (int i = 0; i < count; i++)
    gtk_widget_set_sensitive(buttons[i], TRUE);
  restore_footer();
  select_button(selected);
}
static void activate(GtkWidget *widget, gpointer data) {
  (void)widget;
  if (busy)
    return;
  select_button(GPOINTER_TO_INT(data));
  busy = TRUE;
  for (int i = 0; i < count; i++)
    gtk_widget_set_sensitive(buttons[i], FALSE);
  gtk_label_set_text(GTK_LABEL(footer), "Waiting for the system…");
  Action action = visible[selected];
  GTask *task =
      g_task_new(NULL, NULL, action_finished, GINT_TO_POINTER(action));
  g_task_set_task_data(task, GINT_TO_POINTER(action), NULL);
  g_task_run_in_thread(task, run_action);
  g_object_unref(task);
}
static gboolean key_pressed(GtkWidget *widget, GdkEventKey *event,
                            gpointer data) {
  (void)widget;
  (void)data;
  if (busy ||
      (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK)))
    return FALSE;
  guint key = event->keyval;
  if (key == GDK_KEY_Escape || key == GDK_KEY_q || key == GDK_KEY_Q) {
    gtk_main_quit();
    return TRUE;
  }
  if (!count)
    return FALSE;
  if (key == GDK_KEY_Up || key == GDK_KEY_Left || key == GDK_KEY_Down ||
      key == GDK_KEY_Right || key == GDK_KEY_Tab ||
      key == GDK_KEY_ISO_Left_Tab) {
    int delta =
        key == GDK_KEY_Up || key == GDK_KEY_Left || key == GDK_KEY_ISO_Left_Tab
            ? -1
            : 1;
    select_button(move_selection(selected, count, delta));
    return TRUE;
  }
  if (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter ||
      key == GDK_KEY_space) {
    activate(NULL, GINT_TO_POINTER(selected));
    return TRUE;
  }
  if (key >= GDK_KEY_KP_1 && key <= GDK_KEY_KP_9)
    key = '1' + key - GDK_KEY_KP_1;
  int index = action_for_key(visible, count, gdk_keyval_to_unicode(key));
  if (index >= 0) {
    activate(NULL, GINT_TO_POINTER(index));
    return TRUE;
  }
  return FALSE;
}
int main(int argc, char **argv) {
  if (!parse_options(argc, argv, &options)) {
    print_help();
    return 2;
  }
  if (options.help) {
    print_help();
    return 0;
  }
  discover();
  if (options.list) {
    for (int i = 0; i < count; i++)
      g_print("%d  %s [%c]\n", i + 1, actions[visible[i]].label,
              actions[visible[i]].key);
    return 0;
  }
  if (!gtk_init_check(NULL, NULL)) {
    g_printerr("Byebye needs a graphical desktop session.\n");
    return 1;
  }
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "Byebye");
  gtk_window_set_default_size(GTK_WINDOW(window), 580,
                              220 + ((count + 1) / 2) * 78);
  gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
  gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
  gtk_window_set_keep_above(GTK_WINDOW(window), TRUE);
  g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
  g_signal_connect(window, "key-press-event", G_CALLBACK(key_pressed), NULL);
  GtkCssProvider *css = gtk_css_provider_new();
  gtk_css_provider_load_from_data(
      css,
      "window { background: #1d2020; } label { color: #aaa192; }"
      "label.heading { color: #4da3a8; font: bold 24px monospace; margin: "
      "24px; }"
      "button { background: #1d2020; color: #aaa192; border: 1px solid "
      "#aaa192; border-radius: 7px; box-shadow: none; font: 19px monospace; "
      "min-height: 54px; }"
      "button.selected, button:focus { color: #7ab87a; border: 2px solid "
      "#7ab87a; }"
      "button:hover { background: #29362b; } button:disabled { opacity: 0.45; "
      "}",
      -1, NULL);
  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(css);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 22);
  gtk_container_set_border_width(GTK_CONTAINER(box), 30);
  gtk_container_add(GTK_CONTAINER(window), box);
  GtkWidget *heading = gtk_label_new("Where do you want to GO?");
  gtk_style_context_add_class(gtk_widget_get_style_context(heading), "heading");
  gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 24);
  gtk_grid_set_row_spacing(GTK_GRID(grid), 20);
  gtk_box_pack_start(GTK_BOX(box), grid, TRUE, TRUE, 0);
  for (int i = 0; i < count; i++) {
    char *label =
        g_strdup_printf("%d  %s   [%c]", i + 1, actions[visible[i]].label,
                        actions[visible[i]].key);
    buttons[i] = gtk_button_new_with_label(label);
    g_free(label);
    gtk_widget_set_hexpand(buttons[i], TRUE);
    gtk_grid_attach(GTK_GRID(grid), buttons[i], i % 2, i / 2,
                    (i == count - 1 && count % 2) ? 2 : 1, 1);
    g_signal_connect(buttons[i], "clicked", G_CALLBACK(activate),
                     GINT_TO_POINTER(i));
  }
  if (!count)
    gtk_box_pack_start(
        GTK_BOX(box),
        gtk_label_new("No supported session or power services were found."),
        TRUE, TRUE, 0);
  footer = gtk_label_new("");
  restore_footer();
  gtk_box_pack_end(GTK_BOX(box), footer, FALSE, FALSE, 5);
  gtk_widget_show_all(window);
  if (count)
    select_button(0);
  gtk_window_present(GTK_WINDOW(window));
  gtk_main();
  g_clear_object(&system_bus);
  g_clear_object(&session_bus);
  return 0;
}
