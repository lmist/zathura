/* SPDX-License-Identifier: Zlib */

#include "pdfdb-explorer.h"

#include <girara-gtk/session.h>
#include <girara-gtk/settings.h>
#include <girara/datastructures.h>
#include <girara/utils.h>
#include <glib/gi18n.h>
#include <string.h>

#include "zathura.h"

enum {
  COLUMN_TITLE,
  COLUMN_META,
  COLUMN_POINTER,
  COLUMN_COUNT,
};

struct zathura_pdfdb_explorer_s {
  zathura_t* zathura;
  GtkWidget* widget;
  GtkWidget* query_label;
  GtkWidget* treeview;
  GtkListStore* store;
  girara_list_t* documents;
  char* query;
  bool loaded;
  bool pending_g;
};

static gboolean cb_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);
static void cb_row_activated(GtkTreeView* tree_view, GtkTreePath* path, GtkTreeViewColumn* column, gpointer data);

static bool contains_folded(const char* haystack, const char* needle) {
  if (needle == NULL || *needle == '\0') {
    return true;
  }
  if (haystack == NULL) {
    return false;
  }
  g_autofree char* folded_haystack = g_utf8_casefold(haystack, -1);
  g_autofree char* folded_needle = g_utf8_casefold(needle, -1);
  return folded_haystack != NULL && folded_needle != NULL && strstr(folded_haystack, folded_needle) != NULL;
}

static bool document_matches(const zathura_pdfdb_document_t* doc, const char* query) {
  return contains_folded(doc->title, query) || contains_folded(doc->slug, query) ||
         contains_folded(doc->filename, query) || contains_folded(doc->file_path, query) ||
         contains_folded(doc->source_url, query);
}

static void update_query_label(zathura_pdfdb_explorer_t* explorer) {
  const char* query = explorer->query != NULL && *explorer->query != '\0' ? explorer->query : _("type to search");
  g_autofree char* text = g_strdup_printf("pdfdb: %s", query);
  gtk_label_set_text(GTK_LABEL(explorer->query_label), text);
}

static void select_first(zathura_pdfdb_explorer_t* explorer) {
  GtkTreeModel* model = GTK_TREE_MODEL(explorer->store);
  GtkTreeIter iter;
  if (gtk_tree_model_get_iter_first(model, &iter) == TRUE) {
    GtkTreePath* path = gtk_tree_model_get_path(model, &iter);
    GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(explorer->treeview));
    gtk_tree_selection_select_path(selection, path);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(explorer->treeview), path, NULL, false, 0.0f, 0.0f);
    gtk_tree_path_free(path);
  }
}

static void rebuild(zathura_pdfdb_explorer_t* explorer) {
  gtk_list_store_clear(explorer->store);
  if (explorer->documents == NULL) {
    update_query_label(explorer);
    return;
  }

  unsigned int limit = 200;
  int configured_limit = 0;
  girara_setting_get(explorer->zathura->ui.session, "pdfdb-result-limit", &configured_limit);
  if (configured_limit > 0) {
    limit = (unsigned int)configured_limit;
  }

  const unsigned int size = girara_list_size(explorer->documents);
  for (unsigned int i = 0, added = 0; i != size && added < limit; ++i) {
    zathura_pdfdb_document_t* doc = girara_list_nth(explorer->documents, i);
    if (doc == NULL || document_matches(doc, explorer->query) == false) {
      continue;
    }
    GtkTreeIter iter;
    gtk_list_store_append(explorer->store, &iter);
    g_autofree char* meta = g_strdup_printf("%s  %d pages  %" G_GINT64_FORMAT " bytes",
                                            doc->filename != NULL ? doc->filename : doc->slug, doc->page_count,
                                            (gint64)doc->size_bytes);
    gtk_list_store_set(explorer->store, &iter, COLUMN_TITLE, doc->title != NULL ? doc->title : doc->slug, COLUMN_META,
                       meta, COLUMN_POINTER, doc, -1);
    ++added;
  }
  update_query_label(explorer);
  select_first(explorer);
}

