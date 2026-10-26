#include "unit_runs.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#define UNIT (20ULL*1024*1024)
typedef struct {uint64_t owner,base,anchor;unsigned n;} R;
static int compare(const void*a,const void*b){const R*x=a,*y=b;
 if(x->owner!=y->owner)return x->owner<y->owner?-1:1;
 if(x->base!=y->base)return x->base<y->base?-1:1;
 return x->anchor<y->anchor?-1:x->anchor>y->anchor;
}
int wb_write_runs(const char *placements,const char *destination){
 FILE *in=fopen(placements,"rb");if(!in)return -1;
 char line[1024];if(!fgets(line,sizeof(line),in)){fclose(in);return -1;}
 R*rows=NULL;size_t n=0,cap=0;
 while(fgets(line,sizeof(line),in)){
  uint64_t anchor,owner,base;unsigned track,sample,matched;char status[64],reason[64];
  if(sscanf(line,"%"SCNu64",%"SCNu64",%u,%u,%"SCNu64",%u,%63[^,],%63[^\r\n]",&anchor,&owner,&track,&sample,&base,&matched,status,reason)!=8)continue;
  if(strcmp(status,"ALIGNED_UNIQUE_PREFIX"))continue;
  if(n==cap){size_t next=cap?2*cap:1024;if(next>SIZE_MAX/sizeof(*rows)){free(rows);fclose(in);return -2;}
    R*p=realloc(rows,next*sizeof(*rows));if(!p){free(rows);fclose(in);return -2;}rows=p;cap=next;}
  rows[n++]=(R){owner,base,anchor,matched};
 }
 fclose(in);qsort(rows,n,sizeof(*rows),compare);
 FILE*out=fopen(destination,"wb");if(!out){free(rows);return -3;}
 fputs("moov_offset,start_logical_base,end_logical_base,units,first_physical_anchor,status\n",out);
 for(size_t i=0;i<n;){size_t j=i+1;int duplicate=0;
  while(j<n&&rows[j].owner==rows[i].owner&&rows[j].base==rows[j-1].base+UNIT){j++;}
  if(j<n&&rows[j].owner==rows[i].owner&&rows[j].base==rows[j-1].base)duplicate=1;
  fprintf(out,"%"PRIu64",%"PRIu64",%"PRIu64",%zu,%"PRIu64",%s\n",rows[i].owner,rows[i].base,rows[j-1].base,j-i,rows[i].anchor,
    duplicate?"CANDIDATE_DUPLICATE_BASE_REVIEW":"CANDIDATE_PREFIX_CONTINUITY_ONLY");
  i=j;
 }
 fclose(out);free(rows);return (int)n;
}
/* Recording-oriented inventory. This groups independently accepted prefixes;
 * it does not certify any complete MP4 and never requires a timestamp. */
int wb_write_recording_summary(const char *placements,const char *destination){
 FILE *f=fopen(placements,"rb");if(!f)return -1;
 char line[1024];if(!fgets(line,sizeof(line),f)){fclose(f);return -1;}
 R *rows=NULL;size_t n=0,cap=0;
 while(fgets(line,sizeof(line),f)){
  uint64_t anchor,owner,base;unsigned track,sample,matched;char status[64],reason[64];
  if(sscanf(line,"%"SCNu64",%"SCNu64",%u,%u,%"SCNu64",%u,%63[^,],%63[^\r\n]",&anchor,&owner,&track,&sample,&base,&matched,status,reason)!=8)continue;
  if(strcmp(status,"ALIGNED_UNIQUE_PREFIX"))continue;
  if(n==cap){size_t next=cap?2*cap:1024;R *tmp=realloc(rows,next*sizeof(*rows));if(!tmp){free(rows);fclose(f);return -2;}rows=tmp;cap=next;}
  rows[n++]=(R){owner,base,anchor,matched};
 }
 fclose(f);qsort(rows,n,sizeof(*rows),compare);
 FILE *out=fopen(destination,"wb");if(!out){free(rows);return -3;}
 fputs("moov_offset,unique_prefix_units,first_logical_base,last_logical_base,first_physical_anchor,duplicate_base_candidates,date_status,recovery_status\n",out);
 for(size_t i=0;i<n;){size_t j=i+1;unsigned unique=1,dups=0;
  while(j<n&&rows[j].owner==rows[i].owner){if(rows[j].base==rows[j-1].base)dups++;else unique++;j++;}
  fprintf(out,"%"PRIu64",%u,%"PRIu64",%"PRIu64",%"PRIu64",%u,UNKNOWN_OPTIONAL,PREFIX_CANDIDATES_NOT_COMPLETE\n",rows[i].owner,unique,rows[i].base,rows[j-1].base,rows[i].anchor,dups);
  i=j;
 }
 fclose(out);free(rows);return 0;
}
