#ifndef WB_OWNERSHIP_H
#define WB_OWNERSHIP_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef struct WBIndex WBIndex;
WBIndex *wb_owner_new(void);
void wb_owner_free(WBIndex*);
int wb_owner_add_moov(WBIndex *,const unsigned char *moov,size_t length,uint64_t physical);
int wb_owner_add_anchor(WBIndex *,uint64_t offset,uint32_t size);
/* Feed a sequential scan block after anchor discovery. No extra image reads. */
void wb_owner_stage1_feed(WBIndex*,const unsigned char*,size_t,uint64_t,size_t);
size_t wb_owner_stage1_ready(const WBIndex*);
void wb_owner_stage1_usage(const WBIndex*,size_t *indexed,size_t *fallback);
/* Runs a second read-only phase over the source image. Returns mapped anchor count or negative on error. */
typedef void (*WBMatchProgress)(void *ctx, size_t done, size_t total, size_t accepted);
int wb_owner_resolve_progress(WBIndex*,const char *image_path,FILE *out,FILE *placements,FILE *diagnostics,FILE *samples,FILE *candidates,FILE *orphan,int (*stop)(void*),void *ctx,WBMatchProgress progress);
int wb_owner_resolve(WBIndex*,const char *image_path,FILE *out,FILE *placements,FILE *diagnostics,FILE *samples,FILE *candidates,FILE *orphan,int (*stop)(void*),void *ctx);
#endif
