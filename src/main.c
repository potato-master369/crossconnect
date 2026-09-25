// main.c
// ----------------------------
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int pipe_fd[2];
FILE *stdstream;
GtkApplication *app;
GtkWidget *statuslabel;
guint io_watch_id = 0;
pid_t openvpn_pid = -1;
bool stdread_safe = false;
bool is_started = false;
bool path_selected = false;
char path[256];

static void load_conf(GObject *so, GAsyncResult *res, gpointer user_data) {
  GtkFileDialog *d = GTK_FILE_DIALOG(so);
  GError *err = NULL;
  GFile *f = gtk_file_dialog_open_finish(d, res, &err);

  if (err != NULL) {
    g_printerr("Error choosing file: %s\n", err->message);
    g_error_free(err);
    return;
  }

  if (f != NULL) {
    char *path_tmp = g_file_get_path(f);
    strncpy(path, path_tmp, sizeof(path));
    path_selected = true;
    g_free(path_tmp);
    g_object_unref(f);
  }
}

static void on_button_click(GtkButton *button, gpointer user_data) {
  GtkWindow *parent_window = GTK_WINDOW(user_data);
  GtkFileDialog *fd = gtk_file_dialog_new();
  gtk_file_dialog_set_title(fd, "Select OpenVPN config");
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_add_suffix(filter, "ovpn");
  gtk_file_filter_set_name(filter, "OpenVPN config file");

  GListStore *fl = g_list_store_new(GTK_TYPE_FILE_FILTER);
  g_list_store_append(fl, filter);
  gtk_file_dialog_set_filters(fd, G_LIST_MODEL(fl));
  g_object_unref(filter);
  g_object_unref(fl);
  gtk_file_dialog_open(fd, parent_window, NULL, load_conf, NULL);
  g_object_unref(fd);
}

static void adjust_settings(GtkButton *button, gpointer user_data) {}

static gboolean on_pipe_data_ready(GIOChannel *source, GIOCondition condition,
                                   gpointer data) {
  gchar *str = NULL;
  gsize length = 0;
  GError *error = NULL;
  if (!stdread_safe) {
    return G_SOURCE_CONTINUE;
  }
  if (condition & G_IO_IN) {
    GIOStatus status =
        g_io_channel_read_line(source, &str, &length, NULL, &error);
    if (status == G_IO_STATUS_NORMAL && str != NULL) {
      g_strchomp(str);
      gtk_label_set_text(GTK_LABEL(statuslabel), str);
      g_free(str);
    }
  }

  if (condition & (G_IO_HUP | G_IO_ERR)) {
    return G_SOURCE_REMOVE;
  }

  return G_SOURCE_CONTINUE;
}

static void start_ovpn(GtkButton *b, gpointer u); // forward decl
static void stop_ovpn(GtkButton *button, gpointer user_data) {
  stdread_safe = false;

  if (io_watch_id > 0) {
    g_source_remove(io_watch_id);
    io_watch_id = 0;
  }

  if (openvpn_pid > 0) {
    kill(openvpn_pid, SIGTERM);
    openvpn_pid = -1;
  }

  if (stdstream) {
    fclose(stdstream);
    stdstream = NULL;
  }

  gtk_label_set_text(GTK_LABEL(statuslabel), "Status: Disconnected");
  gtk_button_set_label(button, "Start OpenVPN!");
  g_signal_handlers_disconnect_by_func(button, G_CALLBACK(stop_ovpn),
                                       user_data);
  g_signal_connect(button, "clicked", G_CALLBACK(start_ovpn), user_data);
}

