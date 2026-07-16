#include "cache.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "kvec.h"
#include "log.h"
#include "mem.h"
#include "sqlite3.h"
#include "util.h"

enum DATABASE_SCHEMA_TABLE_IDX {
  DB_FILE_TABLE_IDX = 0,
  DB_HASHES_TABLE_IDX = 1,
  DB_PRAGMAS_IDX = 2
};

static const char *DATABASE_SCHEMA[] = {
  /* FILES TABLE */
  "CREATE TABLE IF NOT EXISTS files ("
  /* Auto incrementing file ID */
  "  id INTEGER PRIMARY KEY,"
  /* Path to file */
  "  path TEXT NOT NULL UNIQUE,"
  /* Mimetype */
  "  media_type INTEGER NOT NULL,"
  /* Size in bytes */
  "  size INTEGER NOT NULL,"
  /* Modification timestamp */
  "  mtime INTEGER NOT NULL,"
  /* Creation timestamp */
  "  ctime INTEGER NOT NULL,"
  /* Duration of video or 0 if stillframe */
  "  duration_us INTEGER NOT NULL,"
  "  last_hashed INTEGER NOT NULL" /* Hashing timestamp */
  ");",
  /* HASHES TABLE */
  "CREATE TABLE IF NOT EXISTS hashes ("
  /* File ID the hash belongs to */
  "  file_id INTEGER NOT NULL,"
  /* Hash value (64 bits) */
  "  hash INTEGER NOT NULL,"
  /* Frame timestamp of hash */
  "  frame_ts INTEGER NOT NULL,"
  /* Composite Primary Key */
  "  PRIMARY KEY (file_id, frame_ts),"
  /* file_id in table is referring to files.id */
  "  FOREIGN KEY (file_id) REFERENCES files (id) ON DELETE CASCADE"
  ") WITHOUT ROWID;"
  /* Create an index on the hash value */
  "CREATE INDEX IF NOT EXISTS idx_hash_lookup ON hashes(hash);",
  /* PRAGMAS */
  /* Some performance settings */
  /* Write ahead logging: Writes to a temp file for logs and then copies it over to database later */
  "PRAGMA journal_mode = WAL;"
  /* Let the operating system synchronise transactions onto disk efficiently */
  "PRAGMA synchronous = NORMAL;"
  /* We do not need to waste time on zeroing deleted entries */
  "PRAGMA secure_delete = OFF;"
  /* Enable foreign keys */
  "PRAGMA foreign_keys = ON;"
  /* Keep temp files in memory */
  "PRAGMA temp_store = MEMORY;"};

static ALWAYS_INLINE int safe_param_index (sqlite3_stmt *stmt,
                                           const char *param_name) {
  int idx = sqlite3_bind_parameter_index(stmt, param_name);
  if (idx == 0) {
    fprintf(
        stderr,
        "FATAL DATABASE ERROR: Bind parameter '%s' not found in statement!\n",
        param_name);
    assert(0 && "Missing bind parameter!");
    exit(EXIT_FAILURE);  // NOLINT (concurrency-mt-unsafe)
  }
  return idx;
}

static inline int safe_bind_i64 (sqlite3_stmt *stmt,
                                 const char *param_name,
                                 const i64 val) {
  int idx = safe_param_index(stmt, param_name);
  return sqlite3_bind_int64(stmt, idx, val);
}

static inline int safe_bind_int (sqlite3_stmt *stmt,
                                 const char *param_name,
                                 const int val) {
  int idx = safe_param_index(stmt, param_name);
  return sqlite3_bind_int(stmt, idx, val);
}

static inline int safe_bind_txt_static (sqlite3_stmt *stmt,
                                        const char *param_name,
                                        const char *val) {
  int idx = safe_param_index(stmt, param_name);
  return sqlite3_bind_text(stmt, idx, val, -1, SQLITE_STATIC);
}

void cache_sync_results_maybe (anu_cache_ctx *ctx,
                               anu_config *config,
                               anu_file_vec *files,
                               enum ANU_STATUS *result_codes,
                               u64 *hashes,
                               u64 *frame_ts) {

  if (!ANU_HAS_ANY_FLAG(config->runtime_flags, RT_CACHE) || !ctx) {
    return;
  }

  const size_t file_count = kv_size(*files);
  time_t curr_time = time(NULL);

  cache_begin_transaction(ctx);

  for (usize i = 0; i < file_count; i++) {
    if (result_codes[i] != ANU_OK) {
      continue;
    }

    /* Add/upsert file to database */
    anu_file *file = &kv_A(*files, i); /* File */
    /* Row of file in database after insertion */
    u64 row_out = 0;

    if (cache_upsert_file(ctx, file, (u64) curr_time, &row_out) != 0 ||
        !row_out) {
      log_error("Failed to upsert file %s", file->path);
      continue;
    }

    for (usize k = 0; k < config->segments; k++) {
      usize curr_seg_idx = (i * config->segments) + k;
      cache_insert_hash(ctx, row_out, hashes[curr_seg_idx],
                        frame_ts[curr_seg_idx]);
    }
  }

  cache_commit_transaction(ctx);
}

