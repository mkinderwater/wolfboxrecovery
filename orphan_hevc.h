#ifndef WB_ORPHAN_HEVC_H
#define WB_ORPHAN_HEVC_H
#include <stdint.h>
typedef struct {uint64_t samples,valid_samples,bad_samples,bytes,random_access,parameter_sets,streams;} WBOrphanStats;
int wb_orphan_extract(const char *image,const char *outdir,WBOrphanStats *stats,int (*stop)(void*),void *ctx);
#endif
