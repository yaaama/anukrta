#ifndef AK_VIDEO_H
#define AK_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "signals.h"

AK_STATUS ak_video_hash(ak_file *file,
                        ak_config *config,
                        ak_signals_ctx *signals,
                        ak_hash_entry *entries_out);

#endif  // AK_VIDEO_H
