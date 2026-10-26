#ifndef WB_AUDIO_MAPPING_H
#define WB_AUDIO_MAPPING_H
#include <stdint.h>
#include <stddef.h>
typedef struct WB_AudioSession WB_AudioSession;
/* Audio mapping accepts the original moov and samples encountered by the MP4
 * writer. No second source-image traversal. Unsupported audio is optional. */
WB_AudioSession *wb_audio_begin(const unsigned char *moov,size_t moov_len,
 const char *report_path,uint64_t owner);
void wb_audio_observe(WB_AudioSession *,uint64_t anchor,uint64_t logical_payload,
 uint64_t physical_payload,uint32_t payload_size);
/* Returns exact source AAC sample index only when logical offset and byte length agree. */
int wb_audio_match(WB_AudioSession *,uint64_t logical_payload,uint32_t size,uint32_t *index);
/* Original MP4 audio entry (including esds), timescale and constant duration.
 * Caller owns *entry. Unsupported sample descriptions return false. */
int wb_audio_description(WB_AudioSession *,unsigned char **entry,size_t *length,
 uint32_t *timescale,uint32_t *delta);
void wb_audio_end(WB_AudioSession *,int completed);
#endif
