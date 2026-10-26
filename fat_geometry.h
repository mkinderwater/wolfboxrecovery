#ifndef WB_FAT_GEOMETRY_H
#define WB_FAT_GEOMETRY_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef struct { uint64_t candidates; uint64_t accepted; } WBGeometryStats;
/* Examine the sector-aligned bytes of a block already read by the structural scanner.
   Outputs geometry candidates only: never makes allocation claims. */
void wb_geometry_block(const unsigned char *p,size_t bytes,uint64_t image_off,uint64_t image_size,FILE *out,WBGeometryStats *stats);
#endif
