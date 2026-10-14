#ifndef AK_CACHE_H_
#define AK_CACHE_H_

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "sqlite3.h"
#include "util.h"

typedef struct ak_cache_ctx {
  sqlite3 *db;
  sqlite3_stmt *stmt_check_cache;
  sqlite3_stmt *stmt_upsert_file;
  sqlite3_stmt *stmt_insert_hash;
  sqlite3_stmt *stmt_get_hashes;
} ak_cache_ctx;

int cache_init_once(void);
ak_cache_ctx *cache_open_db(const char *db_path);
int cache_close_db(ak_cache_ctx *ctx);
int cache_ctx_destroy(ak_cache_ctx **ctx);

AK_DEFINE_AUTO(cache_ctx, ak_cache_ctx *, if (*ak__obj) cache_ctx_destroy(ak__obj))

/**
 * @name Database Transaction Helpers
 * @brief Transaction helpers for bulk operations.
 */
static inline int cache_begin_transaction (ak_cache_ctx *ctx) {
  if (!ctx) {
    return 0;
  }
  return sqlite3_exec(ctx->db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
}

static inline int cache_rollback_transaction (ak_cache_ctx *ctx) {

  if (!ctx) {
    return 0;
  }
  return sqlite3_exec(ctx->db, "ROLLBACK;", NULL, NULL, NULL);
}

static inline int cache_commit_transaction (ak_cache_ctx *ctx) {
  if (!ctx) {
    return 0;
  }
  return sqlite3_exec(ctx->db, "COMMIT;", NULL, NULL, NULL);
}

int cache_is_file_valid(ak_cache_ctx *ctx, const ak_file *file, u64 *out_file_id, i64 *out_duration_us);

int cache_upsert_file(ak_cache_ctx *ctx, const ak_file *file, u64 time_of_hash, u64 *row_id_out);

int cache_insert_hash(ak_cache_ctx *ctx, u64 file_id, ak_hash_entry entry);

int cache_get_hashes(ak_cache_ctx *ctx,
                     u64 file_id,
                     size_t max_hashes,
                     ak_hash_entry *entries_out,
                     u64 *out_count);

void cache_sync_results_maybe(ak_cache_ctx *ctx,
                              const ak_config *config,
                              const ak_file_v *files,
                              const AK_STATUS *result_codes,
                              const ak_hash_entry *entries);

#endif  // AK_CACHE_H_
