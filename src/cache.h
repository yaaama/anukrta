#ifndef AK_CACHE_H_
#define AK_CACHE_H_

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "sqlite3.h"
#include "util.h"

typedef struct anu_cache_ctx {
  sqlite3 *db;
  sqlite3_stmt *stmt_check_cache;
  sqlite3_stmt *stmt_upsert_file;
  sqlite3_stmt *stmt_insert_hash;
  sqlite3_stmt *stmt_get_hashes;
} anu_cache_ctx;

int cache_init_once(void);
anu_cache_ctx *cache_open_db(const char *db_path);
int cache_close_db(anu_cache_ctx *ctx);
int cache_ctx_destroy(anu_cache_ctx **ctx);

AK_DEFINE_AUTO(cache_ctx, anu_cache_ctx *, if (*ak__obj) cache_ctx_destroy(ak__obj))

/**
 * @name Database Transaction Helpers
 * @brief Transaction helpers for bulk operations.
 */
static inline int cache_begin_transaction (anu_cache_ctx *ctx) {
  if (!ctx) {
    return 0;
  }
  return sqlite3_exec(ctx->db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
}

static inline int cache_rollback_transaction (anu_cache_ctx *ctx) {

  if (!ctx) {
    return 0;
  }
  return sqlite3_exec(ctx->db, "ROLLBACK;", NULL, NULL, NULL);
}

static inline int cache_commit_transaction (anu_cache_ctx *ctx) {
  if (!ctx) {
    return 0;
  }
  return sqlite3_exec(ctx->db, "COMMIT;", NULL, NULL, NULL);
}

int cache_is_file_valid(anu_cache_ctx *ctx, ak_file *file, u64 *out_file_id, i64 *out_duration_us);

int cache_upsert_file(anu_cache_ctx *ctx, ak_file *file, uint64_t time_of_hash, uint64_t *row_id_out);

int cache_insert_hash(anu_cache_ctx *ctx, uint64_t file_id, ak_hash_entry entry);

int cache_get_hashes(anu_cache_ctx *ctx,
                     uint64_t file_id,
                     size_t max_hashes,
                     ak_hash_entry *entries_out,
                     u64 *out_count);

void cache_sync_results_maybe(anu_cache_ctx *ctx,
                              ak_config *config,
                              ak_file_v *files,
                              AK_STATUS *result_codes,
                              ak_hash_entry *entries);

#endif  // AK_CACHE_H_
