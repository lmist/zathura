/* SPDX-License-Identifier: Zlib */

#include "pdfdb.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <girara-gtk/session.h>
#include <girara-gtk/settings.h>
#include <girara/utils.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "pdfdb-bridge.h"
#include "zathura.h"

enum {
  PDFDB_ERROR_FAILED,
};

static GQuark pdfdb_error_quark(void) {
  return g_quark_from_static_string("zathura-pdfdb-error");
}

void zathura_pdfdb_document_free(zathura_pdfdb_document_t* document) {
  if (document == NULL) {
    return;
  }
  g_free(document->id);
  g_free(document->slug);
  g_free(document->title);
  g_free(document->filename);
  g_free(document->source_url);
  g_free(document->sha256);
  g_free(document->file_path);
  g_free(document);
}

zathura_pdfdb_document_t* zathura_pdfdb_document_copy(const zathura_pdfdb_document_t* document) {
  if (document == NULL) {
    return NULL;
  }
  zathura_pdfdb_document_t* copy = g_try_new0(zathura_pdfdb_document_t, 1);
  if (copy == NULL) {
    return NULL;
  }
  copy->id            = g_strdup(document->id);
  copy->slug          = g_strdup(document->slug);
  copy->title         = g_strdup(document->title);
  copy->filename      = g_strdup(document->filename);
  copy->source_url    = g_strdup(document->source_url);
  copy->sha256        = g_strdup(document->sha256);
  copy->file_path     = g_strdup(document->file_path);
  copy->size_bytes    = document->size_bytes;
  copy->page_count    = document->page_count;
  copy->created_at_us = document->created_at_us;
  copy->updated_at_us = document->updated_at_us;
  return copy;
}

bool zathura_pdfdb_uri_parse(const char* uri, char** key) {
  if (key != NULL) {
    *key = NULL;
  }
  if (uri == NULL) {
    return false;
  }
  const char* prefix = "pdfdb://doc/";
  if (g_str_has_prefix(uri, prefix) == false) {
    return false;
  }
  const char* value = uri + strlen(prefix);
  if (*value == '\0') {
    return false;
  }
  if (key != NULL) {
    *key = g_uri_unescape_string(value, NULL);
  }
  return true;
}

char* zathura_pdfdb_document_uri(const zathura_pdfdb_document_t* document) {
  if (document == NULL || document->slug == NULL) {
    return NULL;
  }
  g_autofree char* escaped = g_uri_escape_string(document->slug, NULL, true);
  return g_strdup_printf("pdfdb://doc/%s", escaped);
}

static char* setting_string(zathura_t* zathura, const char* name) {
  char* value = NULL;
  girara_setting_get(zathura->ui.session, name, &value);
  return value;
}

static int setting_int(zathura_t* zathura, const char* name) {
  int value = 0;
  girara_setting_get(zathura->ui.session, name, &value);
  return value;
}

static char* cache_dir(zathura_t* zathura) {
  g_autofree char* configured = setting_string(zathura, "pdfdb-cache-dir");
  if (configured != NULL && *configured != '\0') {
    return girara_fix_path(configured);
  }
  return g_build_filename(zathura->config.cache_dir, "pdfdb", "documents", NULL);
}

static char* short_sha(const char* sha) {
  if (sha == NULL || strlen(sha) < 12) {
    return g_strdup("unknown");
  }
  return g_strndup(sha, 12);
}

char* zathura_pdfdb_cache_path(zathura_t* zathura, const zathura_pdfdb_document_t* document) {
  if (zathura == NULL || document == NULL || document->slug == NULL) {
    return NULL;
  }
  g_autofree char* dir = cache_dir(zathura);
  g_autofree char* sha = short_sha(document->sha256);
  g_autofree char* filename = g_strdup_printf("%s-%s.pdf", document->slug, sha);
  return g_build_filename(dir, filename, NULL);
}

