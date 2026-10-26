#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "orphan_hevc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#ifdef _WIN32
#include <direct.h>
#define SEEK64 _fseeki64
#define SEP '\\'
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define SEEK64 fseeko
#define SEP '/'
#define MKDIR(p) mkdir(p,0777)
#endif
#define MAX_SAMPLE (16u*1024u*1024u)
static uint32_t be32(const unsigned char*b){return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];}
/* A structural candidate is not a successfully decoded access unit. */
int wb_orphan_extract(const char *image,const char *outdir,WBOrphanStats *st,int(*stop)(void*),void *ctx){
 char name[4096],dir[4096],line[4096];memset(st,0,sizeof(*st));
 snprintf(name,sizeof(name),"%s%cphysical_video_samples.csv",outdir,SEP);FILE*src=fopen(name,"rb");if(!src)return -1;
 FILE*raw=fopen(image,"rb");if(!raw){fclose(src);return -2;}
 snprintf(name,sizeof(name),"%s%corphan_hevc_samples.csv",outdir,SEP);FILE*log=fopen(name,"wb");if(!log){fclose(src);fclose(raw);return -3;}
 fputs("tbgp_offset,physical_payload_offset,bytes,video_ordinal,validation,nal_count,vcl_nals,irap_nals,vps,sps,pps,forbidden_or_temporal_error,stream_path\n",log);
 snprintf(dir,sizeof(dir),"%s%cOrphan_HEVC",outdir,SEP);MKDIR(dir);
 uint64_t prev=UINT64_MAX;FILE*stream=NULL;char streamname[4096]={0};int stream_valid=0;
 unsigned char *buf=NULL;size_t capacity=0;
 fgets(line,sizeof(line),src);
 while(fgets(line,sizeof(line),src)){
  if(stop&&stop(ctx))break;
  char *v[9]={0};int fields=0;v[fields++]=line;
  for(char *p=line;*p && fields<9;p++)if(*p==','){*p=0;v[fields++]=p+1;}
  if(fields<8)continue;
  if(strncmp(v[7],"UNASSOCIATED",12)!=0 && strcmp(v[7],"NO_VIDEO_OWNER")!=0 && strcmp(v[7],"PARTIAL_VIDEO_PREFIX_ONLY")!=0 && strcmp(v[7],"AMBIGUOUS_FULL_VIDEO_MATCH")!=0)continue;
  uint64_t anchor=strtoull(v[0],NULL,10),off=strtoull(v[1],NULL,10);
  unsigned long sz=strtoul(v[2],NULL,10),ordinal=strtoul(v[3],NULL,10);
  if(anchor!=prev){if(stream){fclose(stream);stream=NULL;}prev=anchor;stream_valid=0;streamname[0]=0;}
  st->samples++;
  if(sz<6||sz>MAX_SAMPLE){fprintf(log,"%"PRIu64",%"PRIu64",%lu,%lu,INVALID_SAMPLE_SIZE,0,0,0,0,0,0,0,\n",anchor,off,sz,ordinal);st->bad_samples++;continue;}
  if(sz>capacity){unsigned char *p=realloc(buf,sz);if(!p){st->bad_samples++;break;}buf=p;capacity=sz;}
  if(SEEK64(raw,(int64_t)off,SEEK_SET)||fread(buf,1,sz,raw)!=sz){fprintf(log,"%"PRIu64",%"PRIu64",%lu,%lu,READ_ERROR,0,0,0,0,0,0,0,\n",anchor,off,sz,ordinal);st->bad_samples++;continue;}
  size_t at=0;unsigned nal=0,vcl=0,irap=0,vps=0,sps=0,pps=0,headererr=0;int valid=1;
  while(at<sz){if(sz-at<6){valid=0;break;}uint32_t len=be32(buf+at);at+=4;
   if(len<2||len>sz-at){valid=0;break;}
   const unsigned char*h=buf+at;unsigned type=(h[0]>>1)&63;
   if((h[0]&128)||(h[1]&7)==0){valid=0;headererr++;break;}
   if(type<=31)vcl++;
   if(type>=16&&type<=23)irap++;
   if(type==32)vps++;
   if(type==33)sps++;
   if(type==34)pps++;
   nal++;at+=len;
  }
  if(!valid||!vcl){fprintf(log,"%"PRIu64",%"PRIu64",%lu,%lu,%s,%u,%u,%u,%u,%u,%u,%u,\n",anchor,off,sz,ordinal,valid?"NO_VCL_NAL":"INVALID_NAL_FRAMING",nal,vcl,irap,vps,sps,pps,headererr);st->bad_samples++;if(stream){fclose(stream);stream=NULL;}stream_valid=0;continue;}
  /* Each unit is independent; a damaged sample splits output and is never skipped in an assembled stream. */
  if(!stream){snprintf(streamname,sizeof(streamname),"Orphan_HEVC%canchor_%"PRIu64"_from_%lu.hevc",SEP,anchor,ordinal);
   char path[4096];snprintf(path,sizeof(path),"%s%c%s",outdir,SEP,streamname);stream=fopen(path,"wb");
   if(!stream){st->bad_samples++;continue;}st->streams++;stream_valid=1;}
  at=0;while(at<sz){uint32_t len=be32(buf+at);at+=4;unsigned char start[4]={0,0,0,1};if(fwrite(start,1,4,stream)!=4||fwrite(buf+at,1,len,stream)!=len){stream_valid=0;break;}at+=len;}
  if(!stream_valid){fclose(stream);stream=NULL;st->bad_samples++;continue;}
  fprintf(log,"%"PRIu64",%"PRIu64",%lu,%lu,STRUCTURAL_PASS,%u,%u,%u,%u,%u,%u,0,%s\n",anchor,off,sz,ordinal,nal,vcl,irap,vps,sps,pps,streamname);
  st->valid_samples++;st->bytes+=sz;st->random_access+=irap;st->parameter_sets+=vps+sps+pps;
 }
 if(stream)fclose(stream);
 free(buf);fclose(raw);fclose(src);fclose(log);
 snprintf(name,sizeof(name),"%s%corphan_hevc_summary.txt",outdir,SEP);FILE*s=fopen(name,"wb");if(s){fprintf(s,"Orphan HEVC structural analysis (NOT decoding)\nSamples: %"PRIu64"\nValidated: %"PRIu64"\nRejected: %"PRIu64"\nPayload bytes: %"PRIu64"\nIRAP NALs: %"PRIu64"\nEmbedded parameter-set NALs: %"PRIu64"\nIndependent streams: %"PRIu64"\nNo codec parameters synthesized. No anchor joins inferred from clocks.\n",st->samples,st->valid_samples,st->bad_samples,st->bytes,st->random_access,st->parameter_sets,st->streams);fclose(s);}return 0;
}
