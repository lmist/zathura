/* SPDX-License-Identifier: Zlib */

#ifndef ZATHURA_PDFDB_BRIDGE_H
#define ZATHURA_PDFDB_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include "pdfdb.h"

#ifdef __cplusplus
extern "C" {
#endif

int zathura_pdfdb_bridge_list(const char* database_url, const char* user, int limit,
                              zathura_pdfdb_document_t*** documents, size_t* count, char** error);
int zathura_pdfdb_bridge_get(const char* database_url, const char* user, const char* key,
                             zathura_pdfdb_document_t** document, char** error);
int zathura_pdfdb_bridge_read_file(const char* database_url, const char* user, const char* file_path,
                                   unsigned char** data, size_t* size, char** error);
void zathura_pdfdb_bridge_free_data(unsigned char* data);
void zathura_pdfdb_bridge_free_error(char* error);

#ifdef __cplusplus
}
#endif

#endif
