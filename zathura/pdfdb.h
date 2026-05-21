/* SPDX-License-Identifier: Zlib */

#ifndef ZATHURA_PDFDB_H
#define ZATHURA_PDFDB_H

#include <stdbool.h>
#include <stdint.h>

#include <girara/datastructures.h>
#include <glib.h>

#include "types.h"

typedef struct zathura_pdfdb_document_s {
  char* id;
  char* slug;
  char* title;
  char* filename;
  char* source_url;
  char* sha256;
  char* file_path;
  int64_t size_bytes;
  int page_count;
  int64_t created_at_us;
  int64_t updated_at_us;
} zathura_pdfdb_document_t;

typedef enum zathura_pdfdb_open_target_e {
  ZATHURA_PDFDB_OPEN_CURRENT,
  ZATHURA_PDFDB_OPEN_TAB,
  ZATHURA_PDFDB_OPEN_WINDOW,
  ZATHURA_PDFDB_OPEN_SPLIT,
} zathura_pdfdb_open_target_t;

void zathura_pdfdb_document_free(zathura_pdfdb_document_t* document);
zathura_pdfdb_document_t* zathura_pdfdb_document_copy(const zathura_pdfdb_document_t* document);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(zathura_pdfdb_document_t, zathura_pdfdb_document_free)

bool zathura_pdfdb_uri_parse(const char* uri, char** key);
char* zathura_pdfdb_document_uri(const zathura_pdfdb_document_t* document);
char* zathura_pdfdb_cache_path(zathura_t* zathura, const zathura_pdfdb_document_t* document);

girara_list_t* zathura_pdfdb_list_documents(zathura_t* zathura, GError** error);
girara_list_t* zathura_pdfdb_filter_documents(girara_list_t* documents, const char* query, unsigned int limit);
char* zathura_pdfdb_resolve_uri(zathura_t* zathura, const char* uri, char** display_uri, GError** error);
bool zathura_pdfdb_open(zathura_t* zathura, const zathura_pdfdb_document_t* document,
                        zathura_pdfdb_open_target_t target);

#endif
