/* SPDX-License-Identifier: Zlib */

#include "pdfdb-bridge.h"

#include <Client.h>
#include <ClientConfig.h>
#include <InvocationResponse.hpp>
#include <Parameter.hpp>
#include <ParameterSet.hpp>
#include <Procedure.hpp>
#include <Row.hpp>
#include <Table.h>
#include <TableIterator.h>
#include <WireType.h>

#include <glib.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct VoltTarget {
  std::string host = "localhost";
  unsigned short port = 21212;
};

std::mutex client_mutex;

std::string quote_sql(const char* value) {
  std::string out = "'";
  if (value != nullptr) {
    for (const char* p = value; *p != '\0'; ++p) {
      if (*p == '\'') {
        out += "''";
      } else {
        out += *p;
      }
    }
  }
  out += "'";
  return out;
}

VoltTarget parse_url(const char* url) {
  VoltTarget target;
  std::string text = (url != nullptr && *url != '\0') ? url : "voltdb://localhost:21212";
  const std::string prefix = "voltdb://";
  if (text.rfind(prefix, 0) == 0) {
    text = text.substr(prefix.size());
  }
  const size_t slash = text.find('/');
  if (slash != std::string::npos) {
    text = text.substr(0, slash);
  }
  const size_t colon = text.rfind(':');
  if (colon != std::string::npos) {
    target.host = text.substr(0, colon);
    const int port = std::atoi(text.substr(colon + 1).c_str());
    if (port > 0 && port <= 65535) {
      target.port = static_cast<unsigned short>(port);
    }
  } else if (!text.empty()) {
    target.host = text;
  }
  if (target.host.empty()) {
    target.host = "localhost";
  }
  return target;
}

voltdb::InvocationResponse invoke_sql(const char* database_url, const std::string& sql) {
  std::lock_guard<std::mutex> lock(client_mutex);
  VoltTarget target = parse_url(database_url);
  voltdb::ClientConfig config("", "");
  voltdb::Client client = voltdb::Client::create(config);
  client.createConnection(target.host, target.port);

  std::vector<voltdb::Parameter> parameter_types(1);
  parameter_types[0] = voltdb::Parameter(voltdb::WIRE_TYPE_STRING);
  voltdb::Procedure procedure("@AdHoc", parameter_types);
  procedure.params()->addString(sql);
  voltdb::InvocationResponse response = client.invoke(procedure);
  client.close();
  if (response.failure()) {
    throw std::runtime_error(response.toString());
  }
  return response;
}

std::string row_string(voltdb::Row& row, const char* name) {
  try {
    return row.getString(name);
  } catch (...) {
    return "";
  }
}

int64_t row_i64(voltdb::Row& row, const char* name) {
  try {
    return row.getInt64(name);
  } catch (...) {
    return 0;
  }
}

zathura_pdfdb_document_t* doc_from_row(voltdb::Row& row) {
  auto* doc = g_new0(zathura_pdfdb_document_t, 1);
  doc->id = g_strdup(row_string(row, "id").c_str());
  doc->slug = g_strdup(row_string(row, "slug").c_str());
  doc->title = g_strdup(row_string(row, "title").c_str());
  doc->filename = g_strdup(row_string(row, "filename").c_str());
  doc->source_url = g_strdup(row_string(row, "source_url").c_str());
  doc->sha256 = g_strdup(row_string(row, "sha256").c_str());
  doc->file_path = g_strdup(row_string(row, "file_path").c_str());
  doc->size_bytes = row_i64(row, "size_bytes");
  doc->page_count = static_cast<int>(row_i64(row, "page_count"));
  doc->created_at_us = row_i64(row, "created_at_us");
  doc->updated_at_us = row_i64(row, "updated_at_us");
  return doc;
}

void set_error(char** error, const std::string& message) {
  if (error != nullptr) {
    *error = g_strdup(message.c_str());
  }
}

std::string select_columns() {
  return "id, slug, title, filename, source_url, sha256, size_bytes, page_count, file_path, created_at_us, updated_at_us";
}

} // namespace