static void start_ovpn(GtkButton *button, gpointer user_data) {
  if (is_started) {
    GtkAlertDialog *d = gtk_alert_dialog_new("Error!");
    gtk_alert_dialog_set_detail(d, "Already running.");
    gtk_alert_dialog_show(d, GTK_WINDOW(user_data));
    g_object_unref(d);
    return;
  }
  if (!path_selected) {
    GtkAlertDialog *d = gtk_alert_dialog_new("Error!");
    gtk_alert_dialog_set_detail(d, "No .ovpn config file selected.");
    gtk_alert_dialog_show(d, GTK_WINDOW(user_data));
    g_object_unref(d);
    return;
  }
  if (pipe(pipe_fd) < 0) {
    perror("pipe fail!");
    gtk_window_destroy(GTK_WINDOW(user_data));
  }
  pid_t pid = fork();
  if (pid < 0) {
    GtkAlertDialog *d = gtk_alert_dialog_new("Error!");
    gtk_alert_dialog_set_detail(d, "Fork failed!");
    gtk_alert_dialog_show(d, GTK_WINDOW(user_data));
    g_object_unref(d);
    return;
  } else if (pid == 0) {
    close(pipe_fd[0]);
    dup2(pipe_fd[1], STDOUT_FILENO);
    dup2(pipe_fd[1], STDERR_FILENO);
    close(pipe_fd[1]);
    printf("Starting OpenVPN!\n");
    char *args[] = {"pkexec", "openvpn", "--config", path, NULL};
    execvp(args[0], args);
    perror("execvp fail!");
    gtk_window_destroy(GTK_WINDOW(user_data)); // quit safely
  } else {
    // parent
    close(pipe_fd[1]);
    stdstream = fdopen(pipe_fd[0], "r");
    if (!stdstream) {
      perror("fdopen fail!");
      gtk_window_destroy(GTK_WINDOW(user_data));
    }
    stdread_safe = true;
    if (io_watch_id == 0) {
      GIOChannel *channel = g_io_channel_unix_new(pipe_fd[0]);
      g_io_channel_set_encoding(channel, NULL, NULL);

      io_watch_id =
          g_io_add_watch(channel, (GIOCondition)(G_IO_IN | G_IO_HUP | G_IO_ERR),
                         on_pipe_data_ready, NULL);

      g_io_channel_unref(channel);
    }
    gtk_button_set_label(button, "Disconnect");
  g_signal_handlers_disconnect_by_func(button, G_CALLBACK(start_ovpn),
                                       user_data);
    g_signal_connect(button, "clicked", G_CALLBACK(stop_ovpn), NULL);
    GNotification *n = g_notification_new("CrossConnect: success!");
    g_notification_set_body(n, "Successfully started OpenVPN.");
    g_application_send_notification(G_APPLICATION(app), "crossconnect", n);
    g_object_unref(n);
  }
}

static void activate(GtkApplication *app, gpointer user_data) {
  GtkWidget *w = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(w), "OpenVPN CrossConnect");
  gtk_window_set_default_size(GTK_WINDOW(w), 640, 480);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
  // a bit of styling :)
  gtk_widget_set_margin_bottom(box, 20);
  gtk_widget_set_margin_top(box, 20);
  gtk_widget_set_margin_start(box, 20);
  gtk_widget_set_margin_end(box, 20);
  statuslabel = gtk_label_new("Status: Not connected");
  gtk_widget_set_halign(statuslabel, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(box), statuslabel);
  GtkWidget *button = gtk_button_new_with_label("Load OpenVPN conf");
  g_signal_connect(button, "clicked", G_CALLBACK(on_button_click), w);
  gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
  gtk_box_append(GTK_BOX(box), button);
  GtkWidget *button2 = gtk_button_new_with_label("Set connection settings");
  g_signal_connect(button2, "clicked", G_CALLBACK(adjust_settings), NULL);
  gtk_box_append(GTK_BOX(box), button2);
  gtk_widget_set_halign(button2, GTK_ALIGN_CENTER);
  GtkWidget *button3 = gtk_button_new_with_label("Start OpenVPN!");
  g_signal_connect(button3, "clicked", G_CALLBACK(start_ovpn), w);
  gtk_box_append(GTK_BOX(box), button3);
  gtk_widget_set_halign(button3, GTK_ALIGN_CENTER);
  GtkWidget *label =
      gtk_label_new("OpenVPN CrossConnect - (C) potato-master369");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(box), label);
  gtk_window_set_child(GTK_WINDOW(w), box);
  gtk_window_present(GTK_WINDOW(w));
}

int main(int argc, char **argv) {
  app = gtk_application_new("io.github.potato-master369.openvpn",
                            G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int s =
      g_application_run(G_APPLICATION(app), 0, NULL); // stub out argc and argv
  g_object_unref(app);
  return s;
}
