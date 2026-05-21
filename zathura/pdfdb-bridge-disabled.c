/* SPDX-License-Identifier: Zlib */

#include "pdfdb-bridge.h"

#include <glib.h>
#include <string.h>

static int disabled(char** error) {
  if (error != NULL) {
    *error = g_strdup("pdfdb support was not enabled at build time");
  }
  return -1;
}

int zathura_pdfdb_bridge_list(const char* UNUSED(database_url), const char* UNUSED(user), int UNUSED(limit),
                              zathura_pdfdb_document_t*** UNUSED(documents), size_t* UNUSED(count), char** error) {
  return disabled(error);
}

int zathura_pdfdb_bridge_get(const char* UNUSED(database_url), const char* UNUSED(user), const char* UNUSED(key),
                             zathura_pdfdb_document_t** UNUSED(document), char** error) {
  return disabled(error);
}

int zathura_pdfdb_bridge_read_file(const char* UNUSED(database_url), const char* UNUSED(user),
                                   const char* UNUSED(file_path), unsigned char** UNUSED(data),
                                   size_t* UNUSED(size), char** error) {
  return disabled(error);
}

void zathura_pdfdb_bridge_free_data(unsigned char* data) {
  g_free(data);
}

void zathura_pdfdb_bridge_free_error(char* error) {
  g_free(error);
}
