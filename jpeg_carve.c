/* JPEG recovery during existing sequential read. No SQL, no FAT and no invented bytes.
 * Files only after complete SOI/SOS/EOI plus valid JPEG segment framing.
 * Nearest anchor proximity is investigative evidence, not recording ownership. */
#define _POSIX_C_SOURCE 200809L
#include "jpeg_carve.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef _WIN32
#include <windows.h>
#define SEP '\\'
#else
#include <sys/stat.h>
#define SEP '/'
#endif
#define JPEG_MAX (32u*1024u*1024u)
typedef struct {uint64_t start;uint32_t size;uint16_t width,height;char name[96];} Image;
struct WBJpeg {
 char root[4096], jpegdir[4096];
 unsigned char *buf;size_t used,cap;
 uint64_t begin,last_off,rejected,too_large;
 int collecting;unsigned char prev1,prev2;
 uint64_t *anchors;size_t na,ac;
 Image *images;size_t ni,ic;
};
static int inspect(const unsigned char *b,size_t n,uint16_t *w,uint16_t *h){
 if(n<16||b[0]!=0xff||b[1]!=0xd8||b[n-2]!=0xff||b[n-1]!=0xd9)return 0;
 size_t p=2;int saw_sos=0,saw_sof=0,saw_dqt=0;
 while(p+3<n){
  if(b[p++]!=0xff)return 0;
  while(p<n&&b[p]==0xff)p++;
  if(p>=n)return 0;
  unsigned char marker=b[p++];
  if(marker==0xda){if(p+2>n)return 0;size_t z=((size_t)b[p]<<8)|b[p+1];if(z<6||p+z>n-2)return 0;saw_sos=1;break;}
  if(marker==0xd9)return 0;
  if(marker==0xd8||marker==0x01||(marker>=0xd0&&marker<=0xd7))return 0;
  if(p+2>n)return 0;
  size_t z=((size_t)b[p]<<8)|b[p+1];if(z<2||z>n-p)return 0;
  if(marker==0xdb)saw_dqt=1;
  if(((marker>=0xc0&&marker<=0xc3)||(marker>=0xc5&&marker<=0xc7)||(marker>=0xc9&&marker<=0xcb)||(marker>=0xcd&&marker<=0xcf))&&z>=8){
   *h=(uint16_t)((b[p+3]<<8)|b[p+4]);*w=(uint16_t)((b[p+5]<<8)|b[p+6]);saw_sof=(*h>0&&*w>0);
  }
  p+=z;
 }
 return saw_sos&&saw_sof&&saw_dqt;
}
static void keep_image(WBJpeg *j){
 uint16_t w=0,h=0;
 if(!inspect(j->buf,j->used,&w,&h)){j->rejected++;return;}
 if(j->ni==j->ic){size_t c=j->ic?j->ic*2:128;Image *a=realloc(j->images,c*sizeof(*a));if(!a){j->rejected++;return;}j->images=a;j->ic=c;}
 Image *a=&j->images[j->ni];memset(a,0,sizeof(*a));a->start=j->begin;a->size=(uint32_t)j->used;a->width=w;a->height=h;
 snprintf(a->name,sizeof(a->name),"jpeg_%012"PRIX64".jpg",j->begin);
 char path[8192];snprintf(path,sizeof(path),"%s%c%s",j->jpegdir,SEP,a->name);
 FILE *f=fopen(path,"wb");if(!f){j->rejected++;return;}
 size_t wrote=fwrite(j->buf,1,j->used,f);int err=fclose(f);
 if(wrote!=j->used||err){remove(path);j->rejected++;return;}
 j->ni++;
}
WBJpeg *wb_jpeg_new(const char *outdir){
 WBJpeg *j=calloc(1,sizeof(*j));if(!j)return NULL;
 snprintf(j->root,sizeof(j->root),"%s",outdir);
 snprintf(j->jpegdir,sizeof(j->jpegdir),"%s%cJPEG_Recovered",outdir,SEP);
#ifdef _WIN32
 CreateDirectoryA(j->jpegdir,NULL);
#else
 mkdir(j->jpegdir,0777);
#endif
 return j;
}
void wb_jpeg_anchor(WBJpeg *j,uint64_t off){
 if(!j)return;
 if(j->na==j->ac){size_t c=j->ac?2*j->ac:256;uint64_t *a=realloc(j->anchors,c*sizeof(*a));if(!a)return;j->anchors=a;j->ac=c;}
 j->anchors[j->na++]=off;
}
void wb_jpeg_feed(WBJpeg *j,const unsigned char *b,size_t n,uint64_t off){
 if(!j)return;
 for(size_t k=0;k<n;k++){
  unsigned char v=b[k];uint64_t pos=off+k;
  if(j->last_off&&pos!=j->last_off+1){j->collecting=0;j->prev1=j->prev2=0;}
  j->last_off=pos;
  if(!j->collecting){
   if(j->prev2==0xff&&j->prev1==0xd8&&v==0xff){
    j->begin=pos-2;j->collecting=1;j->used=0;
    if(!j->buf){j->cap=65536;j->buf=malloc(j->cap);if(!j->buf){j->collecting=0;return;}}
    j->buf[j->used++]=0xff;j->buf[j->used++]=0xd8;
   } else {j->prev2=j->prev1;j->prev1=v;continue;}
  }
  if(j->used==j->cap){size_t c=j->cap*2;
   if(c>JPEG_MAX){j->too_large++;j->collecting=0;j->prev1=j->prev2=0;continue;}
   unsigned char *q=realloc(j->buf,c);if(!q){j->rejected++;j->collecting=0;continue;}j->buf=q;j->cap=c;
  }
  j->buf[j->used++]=v;
  if(j->used>=2&&j->buf[j->used-2]==0xff&&v==0xd9){keep_image(j);j->collecting=0;j->prev1=j->prev2=0;}
 }
}
static int cmp64(const void *aa,const void *bb){uint64_t a=*(const uint64_t*)aa,b=*(const uint64_t*)bb;return (a>b)-(a<b);}
void wb_jpeg_finish(WBJpeg *j,uint64_t *recovered,uint64_t *rejected){
 if(recovered)*recovered=0;
 if(rejected)*rejected=0;
 if(!j)return;
 if(j->collecting)j->rejected++;
 qsort(j->anchors,j->na,sizeof(uint64_t),cmp64);
 char path[8192];snprintf(path,sizeof(path),"%s%cjpeg_evidence.csv",j->root,SEP);
 FILE *f=fopen(path,"wb");
 if(f){
  fputs("jpeg_offset,jpeg_end_exclusive,size,width,height,file,nearest_tbgp_offset,distance_bytes,within_20mib_of_anchor,association_status\n",f);
  for(size_t i=0;i<j->ni;i++){
   Image *m=&j->images[i];uint64_t mid=m->start+m->size/2,best=0,dist=UINT64_MAX;
   size_t lo=0,hi=j->na;
   while(lo<hi){size_t t=lo+(hi-lo)/2;if(j->anchors[t]<mid)lo=t+1;else hi=t;}
   if(lo<j->na){best=j->anchors[lo];dist=(best>mid)?best-mid:mid-best;}
   if(lo>0){uint64_t x=j->anchors[lo-1],d=(x>mid)?x-mid:mid-x;if(d<dist){best=x;dist=d;}}
   fprintf(f,"%"PRIu64",%"PRIu64",%u,%u,%u,%s,",m->start,m->start+m->size,m->size,m->width,m->height,m->name);
   if(dist==UINT64_MAX)fputs(",,NO,UNASSOCIATED_NEARNESS_NOT_OWNERSHIP\n",f);
   else fprintf(f,"%"PRIu64",%"PRIu64",%s,UNASSOCIATED_NEARNESS_NOT_OWNERSHIP\n",best,dist,dist<=20u*1024u*1024u?"YES":"NO");
  }
  fclose(f);
 }
 snprintf(path,sizeof(path),"%s%cjpeg_scan_summary.txt",j->root,SEP);
 f=fopen(path,"wb");if(f){fprintf(f,"JPEGs recovered: %zu\nRejected candidate streams: %"PRIu64"\nExceeded 32 MiB cap: %"PRIu64"\nJPEG stream left incomplete at end: %s\nJPEGs are physical carvings, not confirmed video thumbnails. Nearness to tbgp is not ownership.\n",j->ni,j->rejected,j->too_large,j->collecting?"yes":"no");fclose(f);}
 if(recovered)*recovered=j->ni;
 if(rejected)*rejected=j->rejected+j->too_large;
 free(j->buf);free(j->anchors);free(j->images);free(j);
}
