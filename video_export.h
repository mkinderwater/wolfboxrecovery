#ifndef WB_VIDEO_EXPORT_H
#define WB_VIDEO_EXPORT_H
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
int wb_hevc_sample(FILE *,const unsigned char *,size_t,unsigned);
int wb_export_preview(const char *,const char *,const char *,uint64_t);
#endif
