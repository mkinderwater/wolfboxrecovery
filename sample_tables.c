#include "sample_tables.h"
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t b32(const unsigned char *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint64_t b64(const unsigned char *p){return ((uint64_t)b32(p)<<32)|b32(p+4);}
typedef struct {const unsigned char *p; size_t n, header;} Box;
static int boxat(const unsigned char *p,size_t n,size_t off, Box *out){
 if(off>n||n-off<8)return 0;
 uint64_t z=b32(p+off);size_t h=8;
 if(z==1){if(n-off<16)return 0;z=b64(p+off+8);h=16;}
 if(!z)z=n-off;
 if(z<h||z>n-off||z>SIZE_MAX)return 0;
 out->p=p+off;out->n=(size_t)z;out->header=h;return 1;
}
static int child(Box parent,const char name[4],Box *found){
 size_t i=parent.header;Box q;
 while(i+8<=parent.n){if(!boxat(parent.p,parent.n,i,&q))return 0;
  if(!memcmp(q.p+4,name,4)){*found=q;return 1;}i+=q.n;
 }return 0;
}
static int nested(Box box,const char *path,Box *out){
 Box x=box;for(size_t i=0;path[i];i+=4){if(!child(x,path+i,&x))return 0;}*out=x;return 1;
}
static int verify_stsc(const Box *stsc,uint32_t chunks,uint64_t *implied){
 if(stsc->n<stsc->header+8)return 0;
 const unsigned char *p=stsc->p+stsc->header;
 uint32_t count=b32(p+4);size_t cap=stsc->n-stsc->header-8;
 if(count==0||count>cap/12)return 0;
 uint32_t prev=0;uint32_t prev_spc=0;uint64_t total=0;
 for(uint32_t i=0;i<count;i++){
  const unsigned char *entry=p+8+(size_t)i*12;
  uint32_t first=b32(entry),spc=b32(entry+4),desc=b32(entry+8);
  if(first==0||first<=prev||spc==0||desc==0||(i==0&&first!=1))return 0;
  if(i)total+=(uint64_t)(first-prev)*prev_spc;
  prev=first;prev_spc=spc;
 }
 if(chunks<prev)return 0;
 total+=(uint64_t)(chunks-prev+1)*prev_spc;
 *implied=total;return 1;
}
static void index_track(Box trak,uint64_t moov,uint32_t trackno,FILE *report){
 Box hdlr,stbl,stsz,stsc,stco,co64;
 char kind[5]="????";
 if(nested(trak,"mdiahdlr",&hdlr) && hdlr.n>=hdlr.header+12)memcpy(kind,hdlr.p+hdlr.header+8,4);
 int hasstbl=nested(trak,"mdiaminfstbl",&stbl);
 int hasstsz=hasstbl && child(stbl,"stsz",&stsz);
 int hasstsc=hasstbl && child(stbl,"stsc",&stsc);
 int hasstco=hasstbl && child(stbl,"stco",&stco);
 int hasco64=hasstbl && child(stbl,"co64",&co64);
 uint32_t count=0,chunkcount=0,constant=0,first[4]={0};uint64_t expected=0;
 int valid=hasstsz&&hasstsc&&(hasstco!=hasco64);
 if(hasstsz){
  if(stsz.n<stsz.header+12)valid=0;
  else {
   const unsigned char *p=stsz.p+stsz.header;
   constant=b32(p+4);count=b32(p+8);
   if(!constant && (uint64_t)count>(stsz.n-stsz.header-12)/4)valid=0;
   if(valid)for(uint32_t j=0;j<4&&j<count;j++)first[j]=constant?constant:b32(p+12+j*4);
  }
 }
 if(hasstco||hasco64){
  Box *ct=hasstco?&stco:&co64;size_t step=hasstco?4:8;
  if(ct->n<ct->header+8)valid=0;
  else {chunkcount=b32(ct->p+ct->header+4);
   if(chunkcount>(ct->n-ct->header-8)/step)valid=0;
  }
 }
 if(valid && !verify_stsc(&stsc,chunkcount,&expected))valid=0;
 if(valid && expected!=count)valid=0;
 fprintf(report,"%"PRIu64",%u,%s,%u,%u,%u,%u,%u,%u,%u,%"PRIu64",%s\n",moov,trackno,kind,count,chunkcount,constant,first[0],first[1],first[2],first[3],expected,valid?"TABLES_CONSISTENT":"INCOMPLETE_OR_CONFLICTING");
}
int wb_index_moov(const unsigned char *box,size_t length,uint64_t physical_offset,FILE *report){
 Box moov;if(!boxat(box,length,0,&moov)||moov.n!=length||memcmp(moov.p+4,"moov",4))return -1;
 int tracks=0;size_t i=moov.header;Box q;
 while(i+8<=moov.n){if(!boxat(moov.p,moov.n,i,&q))return -2;
  if(!memcmp(q.p+4,"trak",4))index_track(q,physical_offset,++tracks,report);
  i+=q.n;
 }
 return tracks;
}
