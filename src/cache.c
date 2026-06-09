#include "cache.h"

#include <assert.h>
#include <stdint.h>

#include "log.h"
#include "sqlite3.h"

static sqlite3_stmt *stmt_check_cache = NULL;
static sqlite3_stmt *stmt_upsert_file = NULL;
static sqlite3_stmt *stmt_insert_hash = NULL;
static sqlite3_stmt *stmt_get_hashes = NULL;

static void cache_finalize_statements (void) {
  if (stmt_check_cache) {
    sqlite3_finalize(stmt_check_cache);
  }
  if (stmt_upsert_file) {
    sqlite3_finalize(stmt_upsert_file);
  }
  if (stmt_insert_hash) {
    sqlite3_finalize(stmt_insert_hash);
  }
  if (stmt_get_hashes) {
    sqlite3_finalize(stmt_get_hashes);
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

/**
 * @brief Transaction helpers for bulk inserting video hashes
 */
int cache_begin_transaction (sqlite3 *db) {
  return sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
}

int cache_commit_transaction (sqlite3 *db) {
  return sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
}

int cache_rollback_transaction (sqlite3 *db) {
  return sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
}

int cache_close_db (sqlite3 **db) {
  assert(db);
  cache_finalize_statements();
  int ret = sqlite3_close(*db);
  if (ret == SQLITE_BUSY) {
    log_warn(
        "Database cannot be closed as it is still processing transactions.");
    return SQLITE_BUSY;
  }

  *db = NULL;
  return 0;
}

/**
 * @brief SQLite pragmas to be used after database is successfully open
 */
static int init_db__pragmas (sqlite3 *db) {

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

  /* Execute Pragmas */
  int ret = sqlite3_exec(db, pragmas, NULL, NULL, &err_msg);
  if (ret != SQLITE_OK) {
    log_error("SQL error (Pragmas): '%s'", err_msg);
    sqlite3_free(err_msg);
  }

  return ret;
}

/**
 * @brief Create database tables (if they are not present)
 */
static int init_db__schema (sqlite3 *db) {
  char *err_msg = NULL;

  int ret = 0;

  cache_begin_transaction(db);

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

  ret = sqlite3_exec(db, files_table_schema, NULL, NULL, &err_msg);

  if (ret != SQLITE_OK) {
    log_error("Failed to create files table: '%s'", err_msg);
    sqlite3_free(err_msg);
    return ret;
  }

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

  /* Create tables if not already created */
  ret = sqlite3_exec(db, hashes_table_schema, NULL, NULL, &err_msg);

  if (ret != SQLITE_OK) {
    log_error("Failed to create hashes table '%s'", err_msg);

    cache_rollback_transaction(db);
    sqlite3_free(err_msg);
    return ret;
  }

  cache_commit_transaction(db);

  return ret;
}

/**
 * @brief Checks if a file record exists in the database and has NOT had metadata changes.
 * @return 1 if valid cache exists, 0 if it needs to be hashed.
 *  Populates out_file_id if valid.
 */
int cache_is_file_valid (const char *path,
                         uint64_t file_size,
                         uint64_t mtime,
                         uint64_t ctime,
                         uint64_t *out_file_id) {
  sqlite3_bind_text(stmt_check_cache, 1, path, -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt_check_cache, 2, (sqlite3_int64) file_size);
  sqlite3_bind_int64(stmt_check_cache, 3, (sqlite3_int64) mtime);
  sqlite3_bind_int64(stmt_check_cache, 4, (sqlite3_int64) ctime);

  int ret = sqlite3_step(stmt_check_cache);

  if (ret == SQLITE_ROW) {
    if (out_file_id) {
      *out_file_id = (uint64_t) sqlite3_column_int64(stmt_check_cache, 0);
    }
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
static int init_db__prepare_statements (sqlite3 *db) {
  int result;

#define PREPARE(sql, stmt)                                                     \
  result = sqlite3_prepare_v3(db, sql, -1, SQLITE_PREPARE_PERSISTENT, &(stmt), \
                              NULL);                                           \
  if (result != SQLITE_OK)                                                     \
  return result

  /* PREPARE("BEGIN", stmt_begin);
   * PREPARE("COMMIT", stmt_commit);
   * PREPARE("ROLLBACK", stmt_rollback); */

  /* Check path AND metadata, if metadata does not match then we know the hash is outdated */
  PREPARE(
      "SELECT id FROM files WHERE path = ? AND file_size = ? AND mtime = ? AND "
      "ctime = ?",
      stmt_check_cache);

  /* INSERT OR REPLACE: If the file path exists but metadata changed, this deletes the old row.
     Because of ON DELETE CASCADE, it also automatically wipes the old hashes */
  PREPARE(
      "INSERT OR REPLACE INTO files (path, media_type, file_size, mtime, "
      "ctime, duration_us, last_hashed) VALUES (?, ?, ?, ?, ?, ?, ?)",
      stmt_upsert_file);

  PREPARE(
      "INSERT INTO hashes (file_id, hash, frame_timestamp_us) VALUES (?, ?, ?)",
      stmt_insert_hash);
  PREPARE(
      "SELECT hash, frame_timestamp_us FROM hashes WHERE file_id = ? ORDER BY "
      "frame_timestamp_us ASC",
      stmt_get_hashes);

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
int cache_upsert_file (const char *restrict path,
                       const char *restrict media_type,
                       uint64_t file_size,
                       uint64_t mtime,
                       uint64_t ctime,
                       uint64_t duration,
                       uint64_t last_hashed,
                       uint64_t *out_row_id) {
  sqlite3_bind_text(stmt_upsert_file, 1, path, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt_upsert_file, 2, media_type, -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt_upsert_file, 3, (sqlite3_int64) file_size);
  sqlite3_bind_int64(stmt_upsert_file, 4, (sqlite3_int64) mtime);
  sqlite3_bind_int64(stmt_upsert_file, 5, (sqlite3_int64) ctime);
  sqlite3_bind_int64(stmt_upsert_file, 6, (sqlite3_int64) duration);
  sqlite3_bind_int64(stmt_upsert_file, 7, (sqlite3_int64) last_hashed);

  int ret = sqlite3_step(stmt_upsert_file);
  if (ret != SQLITE_DONE) {
    log_error("Failed to upsert file: %s",
              sqlite3_errmsg(sqlite3_db_handle(stmt_upsert_file)));
  }
  /* Because of the schema, if this was a REPLACE, previous hashes were automatically deleted! */
  if (out_row_id) {
    *out_row_id = (ret == SQLITE_DONE)
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
int cache_insert_hash (uint64_t file_id,
                       uint64_t hash,
                       uint64_t frame_timestamp_us) {
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

int cache_get_hashes (uint64_t file_id,
                      uint64_t *out_hashes,
                      size_t max_hashes,
                      size_t *out_count) {
  if (!stmt_get_hashes || !out_hashes || !out_count) {
    return -1;
  }

  sqlite3_bind_int64(stmt_get_hashes, 1, (sqlite3_int64) file_id);

  size_t count = 0;
  int ret;

  /* Increment strictly to accurately check if the DB has the EXACT segment amount we're asking for */
  while ((ret = sqlite3_step(stmt_get_hashes)) == SQLITE_ROW) {
    if (count < max_hashes) {
      out_hashes[count] = (uint64_t) sqlite3_column_int64(stmt_get_hashes, 0);
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
sqlite3 *cache_open_db (const char *db_path) {

  sqlite3 *db = NULL;

  /* Try opening database file */
  int ret = sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READWRITE, NULL);

  /* If we cannot open the database, create one: */
  if (ret == SQLITE_CANTOPEN) {
    log_info("Database does not yet exist. Creating one...");
    /* Open database in read/write mode,
     Also specify that we do not want to create a new database if one is not already there */
    ret = sqlite3_open_v2(db_path, &db,
                          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
  }

  if (ret != SQLITE_OK) {
    log_error("Can't open database: '%s'", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  /* Execute database pragmas
   * NOTE: This must be the first thing run after opening database */
  if (init_db__pragmas(db) != SQLITE_OK) {
    log_error("Could not execute pragmas: '%s'", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  /* Initialise the database schema if not already initialised */
  if (init_db__schema(db) != SQLITE_OK) {
    log_error("Could not create tables: '%s'", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  };

  if (init_db__prepare_statements(db) != SQLITE_OK) {
    log_error("Could not prepare statements: '%s'", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  assert(db != NULL);
  assert(ret == SQLITE_OK);

  log_debug("Opened database '%s'", db_path);

  return db;
}
