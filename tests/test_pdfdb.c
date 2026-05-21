/* SPDX-License-Identifier: Zlib */

#include "pdfdb.h"

#include <girara/datastructures.h>

static zathura_pdfdb_document_t* document_new(const char* slug, const char* title, const char* filename,
                                              const char* file_path, const char* source_url) {
  zathura_pdfdb_document_t* document = g_new0(zathura_pdfdb_document_t, 1);
  document->slug                     = g_strdup(slug);
  document->title                    = g_strdup(title);
  document->filename                 = g_strdup(filename);
  document->file_path                = g_strdup(file_path);
  document->source_url               = g_strdup(source_url);
  return document;
}

static void test_pdfdb_uri_parse(void) {
  g_autofree char* key = NULL;
  g_assert_true(zathura_pdfdb_uri_parse("pdfdb://doc/dbos-paper", &key));
  g_assert_cmpstr(key, ==, "dbos-paper");

  g_clear_pointer(&key, g_free);
  g_assert_true(zathura_pdfdb_uri_parse("pdfdb://doc/space%20title", &key));
  g_assert_cmpstr(key, ==, "space title");

  g_assert_false(zathura_pdfdb_uri_parse(NULL, NULL));
  g_assert_false(zathura_pdfdb_uri_parse("pdfdb://doc/", NULL));
  g_assert_false(zathura_pdfdb_uri_parse("file:///tmp/document.pdf", NULL));
}

static void test_pdfdb_document_uri(void) {
  zathura_pdfdb_document_t document = {
      .slug = "space title",
  };
  g_autofree char* uri = zathura_pdfdb_document_uri(&document);
  g_assert_cmpstr(uri, ==, "pdfdb://doc/space%20title");
}

static void test_pdfdb_filter_documents(void) {
  girara_list_t* documents = girara_list_new();
  g_assert_nonnull(documents);
  girara_list_set_free_function(documents, (girara_free_function_t)zathura_pdfdb_document_free);
  girara_list_append(documents,
                     document_new("dbos-paper", "DBOS paper", "dbos.pdf", "/home/pdfdb/dbos.pdf",
                                  "https://example.test/dbos"));
  girara_list_append(documents,
                     document_new("zathura-manual", "Zathura Manual", "manual.pdf", "/home/pdfdb/manual.pdf",
                                  "https://example.test/manual"));

  girara_list_t* filtered = zathura_pdfdb_filter_documents(documents, "manual", 10);
  g_assert_nonnull(filtered);
  g_assert_cmpuint(girara_list_size(filtered), ==, 1);
  zathura_pdfdb_document_t* first = girara_list_nth(filtered, 0);
  g_assert_nonnull(first);
  g_assert_cmpstr(first->slug, ==, "zathura-manual");
  girara_list_free(filtered);

  filtered = zathura_pdfdb_filter_documents(documents, "PDFDB", 10);
  g_assert_nonnull(filtered);
  g_assert_cmpuint(girara_list_size(filtered), ==, 2);
  girara_list_free(filtered);

  filtered = zathura_pdfdb_filter_documents(documents, "", 1);
  g_assert_nonnull(filtered);
  g_assert_cmpuint(girara_list_size(filtered), ==, 1);
  girara_list_free(filtered);

  girara_list_free(documents);
}

int main(int argc, char* argv[]) {
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/pdfdb/uri_parse", test_pdfdb_uri_parse);
  g_test_add_func("/pdfdb/document_uri", test_pdfdb_document_uri);
  g_test_add_func("/pdfdb/filter_documents", test_pdfdb_filter_documents);
  return g_test_run();
}
