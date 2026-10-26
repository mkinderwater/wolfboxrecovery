#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "ownership.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef _WIN32
#define WB_SEEK _fseeki64
#else
#define WB_SEEK fseeko
#endif
static uint32_t r32(const unsigned char*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t r64(const unsigned char*p){return (uint64_t)r32(p)<<32|r32(p+4);}
typedef struct {const unsigned char*p;size_t n,h;} Box;
static int box_at(const unsigned char*b,size_t n,size_t pos,Box*out){
 if(pos>n||n-pos<8)return 0;
 uint64_t sz=r32(b+pos);size_t head=8;
 if(sz==1){if(n-pos<16)return 0;sz=r64(b+pos+8);head=16;}
 if(sz==0)sz=n-pos;
 if(sz<head||sz>n-pos||sz>SIZE_MAX)return 0;
 *out=(Box){b+pos,(size_t)sz,head};return 1;
}
static int child(Box p,const char tag[4],Box*out){
 for(size_t i=p.h;i+8<=p.n;){Box a;if(!box_at(p.p,p.n,i,&a))return 0;
  if(!memcmp(a.p+4,tag,4)){*out=a;return 1;}i+=a.n;}return 0;
}
static int nested(Box p,const char*path,Box*out){for(size_t j=0;path[j];j+=4)if(!child(p,path+j,&p))return 0;*out=p;return 1;}
typedef struct{uint64_t moov;uint32_t track,count;uint32_t *sizes;uint64_t *logical;} Track;
typedef struct {uint32_t *video;uint64_t *physical_payload;unsigned nvideo,capacity,naudio,nmeta;uint64_t zeros,free_bytes,walked,first_video_clock,last_video_clock,previous_video_clock;unsigned video_clock_count,clock_reversals;char stop[48];} Probe;
typedef struct{uint64_t offset;uint32_t length; Probe cached; uint64_t next; unsigned cached_done,started;} Anchor;
typedef struct{uint64_t key;uint32_t owner,start;} Finger;
struct WBIndex{Track*tracks;size_t ntracks,tcap;Anchor*anchors;size_t nanchors,acap;Finger*fps;size_t nfps;size_t active_first;size_t cached_used;size_t fallback_used;};
WBIndex *wb_owner_new(void){return calloc(1,sizeof(WBIndex));}
void wb_owner_free(WBIndex*i){if(!i)return;for(size_t k=0;k<i->ntracks;k++){free(i->tracks[k].sizes);free(i->tracks[k].logical);}free(i->tracks);for(size_t a=0;a<i->nanchors;a++){free(i->anchors[a].cached.video);free(i->anchors[a].cached.physical_payload);}free(i->anchors);free(i->fps);free(i);}
static int enlarge(void**ptr,size_t*cap,size_t count,size_t width){if(count<*cap)return 1;size_t next=*cap?*cap*2:128;if(next<count+1)next=count+1;if(next>SIZE_MAX/width)return 0;void*p=realloc(*ptr,next*width);if(!p)return 0;*ptr=p;*cap=next;return 1;}
static uint64_t *track_positions(Box trak,const uint32_t *sizes,uint32_t count);
int wb_owner_add_moov(WBIndex*i,const unsigned char*b,size_t length,uint64_t phys){
 if(!i)return -1;
 Box moov;if(!box_at(b,length,0,&moov)||moov.n!=length||memcmp(b+4,"moov",4))return 0;
 int added=0;unsigned trackno=0;
 for(size_t pos=moov.h;pos+8<=moov.n;){Box trak;if(!box_at(moov.p,moov.n,pos,&trak))break;
  if(!memcmp(trak.p+4,"trak",4)){
   trackno++;Box hdlr,stsz;
   if(nested(trak,"mdiahdlr",&hdlr)&&hdlr.n>=hdlr.h+12&&!memcmp(hdlr.p+hdlr.h+8,"vide",4)
    &&nested(trak,"mdiaminfstblstsz",&stsz)&&stsz.n>=stsz.h+12){
    const unsigned char*p=stsz.p+stsz.h;uint32_t fixed=r32(p+4),count=r32(p+8);
    if(count>=4&&count<=2000000&&(fixed||count<=(stsz.n-stsz.h-12)/4)){
     uint32_t*s=malloc((size_t)count*sizeof(*s));if(!s)return -1;
     for(uint32_t a=0;a<count;a++)s[a]=fixed?fixed:r32(p+12+(size_t)a*4);
     if(!enlarge((void**)&i->tracks,&i->tcap,i->ntracks,sizeof(Track))){free(s);return -1;}
     uint64_t*logical=track_positions(trak,s,count);
     i->tracks[i->ntracks++]=(Track){phys,trackno,count,s,logical};added++;
    }
   }
  }
  pos+=trak.n;
 }
 return added;
}
int wb_owner_add_anchor(WBIndex*i,uint64_t off,uint32_t sz){if(!i||sz<8)return 0;
 if(!enlarge((void**)&i->anchors,&i->acap,i->nanchors,sizeof(Anchor)))return 0;
 i->anchors[i->nanchors++]=(Anchor){.offset=off,.length=sz,.next=off+sz};return 1;
}
/* Decode stsc/stco into exact per-sample logical MP4 positions.
 * Invalid or unsupported tables remain unmapped, never inferred. */
static uint64_t *track_positions(Box trak,const uint32_t *sizes,uint32_t count){
 Box stsc,offs;int co64=0;
 if(!nested(trak,"mdiaminfstblstsc",&stsc))return NULL;
 if(!nested(trak,"mdiaminfstblstco",&offs)){
  if(!nested(trak,"mdiaminfstblco64",&offs))return NULL;
  co64=1;
 }
 if(stsc.n<stsc.h+8||offs.n<offs.h+8)return NULL;
 const unsigned char *sc=stsc.p+stsc.h,*oc=offs.p+offs.h;
 uint32_t ne=r32(sc+4),nc=r32(oc+4);
 if(!ne||!nc||ne>(stsc.n-stsc.h-8)/12||nc>(offs.n-offs.h-8)/(co64?8:4))return NULL;
 uint64_t*pos=malloc((size_t)count*sizeof(uint64_t));if(!pos)return NULL;
 uint32_t samp=0,e=0;
 for(uint32_t c=1;c<=nc;c++){
  while(e+1<ne && r32(sc+8+(size_t)(e+1)*12)<=c)e++;
  const unsigned char*ent=sc+8+(size_t)e*12;
  uint32_t first=r32(ent),spc=r32(ent+4),desc=r32(ent+8);
  if(!first||first>c||!spc||!desc||samp>count||spc>count-samp){free(pos);return NULL;}
  uint64_t base=co64?r64(oc+8+(size_t)(c-1)*8):r32(oc+8+(size_t)(c-1)*4);
  for(uint32_t j=0;j<spc;j++){
   if(UINT64_MAX-base<sizes[samp]){free(pos);return NULL;}
   pos[samp]=base;base+=sizes[samp];samp++;
  }
 }
 if(samp!=count)return free(pos),NULL;
 return pos;
}
static uint64_t fp4(const uint32_t*s){uint64_t x=0xcbf29ce484222325ULL;
 for(int j=0;j<4;j++){uint32_t y=s[j];for(int k=0;k<4;k++){x^=(unsigned char)(y>>(8*k));x*=0x100000001b3ULL;}}
 return x;
}
static int cmpfinger(const void*a,const void*b){uint64_t x=((const Finger*)a)->key,y=((const Finger*)b)->key;return (x>y)-(x<y);}
static int build_fps(WBIndex*i){size_t count=0;
 for(size_t t=0;t<i->ntracks;t++){if(i->tracks[t].count-3>SIZE_MAX-count)return 0;count+=i->tracks[t].count-3;}
 if(count>SIZE_MAX/sizeof(Finger))return 0;
 i->fps=malloc(count*sizeof(Finger));if(!i->fps)return 0;
 for(size_t t=0;t<i->ntracks;t++){Track*tr=&i->tracks[t];for(uint32_t j=0;j+3<tr->count;j++)i->fps[i->nfps++]=(Finger){fp4(tr->sizes+j),(uint32_t)t,j};}
 qsort(i->fps,i->nfps,sizeof(Finger),cmpfinger);return 1;
}
static size_t lowerfp(const WBIndex*i,uint64_t key){size_t l=0,r=i->nfps;while(l<r){size_t m=l+(r-l)/2;if(i->fps[m].key<key)l=m+1;else r=m;}return l;}
/* v0.6: bounded physical traversal. Every skipped byte is classified: either
 * verified zero padding or a structurally valid MP4 free/skip box. The scan
 * never leaps across unknown nonzero bytes, never fabricates samples and
 * cannot cross the 20 MiB unit boundary. */
#define WB_UNIT_BYTES (20ULL*1024*1024)
#define WB_MAX_PAYLOAD (12U*1024*1024)
/* Limit memory per anchor while allowing the complete usual 20 MiB unit.
 * Dynamic arrays avoid reserving thousands of unused entries per anchor. */
#define WB_MAX_SAMPLES_PER_UNIT 8192U
static int append_video(Probe *q,uint32_t sz,uint64_t physical,uint64_t clock){
 if(q->nvideo==WB_MAX_SAMPLES_PER_UNIT)return 0;
 if(q->nvideo==q->capacity){
  unsigned next=q->capacity?q->capacity*2:64;
  if(next>WB_MAX_SAMPLES_PER_UNIT)next=WB_MAX_SAMPLES_PER_UNIT;
  uint32_t *sizes=realloc(q->video,(size_t)next*sizeof(*sizes));
  if(!sizes)return 0;
  q->video=sizes;
  uint64_t *positions=realloc(q->physical_payload,(size_t)next*sizeof(*positions));
  if(!positions)return 0;
  q->physical_payload=positions;q->capacity=next;
 }
 q->video[q->nvideo]=sz;q->physical_payload[q->nvideo]=physical;q->nvideo++;
 if(!q->video_clock_count)q->first_video_clock=clock;
 else if(clock<q->previous_video_clock)q->clock_reversals++;
 q->previous_video_clock=clock;q->last_video_clock=clock;q->video_clock_count++;
 return 1;
}


static int getat(FILE*f,uint64_t pos,unsigned char*b,size_t n){
 if(pos>INT64_MAX||WB_SEEK(f,(int64_t)pos,SEEK_SET))return 0;
 return fread(b,1,n,f)==n;
}
static Probe probe(FILE*f,uint64_t start,uint32_t first_size,uint64_t image_size){
 Probe q={0};strcpy(q.stop,"UNIT_END");
 if(start>UINT64_MAX-first_size){strcpy(q.stop,"BAD_START");return q;}
 uint64_t pos=start+first_size, end;
 /* A physical unit might not be globally aligned: the target is at most
    20 MiB from its own tbgp start, not an absolute card-grid position. */
 end=start+WB_UNIT_BYTES;
 if(end<start||end>image_size)end=image_size;
 while(pos+20<=end){
  unsigned char head[20];if(!getat(f,pos,head,20)){strcpy(q.stop,"READ_END");break;}
  uint32_t cls=r32(head),length=r32(head+16);
  if(cls>=1&&cls<=3&&length>=1&&length<=WB_MAX_PAYLOAD&&
      (uint64_t)length<=end-pos-20){
   if(cls==1){if(!append_video(&q,length,pos+20,r64(head+8))){strcpy(q.stop,"SAMPLE_LIMIT_OR_OOM");break;}}
   else if(cls==2)q.naudio++;else q.nmeta++;
   pos+=20+(uint64_t)length;continue;
  }
  if(!memcmp(head+4,"free",4)||!memcmp(head+4,"skip",4)){
   uint64_t z=r32(head);size_t h=8;
   if(z==1){z=r64(head+8);h=16;}
   if(z<h||z>end-pos){strcpy(q.stop,"INVALID_FREE");break;}
   q.free_bytes+=z;pos+=z;continue;
  }
  if(head[0]==0){
   /* Scan bounded zero padding in fixed-size blocks to avoid false wrapper
    classification and O(bytes) individual file seeks. */
   unsigned char block[4096];size_t n=(size_t)(end-pos<sizeof(block)?end-pos:sizeof(block));
   if(!getat(f,pos,block,n)){strcpy(q.stop,"READ_END");break;}
   size_t z=0;while(z<n&&block[z]==0){
    if(z+8<=n && (!memcmp(block+z+4,"free",4)||!memcmp(block+z+4,"skip",4)) && r32(block+z)>=8)break;
    z++;
   }
   if(z){pos+=z;q.zeros+=z;continue;}
  }
  strcpy(q.stop,"UNKNOWN_BYTES");break;
 }
 q.walked=pos-(start+first_size);return q;
}

/* Stage-1 sequential index: parse the bytes already held by the scanner.
 * This is deliberately a bounded prefix cache, NOT a full-unit proof.
 * A candidate remains open across 4 MiB scan-block boundaries.
 * If evidence cannot be classified, finalize with the same stop reason
 * as the legacy probe. An unfinished candidate falls back to legacy I/O. */
void wb_owner_stage1_feed(WBIndex *idx,const unsigned char *b,size_t n,
                          uint64_t block_off,size_t advance){
 if(!idx||!b)return;
 uint64_t limit=block_off+advance;
 while(idx->active_first<idx->nanchors && idx->anchors[idx->active_first].cached_done)idx->active_first++;
 for(size_t a=idx->active_first;a<idx->nanchors;a++){
  Anchor *an=&idx->anchors[a];
  if(an->cached_done)continue;
  uint64_t pos=an->next;
  uint64_t end=an->offset+WB_UNIT_BYTES;
  if(end<an->offset)end=UINT64_MAX;
  if(pos<block_off)continue;
  if(pos>=limit)continue;
  Probe *q=&an->cached;
  if(!an->started){memset(q,0,sizeof(*q));strcpy(q->stop,"UNIT_END");an->started=1;}
  while(pos<limit && pos+20<=end){
   uint64_t rel=pos-block_off;
   if(rel>n || n-rel<20)break;
   const unsigned char *h=b+(size_t)rel;
   uint32_t cls=r32(h),sz=r32(h+16);
   if(cls>=1&&cls<=3&&sz>=1&&sz<=WB_MAX_PAYLOAD && (uint64_t)sz<=end-pos-20){
    if(cls==1){if(!append_video(q,sz,pos+20,r64(h+8))){strcpy(q->stop,"SAMPLE_LIMIT_OR_OOM");an->cached_done=1;break;}}
    else if(cls==2)q->naudio++;else q->nmeta++;
    pos+=20+(uint64_t)sz;
    continue;
   }
   if(!memcmp(h+4,"free",4)||!memcmp(h+4,"skip",4)){
    uint64_t z=r32(h);size_t head=8;if(z==1){z=r64(h+8);head=16;}
    if(z<head||z>end-pos){strcpy(q->stop,"INVALID_FREE");an->cached_done=1;break;}
    q->free_bytes+=z;pos+=z;continue;
   }
   if(h[0]==0){
    size_t available=(size_t)(limit-pos);if(available>n-rel)available=n-(size_t)rel;
    if(available>end-pos)available=(size_t)(end-pos);
    if(available>4096)available=4096;
    size_t z=0;
    while(z<available&&h[z]==0){
     if(z+8<=available&&(!memcmp(h+z+4,"free",4)||!memcmp(h+z+4,"skip",4))&&r32(h+z)>=8)break;
     z++;
    }
    if(z){pos+=z;q->zeros+=z;continue;}
   }
   strcpy(q->stop,"UNKNOWN_BYTES");an->cached_done=1;break;
  }
  an->next=pos;
  q->walked=pos-(an->offset+an->length);
  if(pos+20>end)an->cached_done=1;
 }
}
size_t wb_owner_stage1_ready(const WBIndex *idx){
 size_t n=0;if(idx)for(size_t a=0;a<idx->nanchors;a++)n+=idx->anchors[a].cached_done;
 return n;
}

int wb_owner_resolve_progress(WBIndex*i,const char*path,FILE*out,FILE*placements,FILE*diagnostics,FILE*samples,FILE*candidates,FILE*orphan,int(*stop)(void*),void*ctx,WBMatchProgress progress){
 if(!i||!out||!placements)return -1;
 if(diagnostics)fputs("tbgp_offset,physical_unit_end,physical_stop_offset,video_wrappers,audio_wrappers,metadata_wrappers,stop_reason,full_video_owners,best_partial_samples,best_partial_owner,best_partial_first_sample,best_partial_ties,decision,aac_gated_video\n",diagnostics);
 if(samples)fputs("tbgp_offset,physical_payload_offset,bytes,video_ordinal,raw_moov,track,video_sample_index,association_status,unit_stop_reason\n",samples);
 if(orphan)fputs("tbgp_offset,video_samples,first_video_clock_us,last_video_clock_us,clock_reversals,first_payload_offset,last_payload_offset,last_payload_bytes,stop_reason,owner_decision,interpretation\n",orphan);
 if(candidates)fputs("tbgp_offset,observed_video_samples,raw_moov,track,first_sample,matching_prefix_samples,first_mismatch_ordinal,observed_size,expected_size,candidate_kind,diagnostic_only\n",candidates);
 fputs("tbgp_offset,moov_offset,track,first_video_sample,logical_unit_base,matched_video_samples,logical_status,stop_reason\n",placements);
 fputs("tbgp_offset,raw_moov,track,video_sample_index,matched_video_wrappers,status,audio_wrappers,metadata_wrappers,zero_padding_bytes,free_box_bytes,walked_bytes,stop_reason\n",out);
 if(!i->nanchors)return 0;
 if(i->ntracks && !build_fps(i))return -2;
 FILE*f=fopen(path,"rb");if(!f)return -3; if(WB_SEEK(f,0,SEEK_END)){fclose(f);return -3;} uint64_t image_size;
#ifdef _WIN32
 image_size=(uint64_t)_ftelli64(f);
#else
 image_size=(uint64_t)ftello(f);
#endif
 int unique=0;
 for(size_t a=0;a<i->nanchors;a++){
  if(stop&&stop(ctx))break;
  Anchor anchor=i->anchors[a];
  int from_index=anchor.cached_done && !getenv("WB_STAGE1_LEGACY");
  Probe q=from_index?anchor.cached:probe(f,anchor.offset,anchor.length,image_size);
  if(from_index)i->cached_used++;else i->fallback_used++;
  int n=(int)q.nvideo;uint32_t*seq=q.video;
  uint64_t stopoff=anchor.offset+anchor.length+q.walked;
  if(n<4){
   if(orphan)fprintf(orphan,"%"PRIu64",%d,%"PRIu64",%"PRIu64",%u,%"PRIu64",%"PRIu64",%u,%s,INSUFFICIENT_VIDEO,UNVERIFIED\n",
      anchor.offset,n,q.first_video_clock,q.last_video_clock,q.clock_reversals,n?q.physical_payload[0]:0,n?q.physical_payload[n-1]:0,n?q.video[n-1]:0,q.stop);
   if(diagnostics)fprintf(diagnostics,"%"PRIu64",%"PRIu64",%"PRIu64",%d,%u,%u,%s,0,0,,,0,INSUFFICIENT_VIDEO,NO\n",anchor.offset,(uint64_t)(anchor.offset+WB_UNIT_BYTES),stopoff,n,q.naudio,q.nmeta,q.stop);
   if(samples)for(unsigned k=0;k<q.nvideo;k++)fprintf(samples,"%"PRIu64",%"PRIu64",%u,%u,,,UNASSOCIATED_INSUFFICIENT_VIDEO,%s\n",anchor.offset,q.physical_payload[k],q.video[k],k,q.stop);
   if(!from_index){free(q.video);free(q.physical_payload);}if(progress)progress(ctx,a+1,i->nanchors,(size_t)unique);fprintf(out,"%"PRIu64",,,,%d,INSUFFICIENT_VIDEO,%u,%u,%"PRIu64",%"PRIu64",%"PRIu64",%s\n",anchor.offset,n,q.naudio,q.nmeta,q.zeros,q.free_bytes,q.walked,q.stop);continue;}
  uint64_t key=fp4(seq);size_t begin=lowerfp(i,key);unsigned accepted=0;Finger winner={0};unsigned best=0;
  unsigned longest=0,partial_ties=0; Finger partial={0};
  for(size_t j=begin;j<i->nfps&&i->fps[j].key==key;j++){
   Finger entry=i->fps[j];Track*t=&i->tracks[entry.owner];uint32_t pos=entry.start;
   if(pos>=t->count)continue;
   unsigned matched=0;while(matched<(unsigned)n && pos+matched<t->count&&t->sizes[pos+matched]==seq[matched])matched++;
   if(matched>longest){longest=matched;partial=entry;partial_ties=1;}
   else if(matched==longest)partial_ties++;
   if(matched==(unsigned)n){accepted++;winner=entry;best=matched;}
  }
  /* Preserve candidate disagreement without changing strict acceptance.
   * A four-size fingerprint is only a search accelerator, never a proof that
   * other physical candidates do not exist. The fallback examines at most
   * two million sample positions and records this coverage limit. */
  if(candidates && accepted!=1){
   unsigned emitted=0;
   for(size_t j=begin;j<i->nfps&&i->fps[j].key==key;j++){
    Finger e=i->fps[j];Track*t=&i->tracks[e.owner];
    unsigned m=0;while(m<(unsigned)n && e.start+m<t->count && t->sizes[e.start+m]==seq[m])m++;
    if(m<4 || emitted>=32)continue;
    unsigned obs=m<(unsigned)n?seq[m]:0, exp=e.start+m<t->count?t->sizes[e.start+m]:0;
    fprintf(candidates,"%"PRIu64",%d,%"PRIu64",%u,%u,%u,%u,%u,%u,FOUR_SIZE_PREFIX,YES\n",anchor.offset,n,t->moov,t->track,e.start,m,m,obs,exp);
    emitted++;
   }
   if(!emitted){
    uint64_t checked=0;unsigned strongest=0;
    for(size_t tno=0;tno<i->ntracks && checked<2000000;tno++){
     Track*t=&i->tracks[tno];
     for(uint32_t z=0;z<t->count && checked<2000000;z++,checked++){
      if(t->sizes[z]!=seq[0])continue;
      unsigned m=1;
      while(m<(unsigned)n && m<32 && z+m<t->count && t->sizes[z+m]==seq[m])m++;
      if(m<2)continue;
      if(m>strongest){strongest=m;emitted=0;}
      if(m==strongest && emitted<16){
       unsigned obs=m<(unsigned)n?seq[m]:0,exp=z+m<t->count?t->sizes[z+m]:0;
       fprintf(candidates,"%"PRIu64",%d,%"PRIu64",%u,%u,%u,%u,%u,%u,TWO_SIZE_SEARCH_BOUNDED,YES\n",anchor.offset,n,t->moov,t->track,z,m,m,obs,exp);emitted++;
      }
     }
    }
    if(!emitted)fprintf(candidates,"%"PRIu64",%d,,,,0,0,0,0,NO_PREFIX_CANDIDATE_WITHIN_SEARCH_CAP_2000000,YES\n",anchor.offset,n);
   }
  }
  const char *decision=accepted==1?"UNIQUE_FULL_VIDEO_MATCH":accepted>1?"AMBIGUOUS_FULL_VIDEO_MATCH":longest>=4?"PARTIAL_VIDEO_PREFIX_ONLY":"NO_VIDEO_OWNER";
  if(orphan && accepted!=1)fprintf(orphan,"%"PRIu64",%d,%"PRIu64",%"PRIu64",%u,%"PRIu64",%"PRIu64",%u,%s,%s,UNVERIFIED\n",
      anchor.offset,n,q.first_video_clock,q.last_video_clock,q.clock_reversals,q.physical_payload[0],q.physical_payload[n-1],q.video[n-1],q.stop,decision);
  if(diagnostics){
   uint64_t pm=longest?i->tracks[partial.owner].moov:0;
   fprintf(diagnostics,"%"PRIu64",%"PRIu64",%"PRIu64",%d,%u,%u,%s,%u,%u,",anchor.offset,(uint64_t)(anchor.offset+WB_UNIT_BYTES),stopoff,n,q.naudio,q.nmeta,q.stop,accepted,longest);
   if(longest)fprintf(diagnostics,"%"PRIu64",%u",pm,partial.start);
   else fputc(',',diagnostics);
   fprintf(diagnostics,",%u,%s,NO\n",partial_ties,decision);
  }
  /* Re-check logical displacement for diagnostic labels independently of
   * accepting the unique size sequence in the legacy inventory. */
  int mapping_consistent=0,mapping_aligned=0;
  if(accepted==1){
   Track *t=&i->tracks[winner.owner];
   if(t->logical){
    uint64_t base=0;mapping_consistent=1;
    for(unsigned k=0;k<q.nvideo;k++){
     uint64_t rel=q.physical_payload[k]-anchor.offset;
     uint64_t logical=t->logical[winner.start+k];
     if(logical<rel){mapping_consistent=0;break;}
     if(k==0)base=logical-rel;
     else if(base!=logical-rel){mapping_consistent=0;break;}
    }
    if(mapping_consistent)mapping_aligned=(base%WB_UNIT_BYTES)==0;
   }
  }
  if(samples)for(unsigned k=0;k<q.nvideo;k++){
   fprintf(samples,"%"PRIu64",%"PRIu64",%u,%u,",anchor.offset,q.physical_payload[k],q.video[k],k);
   if(accepted==1){Track *t=&i->tracks[winner.owner];fprintf(samples,"%"PRIu64",%u,%u,%s,%s\n",t->moov,t->track,winner.start+k,mapping_consistent&&mapping_aligned?"VIDEO_LOGICAL_BASE_ALIGNED":mapping_consistent?"VIDEO_LOGICAL_BASE_UNALIGNED":"VIDEO_SIZE_MATCH_LOGICAL_UNVERIFIED",q.stop);}
   else if(longest && partial_ties==1 && k<longest){Track*t=&i->tracks[partial.owner];fprintf(samples,"%"PRIu64",%u,%u,PARTIAL_CANDIDATE_UNVERIFIED,%s\n",t->moov,t->track,partial.start+k,q.stop);}
   else fprintf(samples,",,,UNASSOCIATED_OR_AMBIGUOUS,%s\n",q.stop);
  }
  if(accepted==1){Track*t=&i->tracks[winner.owner];
   /* Every observed sample must agree on the same physical-to-logical
    * displacement. A unique size sequence alone is insufficient. */
   if(t->logical){
    uint64_t base=0;int consistent=1,aligned=0;
    for(unsigned k=0;k<q.nvideo;k++){
     uint64_t rel=q.physical_payload[k]-anchor.offset;
     uint64_t logical=t->logical[winner.start+k];
     if(logical<rel){consistent=0;break;}
     if(k==0)base=logical-rel;
     else if(base!=logical-rel){consistent=0;break;}
    }
    aligned=(base%WB_UNIT_BYTES)==0;
    fprintf(placements,"%"PRIu64",%"PRIu64",%u,%u,%"PRIu64",%u,%s,%s\n",anchor.offset,t->moov,t->track,winner.start,base,q.nvideo,
       !consistent?"LOGICAL_CONFLICT":aligned?"ALIGNED_UNIQUE_PREFIX":"UNALIGNED_UNVERIFIED",q.stop);
   }else fprintf(placements,"%"PRIu64",%"PRIu64",%u,%u,,%u,NO_VALID_CHUNK_TABLE,%s\n",anchor.offset,t->moov,t->track,winner.start,q.nvideo,q.stop);
   fprintf(out,"%"PRIu64",%"PRIu64",%u,%u,%u,UNIQUE_BOUNDED_PREFIX,%u,%u,%"PRIu64",%"PRIu64",%"PRIu64",%s\n",anchor.offset,t->moov,t->track,winner.start,best,q.naudio,q.nmeta,q.zeros,q.free_bytes,q.walked,q.stop);unique++;}
  else {fprintf(placements,"%"PRIu64",,,,,,%s,%s\n",anchor.offset,accepted?"AMBIGUOUS":"NO_VERIFIED_OWNER",q.stop);
   fprintf(out,"%"PRIu64",,,,,%s,%u,%u,%"PRIu64",%"PRIu64",%"PRIu64",%s\n",anchor.offset,accepted?"AMBIGUOUS":"NO_VERIFIED_OWNER",q.naudio,q.nmeta,q.zeros,q.free_bytes,q.walked,q.stop);}
  if(!from_index){free(q.video);free(q.physical_payload);}
  if(progress)progress(ctx,a+1,i->nanchors,(size_t)unique);
 }
 fclose(f);return unique;
}
int wb_owner_resolve(WBIndex*i,const char*path,FILE*out,FILE*placements,FILE*diagnostics,FILE*samples,FILE*candidates,FILE*orphan,int(*stop)(void*),void*ctx){
 return wb_owner_resolve_progress(i,path,out,placements,diagnostics,samples,candidates,orphan,stop,ctx,NULL);
}

void wb_owner_stage1_usage(const WBIndex *idx,size_t *indexed,size_t *fallback){
 if(indexed)*indexed=idx?idx->cached_used:0;
 if(fallback)*fallback=idx?idx->fallback_used:0;
}
