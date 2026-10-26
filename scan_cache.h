#ifndef WB_SCAN_CACHE_H
#define WB_SCAN_CACHE_H
#include "recovery_core.h"
/* 0 complete (including cache hit); 1 cancelled; negative = failure. */
int wb_scan_cached(const char *image,const char *outdir,WBEvent event,WBStop stop,void *context,WBProgress *progress);
#endif
