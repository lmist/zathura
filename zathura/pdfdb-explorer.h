/* SPDX-License-Identifier: Zlib */

#ifndef ZATHURA_PDFDB_EXPLORER_H
#define ZATHURA_PDFDB_EXPLORER_H

#include <stdbool.h>

#include <gtk/gtk.h>

#include "pdfdb.h"
#include "types.h"

typedef struct zathura_pdfdb_explorer_s zathura_pdfdb_explorer_t;

zathura_pdfdb_explorer_t* zathura_pdfdb_explorer_new(zathura_t* zathura);
void zathura_pdfdb_explorer_free(zathura_pdfdb_explorer_t* explorer);
GtkWidget* zathura_pdfdb_explorer_get_widget(zathura_pdfdb_explorer_t* explorer);
bool zathura_pdfdb_explorer_toggle(zathura_pdfdb_explorer_t* explorer);
bool zathura_pdfdb_explorer_hide(zathura_pdfdb_explorer_t* explorer);
bool zathura_pdfdb_explorer_is_visible(zathura_pdfdb_explorer_t* explorer);

#endif