static bool ensure_loaded(zathura_pdfdb_explorer_t* explorer) {
  if (explorer->loaded == true) {
    return true;
  }

  GError* error = NULL;
  explorer->documents = zathura_pdfdb_list_documents(explorer->zathura, &error);
  if (explorer->documents == NULL) {
    girara_notify(explorer->zathura->ui.session, GIRARA_ERROR, _("pdfdb: %s"),
                  error != NULL ? error->message : _("could not load documents"));
    g_clear_error(&error);
    return false;
  }
  explorer->loaded = true;
  rebuild(explorer);
  return true;
}

zathura_pdfdb_explorer_t* zathura_pdfdb_explorer_new(zathura_t* zathura) {
  g_return_val_if_fail(zathura != NULL, NULL);

  zathura_pdfdb_explorer_t* explorer = g_try_new0(zathura_pdfdb_explorer_t, 1);
  if (explorer == NULL) {
    return NULL;
  }
  explorer->zathura = zathura;
  explorer->query = g_strdup("");

  explorer->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(explorer->widget, 280, -1);
  gtk_style_context_add_class(gtk_widget_get_style_context(explorer->widget), "pdfdb-explorer");

  explorer->query_label = gtk_label_new(NULL);
  gtk_label_set_xalign(GTK_LABEL(explorer->query_label), 0.0f);
  gtk_box_pack_start(GTK_BOX(explorer->widget), explorer->query_label, false, false, 0);

  explorer->store = gtk_list_store_new(COLUMN_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
  explorer->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(explorer->store));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(explorer->treeview), false);
  gtk_tree_view_set_enable_search(GTK_TREE_VIEW(explorer->treeview), false);

  GtkCellRenderer* title_renderer = gtk_cell_renderer_text_new();
  GtkCellRenderer* meta_renderer = gtk_cell_renderer_text_new();
  g_object_set(G_OBJECT(title_renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  g_object_set(G_OBJECT(meta_renderer), "ellipsize", PANGO_ELLIPSIZE_END, "scale", 0.82, NULL);
  GtkTreeViewColumn* column = gtk_tree_view_column_new();
  gtk_tree_view_column_pack_start(column, title_renderer, true);
  gtk_tree_view_column_pack_start(column, meta_renderer, true);
  gtk_tree_view_column_add_attribute(column, title_renderer, "text", COLUMN_TITLE);
  gtk_tree_view_column_add_attribute(column, meta_renderer, "text", COLUMN_META);
  gtk_tree_view_append_column(GTK_TREE_VIEW(explorer->treeview), column);

  GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroll), explorer->treeview);
  gtk_box_pack_start(GTK_BOX(explorer->widget), scroll, true, true, 0);

  g_signal_connect(G_OBJECT(explorer->treeview), "key-press-event", G_CALLBACK(cb_key_press), explorer);
  g_signal_connect(G_OBJECT(explorer->treeview), "row-activated", G_CALLBACK(cb_row_activated), explorer);
  gtk_widget_set_visible(explorer->query_label, true);
  gtk_widget_set_visible(explorer->treeview, true);
  gtk_widget_set_visible(scroll, true);
  update_query_label(explorer);
  return explorer;
}

void zathura_pdfdb_explorer_free(zathura_pdfdb_explorer_t* explorer) {
  if (explorer == NULL) {
    return;
  }
  if (explorer->documents != NULL) {
    girara_list_free(explorer->documents);
  }
  g_clear_object(&explorer->store);
  g_free(explorer->query);
  g_free(explorer);
}

GtkWidget* zathura_pdfdb_explorer_get_widget(zathura_pdfdb_explorer_t* explorer) {
  return explorer != NULL ? explorer->widget : NULL;
}