static bool file_matches(const char* path, int64_t expected_size, const char* expected_sha256) {
  if (path == NULL || expected_size < 0) {
    return false;
  }
  GStatBuf st;
  if (g_stat(path, &st) != 0) {
    return false;
  }
  if (S_ISREG(st.st_mode) == false || st.st_size != expected_size) {
    return false;
  }
  if (expected_sha256 == NULL || *expected_sha256 == '\0') {
    return true;
  }

  gchar* data = NULL;
  gsize size  = 0;
  if (g_file_get_contents(path, &data, &size, NULL) == false) {
    return false;
  }
  g_autofree gchar* checksum = g_compute_checksum_for_data(G_CHECKSUM_SHA256, (const guchar*)data, size);
  g_free(data);
  return checksum != NULL && g_ascii_strcasecmp(checksum, expected_sha256) == 0;
}

girara_list_t* zathura_pdfdb_list_documents(zathura_t* zathura, GError** error) {
  g_return_val_if_fail(zathura != NULL, NULL);
  g_autofree char* database_url = setting_string(zathura, "pdfdb-database-url");
  g_autofree char* user = setting_string(zathura, "pdfdb-dbos-user");
  int limit = setting_int(zathura, "pdfdb-result-limit");
  if (limit <= 0) {
    limit = 200;
  }

  zathura_pdfdb_document_t** docs = NULL;
  size_t count                    = 0;
  char* message                   = NULL;
  if (zathura_pdfdb_bridge_list(database_url, user, limit, &docs, &count, &message) != 0) {
    g_set_error(error, pdfdb_error_quark(), PDFDB_ERROR_FAILED, "%s", message != NULL ? message : "pdfdb list failed");
    zathura_pdfdb_bridge_free_error(message);
    return NULL;
  }

  girara_list_t* out = girara_list_new();
  if (out == NULL) {
    for (size_t i = 0; i != count; ++i) {
      zathura_pdfdb_document_free(docs[i]);
    }
    g_free(docs);
    g_set_error(error, pdfdb_error_quark(), PDFDB_ERROR_FAILED, "%s", _("Out of memory"));
    return NULL;
  }
  girara_list_set_free_function(out, (girara_free_function_t)zathura_pdfdb_document_free);
  for (size_t i = 0; i != count; ++i) {
    girara_list_append(out, docs[i]);
  }
  g_free(docs);
  return out;
}

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

girara_list_t* zathura_pdfdb_filter_documents(girara_list_t* documents, const char* query, unsigned int limit) {
  if (documents == NULL) {
    return NULL;
  }
  girara_list_t* out = girara_list_new();
  if (out == NULL) {
    return NULL;
  }
  girara_list_set_free_function(out, (girara_free_function_t)zathura_pdfdb_document_free);
  const unsigned int size = girara_list_size(documents);
  for (unsigned int i = 0; i != size; ++i) {
    const zathura_pdfdb_document_t* doc = girara_list_nth(documents, i);
    if (doc != NULL && document_matches(doc, query)) {
      girara_list_append(out, zathura_pdfdb_document_copy(doc));
      if (limit > 0 && girara_list_size(out) >= limit) {
        break;
      }
    }
  }
  return out;
}

static zathura_pdfdb_document_t* get_document(zathura_t* zathura, const char* key, GError** error) {
  g_autofree char* database_url = setting_string(zathura, "pdfdb-database-url");
  g_autofree char* user = setting_string(zathura, "pdfdb-dbos-user");
  zathura_pdfdb_document_t* doc = NULL;
  char* message                 = NULL;
  if (zathura_pdfdb_bridge_get(database_url, user, key, &doc, &message) != 0) {
    g_set_error(error, pdfdb_error_quark(), PDFDB_ERROR_FAILED, "%s", message != NULL ? message : "pdfdb lookup failed");
    zathura_pdfdb_bridge_free_error(message);
    return NULL;
  }
  return doc;
}