static void cache_finalize_statements (anu_cache_ctx *ctx) {
  if (ctx->stmt_check_cache) {
    sqlite3_finalize(ctx->stmt_check_cache);
    ctx->stmt_check_cache = NULL;
  }
  if (ctx->stmt_upsert_file) {
    sqlite3_finalize(ctx->stmt_upsert_file);
    ctx->stmt_upsert_file = NULL;
  }
  if (ctx->stmt_insert_hash) {
    sqlite3_finalize(ctx->stmt_insert_hash);
    ctx->stmt_insert_hash = NULL;
  }
  if (ctx->stmt_get_hashes) {
    sqlite3_finalize(ctx->stmt_get_hashes);
    ctx->stmt_get_hashes = NULL;
  }
}

/**
 * @brief Initialise SQLite library. Use only ONCE.
 *
 * @return 0 if success, anything else on failure.
 */
int cache_init_once (void) {
  int ret = sqlite3_initialize();
  if (ret != SQLITE_OK) {
    log_error("Could not initialise SQLite for caching: '%s'",
              sqlite3_errstr(ret));
    return ret;
  }
  return 0;
}

int cache_close_db (anu_cache_ctx *ctx) {
  cache_finalize_statements(ctx);

  int ret = sqlite3_close(ctx->db);
  if (ret == SQLITE_BUSY) {
    log_warn(
        "Database cannot be closed as it is still processing transactions.");
    return SQLITE_BUSY;
  }

  ctx->db = NULL;
  return 0;
}

int cache_ctx_destroy (anu_cache_ctx **ctx) {

  assert(*ctx);
  cache_close_db(*ctx);
  free(*ctx);
  *ctx = NULL;

  return 0;
}

/**
 * @brief SQLite pragmas to be used after database is successfully opened.
 */
static int init_db__pragmas (anu_cache_ctx *ctx) {

  char *err_msg = NULL;
  sqlite3 *db = ctx->db;
  /* Execute Pragmas */
  int ret =
      sqlite3_exec(db, DATABASE_SCHEMA[DB_PRAGMAS_IDX], NULL, NULL, &err_msg);

  if (ret != SQLITE_OK) {
    log_error("SQL error (Pragmas): '%s'", err_msg);
    sqlite3_free(err_msg);
  }

  return ret;
}

/**
 * @brief Create database tables (if they are not present).
 */
static int init_db__schema (anu_cache_ctx *ctx) {
  char *err_msg = NULL;

  int ret = 0;

  sqlite3 *db = ctx->db;
  cache_begin_transaction(ctx);

  /* Create files table */
  ret = sqlite3_exec(db, DATABASE_SCHEMA[DB_FILE_TABLE_IDX], NULL, NULL,
                     &err_msg);

  if (ret != SQLITE_OK) {
    log_error("Failed to create files table (%s)", err_msg);
    sqlite3_free(err_msg);
    return ret;
  }

  /* Create hashes table */
  ret = sqlite3_exec(db, DATABASE_SCHEMA[DB_HASHES_TABLE_IDX], NULL, NULL,
                     &err_msg);

  if (ret != SQLITE_OK) {
    log_error("Failed to create hashes table (%s)", err_msg);
    cache_rollback_transaction(ctx);
    sqlite3_free(err_msg);
    return ret;
  }

  cache_commit_transaction(ctx);

  return ret;
}

/**
 * @brief Checks if a file record exists in the database and has NOT had metadata changes.
 * @return 1 if valid cache exists, 0 if it needs to be hashed.
 *  Populates out_file_id if valid.
 */
