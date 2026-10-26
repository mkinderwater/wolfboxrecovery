#ifndef WB_JPEG_CARVE_H
#define WB_JPEG_CARVE_H
#include <stdint.h>
#include <stddef.h>
typedef struct WBJpeg WBJpeg;
WBJpeg *wb_jpeg_new(const char *outdir);
void wb_jpeg_anchor(WBJpeg *,uint64_t);
void wb_jpeg_feed(WBJpeg *,const unsigned char *,size_t,uint64_t);
void wb_jpeg_finish(WBJpeg *,uint64_t *recovered,uint64_t *rejected);
#endif
