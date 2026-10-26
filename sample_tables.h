#ifndef WB_SAMPLE_TABLES_H
#define WB_SAMPLE_TABLES_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
/* Parses independently discovered moov atoms. No FAT and no case-specific inputs. */
int wb_index_moov(const unsigned char *box, size_t length, uint64_t physical_offset, FILE *report);
#endif
