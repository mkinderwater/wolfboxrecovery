#ifndef TIMESTAMP_CORE_H
#define TIMESTAMP_CORE_H
#include <stdint.h>
#include <stddef.h>
typedef struct {uint64_t scanned, size, moovs, tbgps, wrappers, date_fields; int cancelled;} WBProgress;
typedef void (*WBEvent)(void *ctx, const char *type, uint64_t offset, const char *detail);
typedef int (*WBStop)(void *ctx);
int wb_scan(const char *image_path,const char *outdir,WBEvent ev,WBStop stop,void *ctx,WBProgress *p);
#endif
