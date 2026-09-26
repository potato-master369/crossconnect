// main.c
// ----------------------------
#include "config.h"
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

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

static GtkWidget *settings_window;
static GtkWidget *protocol_entry;
static GtkWidget *lastpath_entry;
static GtkWidget *port_entry;
static GtkWidget *username_entry;
static GtkWidget *password_entry;

// extern for our config
extern cc_config_t local_conf;

static void settings_field(GtkWidget *grid, const char *label_text,
                           GtkWidget *entry, int row) {
  GtkWidget *label = gtk_label_new(label_text);
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label, 0, row, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
  gtk_widget_set_hexpand(entry, true);
}

static void copy_entry_value(GtkWidget *entry, char *destination,
                             size_t destination_size) {
  strncpy(destination, gtk_editable_get_text(GTK_EDITABLE(entry)),
          destination_size - 1);
  destination[destination_size - 1] = '\0';
}

static void save_settings(GtkButton *button, gpointer user_data) {
  copy_entry_value(protocol_entry, local_conf.protocol,
                   sizeof(local_conf.protocol));
  copy_entry_value(lastpath_entry, local_conf.lastpath,
                   sizeof(local_conf.lastpath));
  local_conf.port = atoi(gtk_editable_get_text(GTK_EDITABLE(port_entry)));
  copy_entry_value(username_entry, local_conf.username,
                   sizeof(local_conf.username));
  copy_entry_value(password_entry, local_conf.password,
                   sizeof(local_conf.password));
  config_write_conf();
  gtk_window_destroy(GTK_WINDOW(settings_window));
  settings_window = NULL;
}

static void close_settings(GtkButton *button, gpointer user_data) {
  gtk_window_destroy(GTK_WINDOW(settings_window));
  settings_window = NULL;
}

static void adjust_settings(GtkButton *button, gpointer user_data) {
  if (settings_window != NULL) {
    gtk_window_present(GTK_WINDOW(settings_window));
    return;
  }

  settings_window = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(settings_window), "Connection settings");
  gtk_window_set_default_size(GTK_WINDOW(settings_window), 480, 360);
  gtk_window_set_transient_for(GTK_WINDOW(settings_window),
                               GTK_WINDOW(user_data));
  gtk_window_set_modal(GTK_WINDOW(settings_window), true);

  GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_set_margin_top(outer, 24);
  gtk_widget_set_margin_bottom(outer, 24);
  gtk_widget_set_margin_start(outer, 24);
  gtk_widget_set_margin_end(outer, 24);
  gtk_window_set_child(GTK_WINDOW(settings_window), outer);

  GtkWidget *heading = gtk_label_new("Connection settings");
  gtk_widget_set_halign(heading, GTK_ALIGN_START);
  gtk_widget_add_css_class(heading, "title-2");
  gtk_box_append(GTK_BOX(outer), heading);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
  gtk_box_append(GTK_BOX(outer), grid);

  protocol_entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(protocol_entry), local_conf.protocol);
  settings_field(grid, "Protocol", protocol_entry, 0);
  lastpath_entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(lastpath_entry), local_conf.lastpath);
  settings_field(grid, "Last path", lastpath_entry, 1);
  port_entry = gtk_entry_new();
  char port_text[32];
  snprintf(port_text, sizeof(port_text), "%d", local_conf.port);
  gtk_editable_set_text(GTK_EDITABLE(port_entry), port_text);
  settings_field(grid, "Port", port_entry, 2);
  username_entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(username_entry), local_conf.username);
  settings_field(grid, "Username", username_entry, 3);
  password_entry = gtk_entry_new();
  gtk_entry_set_visibility(GTK_ENTRY(password_entry), false);
  gtk_editable_set_text(GTK_EDITABLE(password_entry), local_conf.password);
  settings_field(grid, "Password", password_entry, 4);

  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  gtk_box_append(GTK_BOX(outer), actions);
  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  g_signal_connect(cancel, "clicked", G_CALLBACK(close_settings), NULL);
  gtk_box_append(GTK_BOX(actions), cancel);
  GtkWidget *save = gtk_button_new_with_label("Save settings");
  gtk_widget_add_css_class(save, "suggested-action");
  g_signal_connect(save, "clicked", G_CALLBACK(save_settings), NULL);
  gtk_box_append(GTK_BOX(actions), save);
  gtk_window_present(GTK_WINDOW(settings_window));
}

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
    char buf[256];
    snprintf(buf, sizeof(buf), "%d", openvpn_pid);
    pid_t child = fork();
    if (child == 0) {
      execlp("pkexec", "pkexec", "/bin/kill", buf, NULL);

      perror("execlp fail!");
      gtk_window_destroy(GTK_WINDOW(user_data));
    } else {
      ; // do nothing
    }
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
    config_read_config();
    char port_str[16] = {0};
    char credfile_path[] = "/tmp/crossconnect_cred_xxx";
    char *args[16] = {0};
    int i = 0;
    int have_credfile = 0;

    args[i++] = "pkexec";
    args[i++] = "openvpn";
    args[i++] = "--config";
    args[i++] = (char *)path;

    if (local_conf.port != -1) {
      snprintf(port_str, sizeof(port_str), "%d", local_conf.port);
      args[i++] = "--port";
      args[i++] = port_str;
    }

    if (strcmp(local_conf.protocol, "NP") != 0) {
      if (strcmp(local_conf.protocol, "udp") != 0 &&
          strcmp(local_conf.protocol, "tcp-client") != 0 &&
          strcmp(local_conf.protocol, "tcp-server") != 0) {
        errno = EINVAL;
        perror("invalid protocol");
      } else {
        args[i++] = "--proto";
        args[i++] = local_conf.protocol;
      }
    }

    if (strcmp(local_conf.username, "NP") != 0 ||
        strcmp(local_conf.password, "NP") != 0) {
      int fd = g_mkstemp(credfile_path);
      if (fd == -1) {
        perror("mkstemp");
      } else {
        if (fchmod(fd, S_IRUSR | S_IWUSR) == -1) {
          perror("fchmod");
        }
        FILE *f = fdopen(fd, "w");
        if (!f) {
          perror("fdopen");
          close(fd);
        } else {
          fprintf(f, "%s\n",
                  strcmp(local_conf.username, "NP") != 0 ? local_conf.username
                                                         : "");
          fprintf(f, "%s\n",
                  strcmp(local_conf.password, "NP") != 0 ? local_conf.password
                                                         : "");
          fclose(f);
          args[i++] = "--auth-user-pass";
          args[i++] = credfile_path;
          have_credfile = 1;
        }
      }
    }
    args[i] = NULL;

    /* debug: print what we built */
    for (int j = 0; args[j] != NULL; j++) {
      printf("args[%d] = %s\n", j, args[j]);
    }

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
    g_signal_connect(button, "clicked", G_CALLBACK(stop_ovpn), user_data);
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
  g_signal_connect(button2, "clicked", G_CALLBACK(adjust_settings), w);
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
  config_read_config();
  app = gtk_application_new("io.github.potato-master369.openvpn",
                            G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int s =
      g_application_run(G_APPLICATION(app), 0, NULL); // stub out argc and argv
  g_object_unref(app);
  return s;
}