int cache_is_file_valid (anu_cache_ctx *ctx,
                         anu_file *file,
                         size_t *out_file_id,
                         size_t *out_duration_us) {

  sqlite3_stmt *stmt_check_cache = ctx->stmt_check_cache;
  safe_bind_txt_static(stmt_check_cache, ":path", file->path);
  safe_bind_i64(stmt_check_cache, ":size", (sqlite3_int64) file->size);
  safe_bind_i64(stmt_check_cache, ":mtime", (sqlite3_int64) file->mtime);
  safe_bind_i64(stmt_check_cache, ":ctime", (sqlite3_int64) file->ctime);

  int ret = sqlite3_step(stmt_check_cache);

  if (ret == SQLITE_ROW) {
    *out_file_id = (uint64_t) sqlite3_column_int64(stmt_check_cache, 0);
    *out_duration_us = (uint64_t) sqlite3_column_int64(stmt_check_cache, 1);
    sqlite3_reset(stmt_check_cache);
    /* File is in DB and hasn't had any metadata changes */
    return 1;
  }

  sqlite3_reset(stmt_check_cache);
  return 0;
}

/**
 * @brief Compile SQL statements.
 */
static int init_db__prepare_statements (anu_cache_ctx *ctx) {
  int result;

  sqlite3 *db = ctx->db;

#define PREPARE(sql, stmt, expected_params)                             \
  do {                                                                  \
    result = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, \
                                &(stmt), NULL);                         \
    if (result != SQLITE_OK)                                            \
      return result;                                                    \
    assert(sqlite3_bind_parameter_count(stmt) == (expected_params) &&   \
           "SQL parameter count mismatch!");                            \
  } while (0);

  /* Check path AND metadata, if metadata does not match then we know the hash is outdated */
  PREPARE(
      "SELECT id, duration_us FROM files "
      "WHERE path = :path "
      "AND size = :size "
      "AND mtime = :mtime "
      "AND ctime = :ctime",
      ctx->stmt_check_cache, 4);

  /* INSERT OR REPLACE: If the file path exists but metadata changed, this deletes the old row.
     Because of ON DELETE CASCADE, it also automatically wipes the old hashes */
  PREPARE(
      "INSERT OR REPLACE INTO files "
      "(path, media_type, size, mtime, ctime, duration_us, last_hashed) "
      "VALUES "
      "(:path, :media_type, :size, :mtime, :ctime, :duration_us, "
      ":last_hashed)",
      ctx->stmt_upsert_file, 7);

  PREPARE(
      "INSERT INTO hashes (file_id, hash, frame_ts) "
      "VALUES (:file_id, :hash, :frame_ts)",
      ctx->stmt_insert_hash, 3);
  PREPARE(
      "SELECT hash, frame_ts FROM hashes "
      "WHERE file_id = :file_id "
      "ORDER BY frame_ts ASC",
      ctx->stmt_get_hashes, 1);

#undef PREPARE
  return SQLITE_OK;
}

/**
 * @brief Inserts or updates a file record, and returns the Row ID as an out parameter.
 *
 * @param path Path to file
 * @param media_type Media type (mime)
 * @param file_size Size in bytes
 * @param mtime Modification time in Unix time
 * @param ctime Change time in Unix time
 * @param last_hashed Last time this was hashed in Unix time
 * @param [out]out_row_id Row ID for the file we inserted/updated
 *
 * @return 0 on success, 1 on failure.
 */
int cache_upsert_file (anu_cache_ctx *ctx,
                       anu_file *file,
                       uint64_t time_of_hash,
                       uint64_t *row_id_out) {
  sqlite3_stmt *stmt_upsert_file = ctx->stmt_upsert_file;
  safe_bind_txt_static(stmt_upsert_file, ":path", file->path);
  safe_bind_int(stmt_upsert_file, ":media_type", file->media_type);
  safe_bind_i64(stmt_upsert_file, ":size", (sqlite3_int64) file->size);
  safe_bind_i64(stmt_upsert_file, ":mtime", (sqlite3_int64) file->mtime);
  safe_bind_i64(stmt_upsert_file, ":ctime", (sqlite3_int64) file->ctime);
  safe_bind_i64(stmt_upsert_file, ":duration_us",
                (sqlite3_int64) file->duration_us);
  safe_bind_i64(stmt_upsert_file, ":last_hashed", (sqlite3_int64) time_of_hash);

  int ret = sqlite3_step(stmt_upsert_file);
  if (ret != SQLITE_DONE) {
    log_error("Failed to upsert file: %s",
              sqlite3_errmsg(sqlite3_db_handle(stmt_upsert_file)));
  }
  /* Because of the schema, if this was a REPLACE, previous hashes were automatically deleted! */
  if (row_id_out) {
    *row_id_out = (ret == SQLITE_DONE)
                      ? (uint64_t) sqlite3_last_insert_rowid(
                            sqlite3_db_handle(stmt_upsert_file))
                      : 0;
  }

  sqlite3_reset(stmt_upsert_file);

  /* Return 0 if success or 1 if fail */
  return (ret == SQLITE_DONE) ? 0 : 1;
}