static bool write_cache_file(const char* path, const unsigned char* data, size_t size, GError** error) {
  g_autofree char* dir = g_path_get_dirname(path);
  if (g_mkdir_with_parents(dir, 0700) != 0) {
    g_set_error(error, pdfdb_error_quark(), PDFDB_ERROR_FAILED, _("Could not create cache directory '%s': %s"), dir,
                g_strerror(errno));
    return false;
  }

  g_autofree char* tmp = g_strdup_printf("%s.tmp-%u", path, g_random_int());
  if (g_file_set_contents(tmp, (const char*)data, size, error) == false) {
    return false;
  }
  if (g_rename(tmp, path) != 0) {
    g_set_error(error, pdfdb_error_quark(), PDFDB_ERROR_FAILED, _("Could not move cache file into place: %s"),
                g_strerror(errno));
    return false;
  }
  return true;
}

char* zathura_pdfdb_resolve_uri(zathura_t* zathura, const char* uri, char** display_uri, GError** error) {
  if (display_uri != NULL) {
    *display_uri = NULL;
  }
  g_autofree char* key = NULL;
  if (zathura_pdfdb_uri_parse(uri, &key) == false) {
    g_set_error(error, pdfdb_error_quark(), PDFDB_ERROR_FAILED, _("Invalid pdfdb URI: %s"), uri);
    return NULL;
  }

  g_autoptr(zathura_pdfdb_document_t) doc = get_document(zathura, key, error);
  if (doc == NULL) {
    return NULL;
  }
  g_autofree char* path = zathura_pdfdb_cache_path(zathura, doc);
  if (file_matches(path, doc->size_bytes, doc->sha256)) {
    if (display_uri != NULL) {
      *display_uri = g_strdup(uri);
    }
    return g_strdup(path);
  }

  g_autofree char* database_url = setting_string(zathura, "pdfdb-database-url");
  g_autofree char* user = setting_string(zathura, "pdfdb-dbos-user");
  unsigned char* data = NULL;
  size_t size         = 0;
  char* message       = NULL;
  if (zathura_pdfdb_bridge_read_file(database_url, user, doc->file_path, &data, &size, &message) != 0) {
    g_set_error(error, pdfdb_error_quark(), PDFDB_ERROR_FAILED, "%s", message != NULL ? message : "pdfdb read failed");
    zathura_pdfdb_bridge_free_error(message);
    return NULL;
  }
  bool ok = write_cache_file(path, data, size, error);
  zathura_pdfdb_bridge_free_data(data);
  if (ok == false) {
    return NULL;
  }
  if (display_uri != NULL) {
    *display_uri = g_strdup(uri);
  }
  return g_strdup(path);
}

bool zathura_pdfdb_open(zathura_t* zathura, const zathura_pdfdb_document_t* document,
                        zathura_pdfdb_open_target_t target) {
  if (zathura == NULL || document == NULL) {
    return false;
  }
  g_autofree char* uri = zathura_pdfdb_document_uri(document);
  if (uri == NULL) {
    return false;
  }

  if (target == ZATHURA_PDFDB_OPEN_WINDOW) {
    const char* executable = (zathura->global.arguments != NULL && zathura->global.arguments[0] != NULL) ?
                                 zathura->global.arguments[0] :
                                 "zathura";
    const char* argv[] = {executable, uri, NULL};
    GError* error = NULL;
    if (g_spawn_async(NULL, (char**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error) == FALSE) {
      girara_notify(zathura->ui.session, GIRARA_ERROR, _("Could not open pdfdb document in a new window: %s"),
                    error != NULL ? error->message : _("unknown error"));
      g_clear_error(&error);
      return false;
    }
    return true;
  }

  if (target == ZATHURA_PDFDB_OPEN_TAB || target == ZATHURA_PDFDB_OPEN_SPLIT) {
    girara_notify(zathura->ui.session, GIRARA_WARNING,
                  _("Tabs and split panes are registered but still use the current document slot in this build."));
  }
  if (zathura_has_document(zathura) == true) {
    document_close(zathura, false);
  }
  document_open_idle(zathura, uri, NULL, ZATHURA_PAGE_NUMBER_UNSPECIFIED, NULL, NULL, NULL, NULL);
  return true;
}
