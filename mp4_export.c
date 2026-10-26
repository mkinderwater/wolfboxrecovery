#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include "mp4_export.h"
#include "audio_mapping.h"
#ifdef _WIN32
#define SEEK64 _fseeki64
#define TELL64 _ftelli64
#else
#include <sys/types.h>
#define SEEK64 fseeko
#define TELL64 ftello
#endif
#define UNIT (20ull*1024ull*1024ull)
#define MAX_SAMPLE (12u*1024u*1024u)
static uint32_t b32(const unsigned char*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t b64(const unsigned char*p){return ((uint64_t)b32(p)<<32)|b32(p+4);}
static void put32(FILE*f,uint32_t n){unsigned char b[4]={(unsigned char)(n>>24),(unsigned char)(n>>16),(unsigned char)(n>>8),(unsigned char)n};fwrite(b,1,4,f);}
static void put16(FILE*f,unsigned n){fputc((n>>8)&255,f);fputc(n&255,f);}
static int readat(FILE*f,uint64_t p,void*b,size_t n){return p<=INT64_MAX&&!SEEK64(f,(int64_t)p,SEEK_SET)&&fread(b,1,n,f)==n;}
static uint64_t box(FILE*f,const char*t){uint64_t p=(uint64_t)TELL64(f);put32(f,0);fwrite(t,1,4,f);return p;}
static int endbox(FILE*f,uint64_t p){uint64_t e=(uint64_t)TELL64(f);if(e-p>UINT32_MAX)return 0;if(SEEK64(f,(int64_t)p,SEEK_SET))return 0;put32(f,(uint32_t)(e-p));return !SEEK64(f,(int64_t)e,SEEK_SET);}
static void matrix(FILE*f){put32(f,65536);put32(f,0);put32(f,0);put32(f,0);put32(f,65536);put32(f,0);put32(f,0);put32(f,0);put32(f,0x40000000);}
static int atom(const unsigned char *b,size_t n,const char *tag,size_t *start,size_t *len){
 for(size_t i=4;i+8<=n;i++){if(memcmp(b+i,tag,4))continue;size_t p=i-4;uint64_t z=b32(b+p);if(z==1&&p+16<=n)z=b64(b+p+8);if(z>=8&&z<=n-p){*start=p;*len=(size_t)z;return 1;}}
 return 0;
}
static int moovread(FILE*f,uint64_t off,unsigned char**out,size_t*sz){unsigned char h[16];if(!readat(f,off,h,16)||memcmp(h+4,"moov",4))return 0;uint64_t z=b32(h);if(z==1)z=b64(h+8);if(z<8||z>128*1024*1024)return 0;unsigned char*b=malloc((size_t)z);if(!b)return 0;if(!readat(f,off,b,(size_t)z)){free(b);return 0;}*out=b;*sz=(size_t)z;return 1;}
static int cfgfind(const unsigned char *b,size_t n,unsigned char **cfg,size_t *cfglen,unsigned *width,unsigned *height,unsigned *ls){size_t p,z;if(!atom(b,n,"hvcC",&p,&z)||p<60||z<31)return 0;*cfg=malloc(z);if(!*cfg)return 0;memcpy(*cfg,b+p,z);*cfglen=z;const unsigned char*data=b+p+8;if(data[0]!=1){free(*cfg);return 0;}*ls=(data[21]&3)+1;*width=((unsigned)b[p-54]<<8)|b[p-53];*height=((unsigned)b[p-52]<<8)|b[p-51];if(!*width||!*height||*width>16384||*height>16384){free(*cfg);return 0;}return 1;}
/* Only a single constant stts entry is currently supported. Unsupported timings fail closed. */
static int timing(const unsigned char*b,size_t n,unsigned *scale,unsigned *delta){size_t p,z,q,y;if(!atom(b,n,"mdhd",&p,&z))return 0;const unsigned char*d=b+p+8;size_t hdr=d[0]?28:20;if(z<8+hdr)return 0;*scale=b32(d+(d[0]?20:12));if(!*scale)return 0;if(!atom(b,n,"stts",&q,&y)||y<24)return 0;const unsigned char*s=b+q+8;if(b32(s+4)!=1)return 0;*delta=b32(s+12);return *delta>0;}
static int audio_track(FILE *f,const unsigned char *entry,size_t entrylen,
 uint32_t *sizes,unsigned n,uint32_t rate,uint32_t delta,uint64_t offset,
 uint32_t movie_scale,uint64_t lead_movie){
 if(!n||!rate||!delta||offset>UINT32_MAX||!entry||!entrylen)return 0;
 uint64_t dur=(uint64_t)n*delta, play=(dur*movie_scale+rate-1)/rate;
 if(dur>UINT32_MAX||play>UINT32_MAX||lead_movie+play>UINT32_MAX)return 0;
 int ok=1;uint64_t tr=box(f,"trak"),x=box(f,"tkhd");
 put32(f,7);put32(f,0);put32(f,0);put32(f,2);put32(f,0);put32(f,(uint32_t)(lead_movie+play));
 put32(f,0);put32(f,0);put16(f,0);put16(f,0);put16(f,256);put16(f,0);matrix(f);put32(f,0);put32(f,0);ok&=endbox(f,x);
 if(lead_movie){uint64_t ed=box(f,"edts");x=box(f,"elst");put32(f,0);put32(f,2);
 put32(f,(uint32_t)lead_movie);put32(f,0xffffffffu);put16(f,1);put16(f,0);
 put32(f,(uint32_t)play);put32(f,0);put16(f,1);put16(f,0);ok&=endbox(f,x);ok&=endbox(f,ed);}
 uint64_t md=box(f,"mdia");x=box(f,"mdhd");put32(f,0);put32(f,0);put32(f,0);put32(f,rate);put32(f,(uint32_t)dur);put16(f,0x55c4);put16(f,0);ok&=endbox(f,x);
 x=box(f,"hdlr");put32(f,0);put32(f,0);fwrite("soun",1,4,f);put32(f,0);put32(f,0);put32(f,0);fwrite("SoundHandler\0",1,13,f);ok&=endbox(f,x);
 uint64_t mi=box(f,"minf");x=box(f,"smhd");put32(f,0);put16(f,0);put16(f,0);ok&=endbox(f,x);
 uint64_t din=box(f,"dinf");x=box(f,"dref");put32(f,0);put32(f,1);uint64_t url=box(f,"url ");put32(f,1);ok&=endbox(f,url);ok&=endbox(f,x);ok&=endbox(f,din);
 uint64_t st=box(f,"stbl");x=box(f,"stsd");put32(f,0);put32(f,1);ok&=(fwrite(entry,1,entrylen,f)==entrylen);ok&=endbox(f,x);
 x=box(f,"stts");put32(f,0);put32(f,1);put32(f,n);put32(f,delta);ok&=endbox(f,x);
 x=box(f,"stsc");put32(f,0);put32(f,1);put32(f,1);put32(f,n);put32(f,1);ok&=endbox(f,x);
 x=box(f,"stsz");put32(f,0);put32(f,0);put32(f,n);for(unsigned k=0;k<n;k++)put32(f,sizes[k]);ok&=endbox(f,x);
 x=box(f,"stco");put32(f,0);put32(f,1);put32(f,(uint32_t)offset);ok&=endbox(f,x);
 ok&=endbox(f,st);ok&=endbox(f,mi);ok&=endbox(f,md);ok&=endbox(f,tr);return ok;
}
static int valid_sample(const unsigned char*b,size_t n,unsigned l){size_t i=0;int cnt=0;if(l<1||l>4)return 0;while(i<n){if(n-i<l)return 0;uint32_t sz=0;for(unsigned j=0;j<l;j++)sz=(sz<<8)|b[i++];if(sz<2||sz>n-i)return 0;unsigned type=(b[i]>>1)&63;if(type>63)return 0;i+=sz;cnt++;}return cnt>0;}
typedef struct {uint64_t anchor,owner,base;unsigned track,first,probed;} WBUnit;
static int unitcmp(const void*a,const void*b){const WBUnit*x=a,*y=b;return (x->base>y->base)-(x->base<y->base);}
static unsigned load_run(const char *csv,uint64_t anchor,WBUnit **result){
 FILE*f=fopen(csv,"rb");if(!f)return 0;
 char line[2048];WBUnit *units=NULL;size_t n=0,cap=0;uint64_t owner=0;unsigned track=0;int requested=0;
 if(!fgets(line,sizeof(line),f)){fclose(f);return 0;}
 while(fgets(line,sizeof(line),f)){
  WBUnit u={0};char state[80],stop[80];
  if(sscanf(line,"%"SCNu64",%"SCNu64",%u,%u,%"SCNu64",%u,%79[^,],%79[^\r\n]",&u.anchor,&u.owner,&u.track,&u.first,&u.base,&u.probed,state,stop)!=8)continue;
  if(strcmp(state,"ALIGNED_UNIQUE_PREFIX")||u.base%UNIT||u.probed<4||u.probed>8192)continue;
  if(u.anchor==anchor){owner=u.owner;track=u.track;requested=1;}
  if(n==cap){size_t nc=cap?cap*2:128;WBUnit*t=realloc(units,nc*sizeof(*units));if(!t){free(units);fclose(f);return 0;}units=t;cap=nc;}
  units[n++]=u;
 }
 fclose(f);
 if(!requested){free(units);return 0;}
 size_t keep=0;for(size_t i=0;i<n;i++)if(units[i].owner==owner&&units[i].track==track)units[keep++]=units[i];
 qsort(units,keep,sizeof(*units),unitcmp);
 /* Duplicate claimed logical bases are conflicts, not candidates to choose from. */
 for(size_t i=1;i<keep;i++)if(units[i-1].base==units[i].base){free(units);return 0;}
 size_t at=0;while(at<keep&&units[at].anchor!=anchor)at++;
 if(at==keep){free(units);return 0;}
 size_t lo=at,hi=at;
 while(lo&&units[lo-1].base+UNIT==units[lo].base&&units[lo-1].first<units[lo].first)lo--;
 while(hi+1<keep&&units[hi].base+UNIT==units[hi+1].base&&units[hi].first<units[hi+1].first)hi++;
 unsigned out=(unsigned)(hi-lo+1);memmove(units,units+lo,out*sizeof(*units));*result=units;return out;
}
/* Read the owning moov's video stsz. Reject unsupported or suspicious sizes. */
static uint32_t*video_sizes(const unsigned char *b,size_t n,unsigned *total){
 size_t p,z;if(!atom(b,n,"stsz",&p,&z)||z<20)return NULL;
 const unsigned char *q=b+p+8;uint32_t fixed=b32(q+4),count=b32(q+8);
 if(!count||count>2000000||(!fixed&&(uint64_t)count>(z-20)/4))return NULL;
 uint32_t*a=malloc((size_t)count*sizeof(*a));if(!a)return NULL;
 for(unsigned k=0;k<count;k++){a[k]=fixed?fixed:b32(q+12+4ull*k);if(!a[k]||a[k]>MAX_SAMPLE){free(a);return NULL;}}
 *total=count;return a;
}
int wb_export_mp4_run(const char*imgpath,const char*csvpath,const char*outpath,uint64_t anchor,int (*cancel)(void*),void *cancel_ctx){
 WBUnit *units=NULL;unsigned nunits=load_run(csvpath,anchor,&units);
 if(!nunits)return 3;
 FILE*img=fopen(imgpath,"rb");if(!img){free(units);return 2;}
 unsigned char*moov=NULL;size_t ml=0;unsigned char*cfg=NULL;size_t cl=0;
 unsigned w=0,h=0,ls=0,scale=0,delta=0,expected_count=0;
 uint32_t *expected=NULL;
 if(!moovread(img,units[0].owner,&moov,&ml)||!cfgfind(moov,ml,&cfg,&cl,&w,&h,&ls)||
    !timing(moov,ml,&scale,&delta)||(expected=video_sizes(moov,ml,&expected_count))==NULL){
  free(moov);free(cfg);free(units);fclose(img);return 4;
 }
 /* Create the AAC evidence report before releasing original moov. */
 char audio_path[4096];WB_AudioSession *audio=NULL;
 if(snprintf(audio_path,sizeof(audio_path),"%s.audio_mapping.csv",outpath)<(int)sizeof(audio_path))
  audio=wb_audio_begin(moov,ml,audio_path,units[0].owner);
 unsigned char *audio_entry=NULL;size_t audio_entrylen=0;
 uint32_t arate=0,adelta=0;
 int audio_possible=wb_audio_description(audio,&audio_entry,&audio_entrylen,&arate,&adelta);
 free(moov);
 if(units[0].first>=expected_count){wb_audio_end(audio,0);free(expected);free(cfg);free(units);fclose(img);return 4;}
 char tmp[4096];if(strlen(outpath)>sizeof(tmp)-8){wb_audio_end(audio,0);free(expected);free(cfg);free(units);fclose(img);return 2;}
 snprintf(tmp,sizeof(tmp),"%s.part",outpath);FILE*out=fopen(tmp,"wb+");
 if(!out){wb_audio_end(audio,0);free(expected);free(cfg);free(units);fclose(img);return 2;}
 uint64_t ftyp=box(out,"ftyp");fwrite("isom",1,4,out);put32(out,512);fwrite("isomiso6mp41",1,12,out);endbox(out,ftyp);
 uint64_t mdat=box(out,"mdat");uint64_t sampleoff=(uint64_t)TELL64(out);
 unsigned char *unitbuf=malloc((size_t)UNIT);
 uint32_t *sz=calloc(expected_count-units[0].first,sizeof(*sz));
 unsigned count=0, next_index=units[0].first;int ok=sz!=NULL && unitbuf!=NULL;
 unsigned processed=0;char end_reason[64]="";
 FILE *aspool=audio_possible?tmpfile():NULL;
 uint32_t *asz=NULL;size_t an=0,acap=0;uint32_t afirst=0,anext=0;
 int audio_clean=aspool!=NULL;
 const char *audio_reason=audio_possible?"NO_VERIFIED_AUDIO":"AAC_DESCRIPTION_UNAVAILABLE";
 unsigned audio_rejected=0;
 /* Do not infer microphone capability from an AAC track in moov. */


 for(unsigned u=0;ok&&u<nunits;u++){
  if(cancel&&cancel(cancel_ctx)){ok=0;strcpy(end_reason,"CANCELLED");break;}
  uint64_t start=units[u].anchor;
  /* A full 20 MiB read replaces hundreds of random wrapper and payload reads.
   * Failed/short reads are never zero-filled or silently treated as media. */
  if(!readat(img,start,unitbuf,(size_t)UNIT)){ok=0;strcpy(end_reason,"UNIT_READ_FAILED");break;}
  if(memcmp(unitbuf+4,"tbgp",4)){ok=0;strcpy(end_reason,"MISSING_TBGP");break;}
  uint64_t tz=b32(unitbuf);if(tz==1)tz=b64(unitbuf+8);
  if(tz<8||tz>=UNIT||UINT64_MAX-start<UNIT){ok=0;break;}
  if(units[u].first!=next_index){ok=0;strcpy(end_reason,"SAMPLE_GAP");break;}
  uint64_t pos=tz;
  unsigned stop_at=(u+1<nunits)?units[u+1].first:expected_count;
  unsigned matched_probe=0;
  while(ok&&pos+20<=UNIT){
   if(cancel&&cancel(cancel_ctx)){ok=0;strcpy(end_reason,"CANCELLED");break;}
   const unsigned char *head=unitbuf+(size_t)pos;
   uint32_t cls=b32(head),n=b32(head+16);
   if(cls>=1&&cls<=3&&n&&n<=MAX_SAMPLE&&n<=UNIT-pos-20){
    if(cls==1){
     if(next_index>=stop_at){ok=0;strcpy(end_reason,"VIDEO_BEYOND_UNIT_BOUNDARY");break;}
     if(next_index>=expected_count||n!=expected[next_index]){ok=0;strcpy(end_reason,"SAMPLE_SIZE_CONFLICT");break;}
     const unsigned char *payload=head+20;
     if(!valid_sample(payload,n,ls)||fwrite(payload,1,n,out)!=n){ok=0;break;}
     sz[count++]=n;next_index++;matched_probe++;
    }else if(cls==2){
     uint64_t logical=units[u].base+pos+20;
     wb_audio_observe(audio,start,logical,start+pos+20,n);
     if(audio_clean){uint32_t ai=0;
      if(!wb_audio_match(audio,logical,n,&ai) || (an&&ai!=anext)) {
       audio_clean=0;audio_rejected++;
       audio_reason="AUDIO_SAMPLE_GAP_OR_CONFLICT";
      }
      else {
       if(!an)afirst=ai;
       if(an==acap){size_t nc=acap?acap*2:256;uint32_t *q=realloc(asz,nc*sizeof(*q));if(!q){audio_clean=0;audio_reason="AUDIO_OUT_OF_MEMORY";}else{asz=q;acap=nc;}}
       if(audio_clean&&(fwrite(head+20,1,n,aspool)!=n)){audio_clean=0;audio_reason="AUDIO_SPOOL_WRITE_FAILED";}
       if(audio_clean){asz[an++]=n;anext=ai+1;}
      }
     }
    }
    pos+=20+(uint64_t)n;continue;
   }
   if(!memcmp(head+4,"free",4)||!memcmp(head+4,"skip",4)){
    uint64_t z=b32(head);if(z==1)z=b64(head+8);
    if(z<8||z>UNIT-pos){ok=0;break;}pos+=z;continue;
   }
   if(head[0]==0){
    size_t avail=(size_t)((UNIT-pos)<4096?UNIT-pos:4096);
    size_t z=0;while(z<avail&&!head[z])z++;
    if(z){pos+=z;continue;}
   }
   if(matched_probe>=units[u].probed&&next_index==stop_at){strcpy(end_reason,"FINAL_UNIT_UNRESOLVED_TAIL");break;}
   ok=0;strcpy(end_reason,"UNKNOWN_WITHIN_REQUIRED_UNIT");break;
  }
  if(!ok)break;
  if(matched_probe<units[u].probed|| (u+1<nunits&&next_index!=stop_at)){
   ok=0;strcpy(end_reason,"INCOMPLETE_VERIFIED_UNIT");break;
  }
  processed++;
 }
 /* Append exact-matched AAC samples as one chunk in the same mdat. */
 uint64_t audio_offset=(uint64_t)TELL64(out);
 if(audio_clean&&an){
  if(fflush(aspool)||fseek(aspool,0,SEEK_SET)){audio_clean=0;audio_reason="AUDIO_SPOOL_READ_FAILED";}
  unsigned char copybuf[65536];size_t r;
  while(audio_clean&&(r=fread(copybuf,1,sizeof(copybuf),aspool))>0)
    if(fwrite(copybuf,1,r,out)!=r)audio_clean=0;
  if(ferror(aspool)){audio_clean=0;audio_reason="AUDIO_SPOOL_READ_FAILED";}
 }
 if(!audio_clean && (uint64_t)TELL64(out)!=audio_offset)ok=0;
 int audio_written=audio_clean&&an>0;
 uint64_t a_lead=0, movie_duration=(uint64_t)count*delta;
 if(audio_written){
  uint64_t first_video_time=(uint64_t)units[0].first*delta;
  uint64_t first_audio_time=(uint64_t)afirst*adelta*scale/arate;
  if(first_audio_time<first_video_time){audio_written=0;audio_reason="AUDIO_LEADS_VIDEO_UNSUPPORTED";}
  else {
   a_lead=first_audio_time-first_video_time;
   uint64_t audio_duration=((uint64_t)an*adelta*scale+arate-1)/arate;
   if(a_lead>UINT32_MAX||audio_duration>UINT32_MAX||a_lead+audio_duration>UINT32_MAX){
    audio_written=0;audio_reason="AUDIO_DURATION_UNSUPPORTED";
   }
   else if(a_lead+audio_duration>movie_duration)movie_duration=a_lead+audio_duration;
  }
 }
 if(count==0||!endbox(out,mdat))ok=0;
 if(ok){if(movie_duration>UINT32_MAX||sampleoff>UINT32_MAX)ok=0;}
 if(ok){uint32_t dur=count*delta;uint64_t mo=box(out,"moov");uint64_t x=box(out,"mvhd");put32(out,0);put32(out,0);put32(out,0);put32(out,scale);put32(out,(uint32_t)movie_duration);put32(out,65536);put16(out,256);put16(out,0);put32(out,0);put32(out,0);matrix(out);for(int k=0;k<6;k++)put32(out,0);put32(out,3);ok=endbox(out,x);
 uint64_t tr=box(out,"trak");x=box(out,"tkhd");put32(out,7);put32(out,0);put32(out,0);put32(out,1);put32(out,0);put32(out,dur);put32(out,0);put32(out,0);put16(out,0);put16(out,0);put16(out,0);put16(out,0);matrix(out);put32(out,w<<16);put32(out,h<<16);ok&=endbox(out,x);
 uint64_t md=box(out,"mdia");x=box(out,"mdhd");put32(out,0);put32(out,0);put32(out,0);put32(out,scale);put32(out,dur);put16(out,0x55c4);put16(out,0);ok&=endbox(out,x);
 x=box(out,"hdlr");put32(out,0);put32(out,0);fwrite("vide",1,4,out);put32(out,0);put32(out,0);put32(out,0);fwrite("VideoHandler\0",1,13,out);ok&=endbox(out,x);
 uint64_t mi=box(out,"minf");x=box(out,"vmhd");put32(out,1);put16(out,0);put16(out,0);put16(out,0);put16(out,0);ok&=endbox(out,x);
 uint64_t din=box(out,"dinf");x=box(out,"dref");put32(out,0);put32(out,1);uint64_t url=box(out,"url ");put32(out,1);ok&=endbox(out,url);ok&=endbox(out,x);ok&=endbox(out,din);
 uint64_t st=box(out,"stbl");x=box(out,"stsd");put32(out,0);put32(out,1);uint64_t sample=box(out,"hvc1");for(int k=0;k<6;k++)fputc(0,out);put16(out,1);put16(out,0);put16(out,0);put32(out,0);put32(out,0);put32(out,0);put16(out,w);put16(out,h);put32(out,0x00480000);put32(out,0x00480000);put32(out,0);put16(out,1);for(int k=0;k<32;k++)fputc(0,out);put16(out,24);put16(out,0xffff);fwrite(cfg,1,cl,out);ok&=endbox(out,sample);ok&=endbox(out,x);
 x=box(out,"stts");put32(out,0);put32(out,1);put32(out,count);put32(out,delta);ok&=endbox(out,x);
 x=box(out,"stsc");put32(out,0);put32(out,1);put32(out,1);put32(out,count);put32(out,1);ok&=endbox(out,x);
 x=box(out,"stsz");put32(out,0);put32(out,0);put32(out,count);for(unsigned k=0;k<count;k++)put32(out,sz[k]);ok&=endbox(out,x);
 x=box(out,"stco");put32(out,0);put32(out,1);put32(out,(uint32_t)sampleoff);ok&=endbox(out,x);
 ok&=endbox(out,st);ok&=endbox(out,mi);ok&=endbox(out,md);ok&=endbox(out,tr);
 if(audio_written){
   if(!audio_track(out,audio_entry,audio_entrylen,asz,(unsigned)an,
     arate,adelta,audio_offset,scale,a_lead)){
    ok=0;audio_reason="AUDIO_TRACK_WRITE_FAILED";
   }else audio_reason="AAC_MUXED";
  }
 ok&=endbox(out,mo);
 }
 if(fflush(out)||ferror(out))ok=0;
 if(fclose(out))ok=0;
 wb_audio_end(audio,ok);if(aspool)fclose(aspool);free(asz);free(audio_entry);free(unitbuf);fclose(img);free(cfg);free(sz);free(expected);
 if(!ok){free(units);remove(tmp);if(!strcmp(end_reason,"CANCELLED"))return 9;fprintf(stderr,"MP4 extraction refused: incomplete/invalid samples or unsupported timing.\n");return 5;}
 if(rename(tmp,outpath)){free(units);remove(tmp);return 6;}
 /* A sidecar records what was actually written. A mapping CSV alone does not
  * establish that an AAC track survived the mux validation gate. */
 char infopath[4096];
 if(snprintf(infopath,sizeof(infopath),"%s.recovery_info.csv",outpath)<(int)sizeof(infopath)){
  FILE *info=fopen(infopath,"wb");
  if(info){
   fputs("field,value\n",info);
   fprintf(info,"recording_moov_offset,%" PRIu64 "\n",units[0].owner);
   fprintf(info,"first_physical_anchor,%" PRIu64 "\n",units[0].anchor);
   fprintf(info,"source_units,%u\n",processed);
   fprintf(info,"verified_video_samples,%u\n",count);
   fprintf(info,"video_first_sample_index,%u\n",units[0].first);
   fprintf(info,"video_next_sample_index,%u\n",next_index);
   fprintf(info,"video_expected_total_samples,%u\n",expected_count);
   fprintf(info,"video_duration_seconds,%.6f\n",(double)count*delta/scale);
   fprintf(info,"original_video_track_complete,%s\n",next_index==expected_count&&units[0].first==0?"yes":"no");
   fprintf(info,"video_leading_unrecovered_samples,%u\n",units[0].first);
   fprintf(info,"video_trailing_unrecovered_samples,%u\n",expected_count-next_index);
   fprintf(info,"video_leading_unrecovered_seconds,%.6f\n",(double)units[0].first*delta/scale);
   fprintf(info,"video_trailing_unrecovered_seconds,%.6f\n",(double)(expected_count-next_index)*delta/scale);
   fprintf(info,"video_recovered_sample_fraction,%.8f\n",(double)count/expected_count);
   fprintf(info,"original_video_expected_seconds,%.6f\n",(double)expected_count*delta/scale);
   fprintf(info,"recovery_scope,%s\n",units[0].first==0&&next_index==expected_count?"VIDEO_SAMPLES_ACCOUNTED":"BOUNDED_CONTIGUOUS_PARTIAL");
   fprintf(info,"aac_samples_matched_in_run,%zu\n",an);
   fprintf(info,"aac_mapping_conflicts,%u\n",audio_rejected);
   fprintf(info,"aac_track_muxed,%s\n",audio_written?"yes":"no");
   fprintf(info,"aac_result,%s\n",audio_reason);
   fprintf(info,"camera_microphone_capability,UNKNOWN\n");
   fprintf(info,"audio_required_for_video_recovery,no\n");
   fprintf(info,"aac_physical_samples_observed,%s\n",an>0?"yes":"no_verified_sequence");
   fprintf(info,"audio_interpretation,%s\n",audio_written?"RECOVERED_AAC_TRACK":
     (an>0?"AAC_EVIDENCE_PRESENT_NOT_MUXED":"AUDIO_NOT_ESTABLISHED"));
   fprintf(info,"audio_absence_is_recovery_failure,no\n");
   fprintf(info,"source_date_required,no\n");
   fclose(info);
  }
 }
 printf("Exported %u verified video samples across %u consecutive units; duration %.3f seconds. %s\n",count,processed,(double)count*delta/scale,next_index==expected_count&&units[0].first==0?"VIDEO_TRACK_COMPLETE_AUDIO_UNVERIFIED":"PARTIAL_RECORDING");free(units);return 0;
}

int wb_export_mp4(const char *img,const char *csv,const char *out,uint64_t anchor){return wb_export_mp4_run(img,csv,out,anchor,NULL,NULL);}

uint64_t wb_run_canonical_anchor(const char *csv,uint64_t anchor){WBUnit *u=NULL;unsigned n=load_run(csv,anchor,&u);uint64_t first=n?u[0].anchor:0;free(u);return first;}

int wb_export_mp4_cancellable(const char *img,const char *csv,const char *out,uint64_t anchor,int (*cancel)(void*),void *ctx){return wb_export_mp4_run(img,csv,out,anchor,cancel,ctx);}
