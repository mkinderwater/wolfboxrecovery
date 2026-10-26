#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "scan_cache.h"
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <sys/stat.h>
#define STAT_STRUCT struct _stat64
#define STAT _stat64
#define FSEEK _fseeki64
#define FTELL _ftelli64
#define SLASH '\\'
#else
#include <sys/stat.h>
#define STAT_STRUCT struct stat
#define STAT stat
#define FSEEK fseeko
#define FTELL ftello
#define SLASH '/'
#endif
#define CACHE_VERSION "WolfBox-C-2.13-orphan-nal-1"
#define PROBE_BYTES 65536U
static void pathcat(char *buf,size_t cap,const char *dir,const char *name){snprintf(buf,cap,"%s%c%s",dir,SLASH,name);}
static uint64_t feed(uint64_t h,const unsigned char *b,size_t n){for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ULL;}return h;}
static int signature(const char *file,uint64_t *length,int64_t *modified,uint64_t *probe){
 STAT_STRUCT st;if(STAT(file,&st)||st.st_size<=0)return 0;
 *length=(uint64_t)st.st_size;*modified=(int64_t)st.st_mtime;
 FILE *f=fopen(file,"rb");if(!f)return 0;
 unsigned char b[PROBE_BYTES];uint64_t h=14695981039346656037ULL;
 uint64_t positions[4]={0,*length/3,(*length/3)*2,*length>PROBE_BYTES?*length-PROBE_BYTES:0};
 for(int i=0;i<4;i++){
  uint64_t pos=positions[i];if(pos>=*length)pos=0;
  if(FSEEK(f,(int64_t)pos,SEEK_SET)){fclose(f);return 0;}
  size_t n=(*length-pos<PROBE_BYTES)?(size_t)(*length-pos):PROBE_BYTES;
  if(fread(b,1,n,f)!=n){fclose(f);return 0;}
  h=feed(h,(const unsigned char*)&pos,sizeof(pos));h=feed(h,b,n);
 }
 fclose(f);*probe=h;return 1;
}
static int outputs_present(const char *dir){
 const char *required[]={"container_time_fields.csv","tbgp_wrapper_clocks.csv","recording_inventory.csv","sample_table_index.csv","fat_geometry_candidates.csv","logical_placements.csv","physical_ownership.csv","candidate_unit_runs.csv","recording_summary.csv","association_decisions.csv","physical_video_samples.csv","ownership_candidates.csv","unmapped_priorities.csv","scan_summary.txt","orphan_hevc_samples.csv","orphan_hevc_summary.txt","jpeg_evidence.csv","jpeg_scan_summary.txt"};
 char buf[4096];for(size_t i=0;i<sizeof(required)/sizeof(required[0]);i++){
  pathcat(buf,sizeof(buf),dir,required[i]);FILE *f=fopen(buf,"rb");if(!f)return 0;
  if(fseek(f,0,SEEK_END)||ftell(f)<=0){fclose(f);return 0;}fclose(f);
 }return 1;
}
int wb_scan_cached(const char *image,const char *outdir,WBEvent ev,WBStop stop,void *ctx,WBProgress *p){
 uint64_t size,probe;int64_t mtime;
 if(!signature(image,&size,&mtime,&probe))return -21;
 char filename[4096],tmp[4096];pathcat(filename,sizeof(filename),outdir,".wolfbox_scan_cache_v1.3");pathcat(tmp,sizeof(tmp),outdir,".wolfbox_scan_cache_v1.3.tmp");
 char wanted[256];snprintf(wanted,sizeof(wanted),"%s\n%s\n%"PRIu64"\n%"PRId64"\n%016"PRIx64"\n",CACHE_VERSION,image,size,mtime,probe);
 FILE *f=fopen(filename,"rb");if(f){char found[256]={0};size_t n=fread(found,1,sizeof(found)-1,f);found[n]=0;fclose(f);
  if(!strcmp(found,wanted)&&outputs_present(outdir)){
   memset(p,0,sizeof(*p));p->size=size;p->scanned=size;
   if(ev)ev(ctx,"CACHE_HIT",size,"Verified source sample fingerprint and complete report set; reused analysis");
   return 0;
  }
 }
 /* An interrupted or changed-source scan cannot retain a valid manifest. */
 remove(filename);remove(tmp);
 if(ev)ev(ctx,"CACHE_MISS",0,"Scanning image; no matching completed analysis cache");
 int rc=wb_scan(image,outdir,ev,stop,ctx,p);
 if(rc==0 && !(stop&&stop(ctx)) && outputs_present(outdir)){
  f=fopen(tmp,"wb");if(f){int ok=fwrite(wanted,1,strlen(wanted),f)==strlen(wanted);
   if(fclose(f)==0&&ok){if(rename(tmp,filename)!=0)remove(tmp);}else remove(tmp);
  }
 }
 return rc;
}