bool zathura_pdfdb_explorer_is_visible(zathura_pdfdb_explorer_t* explorer) {
  return explorer != NULL && gtk_widget_get_visible(explorer->widget) == TRUE;
}

bool zathura_pdfdb_explorer_hide(zathura_pdfdb_explorer_t* explorer) {
  if (explorer == NULL) {
    return false;
  }
  gtk_widget_hide(explorer->widget);
  girara_mode_set(explorer->zathura->ui.session, explorer->zathura->modes.normal);
  gtk_widget_grab_focus(explorer->zathura->ui.session->gtk.view);
  return true;
}

bool zathura_pdfdb_explorer_toggle(zathura_pdfdb_explorer_t* explorer) {
  if (explorer == NULL) {
    return false;
  }
  if (zathura_pdfdb_explorer_is_visible(explorer)) {
    return zathura_pdfdb_explorer_hide(explorer);
  }
  if (ensure_loaded(explorer) == false) {
    return false;
  }
  gtk_widget_show(explorer->widget);
  gtk_widget_grab_focus(explorer->treeview);
  girara_mode_set(explorer->zathura->ui.session, explorer->zathura->modes.pdfdb);
  return true;
}

static zathura_pdfdb_document_t* selected_document(zathura_pdfdb_explorer_t* explorer) {
  GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(explorer->treeview));
  GtkTreeModel* model = NULL;
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected(selection, &model, &iter) == FALSE) {
    return NULL;
  }
  zathura_pdfdb_document_t* doc = NULL;
  gtk_tree_model_get(model, &iter, COLUMN_POINTER, &doc, -1);
  return doc;
}

static void move_selection(zathura_pdfdb_explorer_t* explorer, int delta) {
  GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(explorer->treeview));
  GtkTreeModel* model = GTK_TREE_MODEL(explorer->store);
  GtkTreePath* path = NULL;
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected(selection, NULL, &iter) == TRUE) {
    path = gtk_tree_model_get_path(model, &iter);
  } else {
    select_first(explorer);
    return;
  }
  int* indices = gtk_tree_path_get_indices(path);
  int next = indices != NULL ? indices[0] + delta : 0;
  const int count = gtk_tree_model_iter_n_children(model, NULL);
  if (next < 0) {
    next = 0;
  } else if (next >= count) {
    next = count - 1;
  }
  gtk_tree_path_free(path);
  if (next < 0) {
    return;
  }
  path = gtk_tree_path_new_from_indices(next, -1);
  gtk_tree_selection_select_path(selection, path);
  gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(explorer->treeview), path, NULL, false, 0.0f, 0.0f);
  gtk_tree_path_free(path);
}

static void move_to_edge(zathura_pdfdb_explorer_t* explorer, bool last) {
  const int count = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(explorer->store), NULL);
  if (count <= 0) {
    return;
  }
  GtkTreePath* path = gtk_tree_path_new_from_indices(last ? count - 1 : 0, -1);
  GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(explorer->treeview));
  gtk_tree_selection_select_path(selection, path);
  gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(explorer->treeview), path, NULL, false, 0.0f, 0.0f);
  gtk_tree_path_free(path);
}

static void open_selected(zathura_pdfdb_explorer_t* explorer, zathura_pdfdb_open_target_t target) {
  zathura_pdfdb_document_t* doc = selected_document(explorer);
  if (doc != NULL && zathura_pdfdb_open(explorer->zathura, doc, target)) {
    if (target != ZATHURA_PDFDB_OPEN_WINDOW) {
      zathura_pdfdb_explorer_hide(explorer);
    }
  }
}

static void append_query(zathura_pdfdb_explorer_t* explorer, gunichar ch) {
  if (!g_unichar_isprint(ch)) {
    return;
  }
  char buffer[8] = {0};
  const int len = g_unichar_to_utf8(ch, buffer);
  buffer[len] = '\0';
  g_autofree char* next = g_strconcat(explorer->query != NULL ? explorer->query : "", buffer, NULL);
  g_free(explorer->query);
  explorer->query = g_steal_pointer(&next);
  explorer->pending_g = false;
  rebuild(explorer);
}

