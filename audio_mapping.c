#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "audio_mapping.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef _WIN32
#define FS _fseeki64
#else
#define FS fseeko
#endif
#define UNIT (20ull*1024*1024)
#define MAX_SAMPLE 12582912U
static uint32_t be32(const unsigned char*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint64_t be64(const unsigned char*p){return ((uint64_t)be32(p)<<32)|be32(p+4);}
typedef struct{const unsigned char *p;size_t size,head;} Box;
static int box(const unsigned char*p,size_t n,size_t i,Box*b){if(i>n||n-i<8)return 0;uint64_t z=be32(p+i);size_t h=8;if(z==1){if(n-i<16)return 0;z=be64(p+i+8);h=16;}if(!z)z=n-i;if(z<h||z>n-i||z>SIZE_MAX)return 0;*b=(Box){p+i,(size_t)z,h};return 1;}
static int child(Box par,const char*name,Box*out){for(size_t i=par.head;i+8<=par.size;){Box b;if(!box(par.p,par.size,i,&b))return 0;if(!memcmp(b.p+4,name,4)){*out=b;return 1;}i+=b.size;}return 0;}
static int path(Box p,const char*s,Box*out){for(;*s;s+=4)if(!child(p,s,&p))return 0;*out=p;return 1;}
typedef struct {uint32_t *sizes;uint64_t*positions;uint32_t count,rate,delta;} Audio;
static void dispose(Audio*a){free(a->sizes);free(a->positions);memset(a,0,sizeof(*a));}
static int audio_tables(const unsigned char*m,size_t n,Audio*a){Box mo;if(!box(m,n,0,&mo)||memcmp(mo.p+4,"moov",4))return 0;for(size_t p=mo.head;p+8<=mo.size;){Box tr;if(!box(mo.p,mo.size,p,&tr))return 0;p+=tr.size;if(memcmp(tr.p+4,"trak",4))continue;Box hd,sz,sc,co,md,stts;
  if(!path(tr,"mdiahdlr",&hd)||hd.size<hd.head+12||memcmp(hd.p+hd.head+8,"soun",4))continue;
  if(!path(tr,"mdiaminfstblstsz",&sz)||!path(tr,"mdiaminfstblstsc",&sc)||!path(tr,"mdiamdhd",&md)||!path(tr,"mdiaminfstblstts",&stts))return 0;
  int large=0;if(!path(tr,"mdiaminfstblstco",&co)){if(!path(tr,"mdiaminfstblco64",&co))return 0;large=1;}
  if(sz.size<sz.head+12||sc.size<sc.head+8||co.size<co.head+8||md.size<md.head+20||stts.size<stts.head+16)return 0;
  const unsigned char*z=sz.p+sz.head,*s=sc.p+sc.head,*c=co.p+co.head,*d=md.p+md.head,*t=stts.p+stts.head;
  uint32_t fixed=be32(z+4),count=be32(z+8),ns=be32(s+4),nc=be32(c+4);
  if(!count||count>3000000||(!fixed&&count>(sz.size-sz.head-12)/4)||!ns||!nc||ns>(sc.size-sc.head-8)/12||nc>(co.size-co.head-8)/(large?8:4)||be32(t+4)!=1)return 0;
  uint32_t rate=be32(d+(d[0]?20:12)),delta=be32(t+12);if(!rate||!delta)return 0;
  uint32_t *sizes=malloc((size_t)count*sizeof(*sizes));uint64_t*pos=malloc((size_t)count*sizeof(*pos));if(!sizes||!pos){free(sizes);free(pos);return 0;}
  for(uint32_t i=0;i<count;i++){sizes[i]=fixed?fixed:be32(z+12+(size_t)i*4);if(!sizes[i]||sizes[i]>MAX_SAMPLE){free(sizes);free(pos);return 0;}}
  uint32_t k=0,e=0;for(uint32_t j=1;j<=nc;j++){while(e+1<ns&&be32(s+8+(size_t)(e+1)*12)<=j)e++;const unsigned char*ent=s+8+(size_t)e*12;uint32_t first=be32(ent),spc=be32(ent+4),desc=be32(ent+8);if(!first||first>j||!spc||!desc||spc>count-k){free(sizes);free(pos);return 0;}uint64_t base=large?be64(c+8+(size_t)(j-1)*8):be32(c+8+(size_t)(j-1)*4);for(uint32_t x=0;x<spc;x++){pos[k]=base;base+=sizes[k++];}}
  if(k!=count){free(sizes);free(pos);return 0;}*a=(Audio){sizes,pos,count,rate,delta};return 1;
 }return 0;}
static int sample_entry(const unsigned char*m,size_t n,unsigned char**dst,size_t*len){
 Box mo;if(!box(m,n,0,&mo)||memcmp(mo.p+4,"moov",4))return 0;
 for(size_t p=mo.head;p+8<=mo.size;){Box tr;if(!box(mo.p,mo.size,p,&tr))return 0;p+=tr.size;
  if(memcmp(tr.p+4,"trak",4))continue;
  Box hd,sd;if(!path(tr,"mdiahdlr",&hd)||hd.size<hd.head+12||memcmp(hd.p+hd.head+8,"soun",4))continue;
  if(!path(tr,"mdiaminfstblstsd",&sd)||sd.size<sd.head+16)return 0;
  const unsigned char *q=sd.p+sd.head;if(be32(q+4)!=1)return 0;
  Box ent;if(!box(sd.p,sd.size,sd.head+8,&ent)||memcmp(ent.p+4,"mp4a",4)||ent.size<36||ent.size>65536)return 0;
  unsigned char*b=malloc(ent.size);if(!b)return 0;memcpy(b,ent.p,ent.size);*dst=b;*len=ent.size;return 1;
 }return 0;
}
static int locate(const Audio*a,uint64_t pos,uint32_t len,uint32_t *idx){/* absolute logical payload position from independently mapped unit */
 size_t low=0,high=a->count;while(low<high){size_t mid=low+(high-low)/2;if(a->positions[mid]<pos)low=mid+1;else high=mid;}if(low==a->count||a->positions[low]!=pos||a->sizes[low]!=len)return 0;*idx=(uint32_t)low;return 1;}

struct WB_AudioSession {Audio expected;FILE *report;uint64_t owner,accepted,rejected;unsigned char *entry;size_t entrylen;};
WB_AudioSession *wb_audio_begin(const unsigned char *moov,size_t n,
 const char *report_path,uint64_t owner){
 WB_AudioSession *s=calloc(1,sizeof(*s));if(!s)return NULL;
 if(!audio_tables(moov,n,&s->expected)){free(s);return NULL;}
 (void)sample_entry(moov,n,&s->entry,&s->entrylen);
 s->report=fopen(report_path,"wb");if(!s->report){free(s->entry);dispose(&s->expected);free(s);return NULL;}
 s->owner=owner;
 fputs("anchor,raw_moov,logical_payload_offset,physical_payload_offset,bytes,audio_sample_index,status\n",s->report);
 return s;
}
void wb_audio_observe(WB_AudioSession *s,uint64_t anchor,uint64_t logical,
 uint64_t physical,uint32_t len){
 if(!s)return;
 uint32_t idx=0;int ok=locate(&s->expected,logical,len,&idx);
 fprintf(s->report,"%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,",
 anchor,s->owner,logical,physical,len);
 if(ok){fprintf(s->report,"%u,EXACT_POSITION_AND_SIZE\n",idx);s->accepted++;}
 else{fputs("-1,UNRESOLVED_AUDIO_SAMPLE\n",s->report);s->rejected++;}
}
int wb_audio_match(WB_AudioSession *s,uint64_t logical,uint32_t n,uint32_t *idx){
 return s&&locate(&s->expected,logical,n,idx);
}
int wb_audio_description(WB_AudioSession*s,unsigned char **entry,size_t *length,
 uint32_t *rate,uint32_t *delta){
 if(!s||!s->entry||!s->entrylen)return 0;
 *entry=malloc(s->entrylen);if(!*entry)return 0;memcpy(*entry,s->entry,s->entrylen);
 *length=s->entrylen;*rate=s->expected.rate;*delta=s->expected.delta;return 1;
}
void wb_audio_end(WB_AudioSession *s,int completed){
 if(!s)return;
 fprintf(s->report,"#summary,accepted=%"PRIu64",rejected=%"PRIu64",audio_expected=%u,sample_rate=%u,delta=%u,completed=%d\n",
 s->accepted,s->rejected,s->expected.count,s->expected.rate,s->expected.delta,completed);
 fclose(s->report);free(s->entry);dispose(&s->expected);free(s);
}
