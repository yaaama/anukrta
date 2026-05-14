#ifndef ANU_CACHE_H_
#define ANU_CACHE_H_

#include <stddef.h>
#include <stdint.h>

#include "sqlite3.h"

int cache_init_once(void);
sqlite3 *cache_open_db(const char *db_path);
int cache_close_db(sqlite3 **db);

int cache_begin_transaction(sqlite3 *db);
int cache_rollback_transaction(sqlite3 *db);
int cache_commit_transaction(sqlite3 *db);

int cache_is_file_valid(const char *path,
                        uint64_t file_size,
                        uint64_t mtime,
                        uint64_t ctime,
                        uint64_t *out_file_id);

int cache_upsert_file(const char *restrict path,
                      const char *restrict media_type,
                      uint64_t file_size,
                      uint64_t mtime,
                      uint64_t ctime,
                      uint64_t duration,
                      uint64_t last_hashed,
                      uint64_t *out_row_id);

int cache_insert_hash(uint64_t file_id,
                      uint64_t hash,
                      uint64_t frame_timestamp_us);

int cache_get_hashes(uint64_t file_id,
                     uint64_t *out_hashes,
                     size_t max_hashes,
                     size_t *out_count);
#endif  // ANU_CACHE_H_
