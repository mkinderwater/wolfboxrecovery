#ifndef WB_MP4_EXPORT_H
#define WB_MP4_EXPORT_H
#include <stdint.h>
uint64_t wb_run_canonical_anchor(const char*,uint64_t);
int wb_export_mp4_cancellable(const char*,const char*,const char*,uint64_t,int (*)(void*),void*);
int wb_export_mp4(const char*,const char*,const char*,uint64_t);
#endif