extern "C" int zathura_pdfdb_bridge_list(const char* database_url, const char* user, int limit,
                                         zathura_pdfdb_document_t*** documents, size_t* count, char** error) {
  if (documents == nullptr || count == nullptr) {
    set_error(error, "invalid output pointer");
    return -1;
  }
  *documents = nullptr;
  *count = 0;
  if (limit <= 0) {
    limit = 200;
  }

  try {
    std::ostringstream sql;
    sql << "SELECT " << select_columns() << " FROM dbos_pdf_document WHERE user_name = " << quote_sql(user)
        << " ORDER BY created_at_us ASC LIMIT " << limit;
    voltdb::InvocationResponse response = invoke_sql(database_url, sql.str());
    std::vector<voltdb::Table> results = response.results();
    if (results.empty()) {
      return 0;
    }
    voltdb::TableIterator it = results[0].iterator();
    std::vector<zathura_pdfdb_document_t*> out;
    while (it.hasNext()) {
      voltdb::Row row = it.next();
      out.push_back(doc_from_row(row));
    }
    *count = out.size();
    *documents = g_new0(zathura_pdfdb_document_t*, out.size());
    for (size_t i = 0; i != out.size(); ++i) {
      (*documents)[i] = out[i];
    }
    return 0;
  } catch (const std::exception& ex) {
    set_error(error, ex.what());
    return -1;
  }
}

extern "C" int zathura_pdfdb_bridge_get(const char* database_url, const char* user, const char* key,
                                        zathura_pdfdb_document_t** document, char** error) {
  if (document == nullptr || key == nullptr || *key == '\0') {
    set_error(error, "invalid document key");
    return -1;
  }
  *document = nullptr;
  try {
    std::ostringstream sql;
    sql << "SELECT " << select_columns() << " FROM dbos_pdf_document WHERE user_name = " << quote_sql(user)
        << " AND (id = " << quote_sql(key) << " OR slug = " << quote_sql(key) << " OR filename = " << quote_sql(key)
        << " OR file_path = " << quote_sql(key) << ") ORDER BY created_at_us ASC LIMIT 1";
    voltdb::InvocationResponse response = invoke_sql(database_url, sql.str());
    std::vector<voltdb::Table> results = response.results();
    if (results.empty() || results[0].rowCount() == 0) {
      set_error(error, "document not found");
      return -1;
    }
    voltdb::TableIterator it = results[0].iterator();
    voltdb::Row row = it.next();
    *document = doc_from_row(row);
    return 0;
  } catch (const std::exception& ex) {
    set_error(error, ex.what());
    return -1;
  }
}

extern "C" int zathura_pdfdb_bridge_read_file(const char* database_url, const char* user, const char* file_path,
                                              unsigned char** data, size_t* size, char** error) {
  if (data == nullptr || size == nullptr || file_path == nullptr || *file_path == '\0') {
    set_error(error, "invalid file path");
    return -1;
  }
  *data = nullptr;
  *size = 0;
  try {
    std::ostringstream sql;
    sql << "SELECT block_number, bytes FROM dbos_file WHERE p_key = 0 AND user_name = " << quote_sql(user)
        << " AND file_name = " << quote_sql(file_path) << " ORDER BY block_number ASC";
    voltdb::InvocationResponse response = invoke_sql(database_url, sql.str());
    std::vector<voltdb::Table> results = response.results();
    if (results.empty()) {
      set_error(error, "file has no blocks");
      return -1;
    }
    std::vector<unsigned char> out;
    voltdb::TableIterator it = results[0].iterator();
    while (it.hasNext()) {
      voltdb::Row row = it.next();
      int32_t len = 0;
      std::vector<uint8_t> block(1024 * 1024);
      if (row.getVarbinary("bytes", static_cast<int32_t>(block.size()), block.data(), &len) == false || len < 0) {
        set_error(error, "failed to read DBOS block bytes");
        return -1;
      }
      out.insert(out.end(), block.begin(), block.begin() + len);
    }
    *size = out.size();
    *data = static_cast<unsigned char*>(g_malloc(out.size()));
    if (*data == nullptr && !out.empty()) {
      set_error(error, "out of memory");
      return -1;
    }
    if (!out.empty()) {
      std::memcpy(*data, out.data(), out.size());
    }
    return 0;
  } catch (const std::exception& ex) {
    set_error(error, ex.what());
    return -1;
  }
}

extern "C" void zathura_pdfdb_bridge_free_data(unsigned char* data) {
  g_free(data);
}

extern "C" void zathura_pdfdb_bridge_free_error(char* error) {
  g_free(error);
}
