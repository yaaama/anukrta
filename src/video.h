#ifndef AK_VIDEO_H
#define AK_VIDEO_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "defs.h"
#include "explore.h"
#include "signals.h"

/**
 * Checks if the linked libavcodec was compiled with certain features.
 *
 * @return Supported features bitflag (`AK_LAV_SUPPS_`)
 */
u32 ak_libav_supports(void);

AK_STATUS ak_video_hash(ak_file *file,
                        ak_config *config,
                        ak_signals_ctx *signals,
                        ak_hash_entry *entries_out);

#endif  // AK_VIDEO_H