static gboolean cb_key_press(GtkWidget* UNUSED(widget), GdkEventKey* event, gpointer data) {
  zathura_pdfdb_explorer_t* explorer = data;
  if (explorer == NULL || event == NULL) {
    return GDK_EVENT_PROPAGATE;
  }

  if ((event->state & GDK_META_MASK) != 0 && (event->keyval == GDK_KEY_b || event->keyval == GDK_KEY_B)) {
    zathura_pdfdb_explorer_hide(explorer);
    return GDK_EVENT_STOP;
  }

  switch (event->keyval) {
    case GDK_KEY_Escape:
      explorer->pending_g = false;
      zathura_pdfdb_explorer_hide(explorer);
      return GDK_EVENT_STOP;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_o:
      explorer->pending_g = false;
      open_selected(explorer, ZATHURA_PDFDB_OPEN_CURRENT);
      return GDK_EVENT_STOP;
    case GDK_KEY_t:
      explorer->pending_g = false;
      open_selected(explorer, ZATHURA_PDFDB_OPEN_TAB);
      return GDK_EVENT_STOP;
    case GDK_KEY_w:
      explorer->pending_g = false;
      open_selected(explorer, ZATHURA_PDFDB_OPEN_WINDOW);
      return GDK_EVENT_STOP;
    case GDK_KEY_v:
      explorer->pending_g = false;
      open_selected(explorer, ZATHURA_PDFDB_OPEN_SPLIT);
      return GDK_EVENT_STOP;
    case GDK_KEY_j:
    case GDK_KEY_Down:
      explorer->pending_g = false;
      move_selection(explorer, 1);
      return GDK_EVENT_STOP;
    case GDK_KEY_k:
    case GDK_KEY_Up:
      explorer->pending_g = false;
      move_selection(explorer, -1);
      return GDK_EVENT_STOP;
    case GDK_KEY_d:
      if ((event->state & GDK_CONTROL_MASK) != 0) {
        explorer->pending_g = false;
        move_selection(explorer, 10);
        return GDK_EVENT_STOP;
      }
      break;
    case GDK_KEY_u:
      if ((event->state & GDK_CONTROL_MASK) != 0) {
        explorer->pending_g = false;
        move_selection(explorer, -10);
        return GDK_EVENT_STOP;
      }
      break;
    case GDK_KEY_BackSpace:
      if (explorer->query != NULL && *explorer->query != '\0') {
        char* previous = g_utf8_find_prev_char(explorer->query, explorer->query + strlen(explorer->query));
        if (previous != NULL) {
          *previous = '\0';
          rebuild(explorer);
        }
      }
      explorer->pending_g = false;
      return GDK_EVENT_STOP;
    case GDK_KEY_G:
      explorer->pending_g = false;
      move_to_edge(explorer, true);
      return GDK_EVENT_STOP;
    case GDK_KEY_g:
      if (explorer->pending_g == true) {
        explorer->pending_g = false;
        move_to_edge(explorer, false);
        return GDK_EVENT_STOP;
      }
      explorer->pending_g = true;
      return GDK_EVENT_STOP;
    default:
      break;
  }

  gunichar ch = gdk_keyval_to_unicode(event->keyval);
  if ((event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_META_MASK)) == 0 && ch != 0) {
    append_query(explorer, ch);
    return GDK_EVENT_STOP;
  }

  return GDK_EVENT_PROPAGATE;
}

static void cb_row_activated(GtkTreeView* UNUSED(tree_view), GtkTreePath* UNUSED(path),
                             GtkTreeViewColumn* UNUSED(column), gpointer data) {
  open_selected(data, ZATHURA_PDFDB_OPEN_CURRENT);
}