/**
 * @brief Inserts a hash for a frame.
 *
 * @param file_id
 * @param hash
 * @param frame_timestamp_us
 *
 * @return 0 on success, 1 on fail.
 * @note Place this in a transaction when bulk inserting.
 */
int cache_insert_hash (anu_cache_ctx *ctx,
                       uint64_t file_id,
                       uint64_t hash,
                       uint64_t frame_timestamp_us) {
  sqlite3_stmt *stmt_insert_hash = ctx->stmt_insert_hash;
  /* Storing 64-bit uint as sqlite 64-bit signed int. The bit pattern stays the same. */
  safe_bind_i64(stmt_insert_hash, ":file_id", (sqlite3_int64) file_id);
  safe_bind_i64(stmt_insert_hash, ":hash", (sqlite3_int64) hash);
  safe_bind_i64(stmt_insert_hash, ":frame_ts",
                (sqlite3_int64) frame_timestamp_us);

  int ret = sqlite3_step(stmt_insert_hash);
  if (ret != SQLITE_DONE) {
    log_error("Failed to insert hash (%s)",
              sqlite3_errmsg(sqlite3_db_handle(stmt_insert_hash)));
  }
  sqlite3_reset(stmt_insert_hash);
  /* Return 0 on success or 1 on fail */
  return (ret == SQLITE_DONE) ? 0 : -1;
}

int cache_get_hashes (anu_cache_ctx *ctx,
                      uint64_t file_id,
                      size_t max_hashes,
                      uint64_t *out_hashes,
                      uint64_t *out_timestamps,
                      size_t *out_count) {
  if (!ctx->stmt_get_hashes || !out_hashes || !out_count || !out_timestamps) {
    return -1;
  }

  sqlite3_stmt *stmt_get_hashes = ctx->stmt_get_hashes;
  safe_bind_i64(stmt_get_hashes, ":file_id", (sqlite3_int64) file_id);

  size_t count = 0;
  int ret;

  /* Increment strictly to accurately check if the DB has the EXACT segment amount we're asking for */
  while ((ret = sqlite3_step(stmt_get_hashes)) == SQLITE_ROW) {
    if (count < max_hashes) {
      out_hashes[count] = (uint64_t) sqlite3_column_int64(stmt_get_hashes, 0);
      out_timestamps[count] =
          (uint64_t) sqlite3_column_int64(stmt_get_hashes, 1);
    }
    ++count;
  }

  *out_count = count;
  sqlite3_reset(stmt_get_hashes);
  if (ret != SQLITE_DONE) {
    log_error("Database error while retrieving hashes (%s)",
              sqlite3_errstr(ret));
    return ret;
  }

  return 0;
}

/**
 * @brief Open database located at `db_path`.
 */
anu_cache_ctx *cache_open_db (const char *db_path) {

  /* TODO Add cleanup function for context
   * TODO Return pointer using return_ptr macro */
  anu_cache_ctx *ctx __free(cache_ctx) = xcalloc(1, sizeof(anu_cache_ctx));
  sqlite3 *db = NULL;

  /* Try opening database file */
  int ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);

  /* If we cannot open the database, create one: */
  if (ret == SQLITE_CANTOPEN) {
    log_info("Database does not yet exist. Creating one...");
    /* NOTE: Sqlite3_open_v2 must be closed if any error occurs during opening (even if we try to open it again) */
    sqlite3_close(db);
    db = NULL;

    /* Open database in read/write mode,
     Also specify that we do not want to create a new database if one is not already there */
    ret = sqlite3_open_v2(db_path, &db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
  }

  if (ret != SQLITE_OK) {
    log_error("Can't open database: (%s)", sqlite3_errmsg(db));
    return NULL;
  }
  ctx->db = db;
  /* Execute database pragmas
   * NOTE: This must be the first thing run after opening database */
  if (init_db__pragmas(ctx) != SQLITE_OK) {
    log_error("Could not execute pragmas");
    return NULL;
  }

  /* Initialise the database schema if not already initialised */
  if (init_db__schema(ctx) != SQLITE_OK) {
    log_error("Could not create tables: (%s)", sqlite3_errmsg(db));
    return NULL;
  };

  if (init_db__prepare_statements(ctx) != SQLITE_OK) {
    log_error("Could not prepare statements: (%s)", sqlite3_errmsg(db));
    return NULL;
  }

  assert(db != NULL);
  assert(ret == SQLITE_OK);

  log_debug("Opened database '%s'", db_path);

  return_ptr(ctx);
}
