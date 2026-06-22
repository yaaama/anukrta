#include "cache.h"

#include <assert.h>
#include <stdint.h>

#include "log.h"
#include "sqlite3.h"

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
  const char *pragmas =
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
      "PRAGMA temp_store = MEMORY;";

  sqlite3 *db = ctx->db;
  /* Execute Pragmas */
  int ret = sqlite3_exec(db, pragmas, NULL, NULL, &err_msg);
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

  const char *files_table_schema =
      /* FILES TABLE */
      "CREATE TABLE IF NOT EXISTS files ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT," /* Auto incrementing file ID */
      "  path TEXT NOT NULL UNIQUE,"            /* Path to file */
      "  media_type TEXT NOT NULL,"             /* Mimetype */
      "  file_size INTEGER NOT NULL,"           /* Size in bytes */
      "  mtime INTEGER NOT NULL,"               /* Modification timestamp */
      "  ctime INTEGER NOT NULL,"               /* Creation timestamp */
      "  duration_us INTEGER NOT NULL," /* Duration of video or 0 if stillframe */
      "  last_hashed INTEGER NOT NULL," /* Hashing timestamp */
      "  UNIQUE(path)"
      ");"
      "CREATE INDEX IF NOT EXISTS idx_file_path ON files(path);" /* Create an index on the path value */
      ;

  const char *hashes_table_schema =
      /* HASHES TABLE */
      "CREATE TABLE IF NOT EXISTS hashes ("
      "  id INTEGER PRIMARY KEY AUTOINCREMENT," /* Hash ID */
      "  file_id INTEGER NOT NULL,"            /* File ID the hash belongs to */
      "  hash INTEGER NOT NULL,"               /* Hash value (64 bits) */
      "  frame_timestamp_us INTEGER NOT NULL," /* Frame timestamp of hash */
      "  FOREIGN KEY (file_id) REFERENCES files (id) ON DELETE CASCADE" /* file_id in table is referring to files.id */
      ");"
      "CREATE INDEX IF NOT EXISTS idx_hash_lookup ON hashes(hash);" /* Create an index on the hash value */
      ;

  /* Create files table */
  ret = sqlite3_exec(db, files_table_schema, NULL, NULL, &err_msg);

  if (ret != SQLITE_OK) {
    log_error("Failed to create files table: '%s'", err_msg);
    sqlite3_free(err_msg);
    return ret;
  }

  /* Create hashes table */
  ret = sqlite3_exec(db, hashes_table_schema, NULL, NULL, &err_msg);

  if (ret != SQLITE_OK) {
    log_error("Failed to create hashes table '%s'", err_msg);

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
  sqlite3_bind_text(stmt_check_cache, 1, file->path, -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt_check_cache, 2, (sqlite3_int64) file->size);
  sqlite3_bind_int64(stmt_check_cache, 3, (sqlite3_int64) file->mtime);
  sqlite3_bind_int64(stmt_check_cache, 4, (sqlite3_int64) file->ctime);

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

#define PREPARE(sql, stmt)                                                     \
  result = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, &(stmt), \
                              NULL);                                           \
  if (result != SQLITE_OK)                                                     \
  return result

  /* Check path AND metadata, if metadata does not match then we know the hash is outdated */
  PREPARE(
      "SELECT id, duration_us FROM files WHERE path = ? AND file_size = ? AND "
      "mtime = ? AND "
      "ctime = ?",
      ctx->stmt_check_cache);

  /* INSERT OR REPLACE: If the file path exists but metadata changed, this deletes the old row.
     Because of ON DELETE CASCADE, it also automatically wipes the old hashes */
  PREPARE(
      "INSERT OR REPLACE INTO files (path, media_type, file_size, mtime, "
      "ctime, duration_us, last_hashed) VALUES (?, ?, ?, ?, ?, ?, ?)",
      ctx->stmt_upsert_file);

  PREPARE(
      "INSERT INTO hashes (file_id, hash, frame_timestamp_us) VALUES (?, ?, ?)",
      ctx->stmt_insert_hash);
  PREPARE(
      "SELECT hash, frame_timestamp_us FROM hashes WHERE file_id = ? ORDER BY "
      "frame_timestamp_us ASC",
      ctx->stmt_get_hashes);

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
                       const char *restrict media_type,
                       anu_file *file,
                       uint64_t time_of_hash,
                       uint64_t *row_id_out) {
  sqlite3_stmt *stmt_upsert_file = ctx->stmt_upsert_file;
  sqlite3_bind_text(stmt_upsert_file, 1, file->path, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt_upsert_file, 2, media_type, -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt_upsert_file, 3, (sqlite3_int64) file->size);
  sqlite3_bind_int64(stmt_upsert_file, 4, (sqlite3_int64) file->mtime);
  sqlite3_bind_int64(stmt_upsert_file, 5, (sqlite3_int64) file->ctime);
  sqlite3_bind_int64(stmt_upsert_file, 6, (sqlite3_int64) file->duration_us);
  sqlite3_bind_int64(stmt_upsert_file, 7, (sqlite3_int64) time_of_hash);

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
  sqlite3_bind_int64(stmt_insert_hash, 1, (sqlite3_int64) file_id);

  /* Storing 64-bit uint as sqlite 64-bit signed int. The bit pattern stays the same. */
  sqlite3_bind_int64(stmt_insert_hash, 2, (sqlite3_int64) hash);
  sqlite3_bind_int64(stmt_insert_hash, 3, (sqlite3_int64) frame_timestamp_us);

  int ret = sqlite3_step(stmt_insert_hash);
  if (ret != SQLITE_DONE) {
    log_error("Failed to insert hash: %s",
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
  sqlite3_bind_int64(stmt_get_hashes, 1, (sqlite3_int64) file_id);

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
    log_error("Database error while retrieving hashes: '%s'",
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
  anu_cache_ctx *ctx __free(cache_ctx) = calloc(1, sizeof(anu_cache_ctx));
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
    log_error("Can't open database: '%s'", sqlite3_errmsg(db));
    return NULL;
  }
  ctx->db = db;
  /* Execute database pragmas
   * NOTE: This must be the first thing run after opening database */
  if (init_db__pragmas(ctx) != SQLITE_OK) {
    log_error("Could not execute pragmas: '%s'", sqlite3_errmsg(db));
    return NULL;
  }

  /* Initialise the database schema if not already initialised */
  if (init_db__schema(ctx) != SQLITE_OK) {
    log_error("Could not create tables: '%s'", sqlite3_errmsg(db));
    return NULL;
  };

  if (init_db__prepare_statements(ctx) != SQLITE_OK) {
    log_error("Could not prepare statements: '%s'", sqlite3_errmsg(db));
    return NULL;
  }

  assert(db != NULL);
  assert(ret == SQLITE_OK);

  log_debug("Opened database '%s'", db_path);

  return_ptr(ctx);
}
