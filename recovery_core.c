#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "recovery_core.h"
#include "sample_tables.h"
#include "ownership.h"
#include "unit_runs.h"
#include "fat_geometry.h"
#include "jpeg_carve.h"
#include "orphan_hevc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#define FSEEK _fseeki64
#define FTELL _ftelli64
#define SEP '\\'
#else
#include <sys/stat.h>
#define FSEEK fseeko
#define FTELL ftello
#define SEP '/'
#endif
static uint32_t be32(const unsigned char *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint64_t be64(const unsigned char *p){return ((uint64_t)be32(p)<<32)|be32(p+4);}
static void csv_s(FILE *f,const char *s){fputc('"',f);for(;*s;s++){if(*s=='"')fputc('"',f);if(*s=='\n'||*s=='\r')fputc(' ',f);else fputc(*s,f);}fputc('"',f);}
/* Rank missing-owner anchors using observed wrapper counts only.
 * This report is a triage list, never a recovery or ownership input. */
static void write_unmapped_priorities(const char *dir){
 char input[4096],output[4096],line[4096];
 snprintf(input,sizeof(input),"%s%cassociation_decisions.csv",dir,SEP);
 snprintf(output,sizeof(output),"%s%cunmapped_priorities.csv",dir,SEP);
 FILE *in=fopen(input,"rb"),*out=fopen(output,"wb");
 if(!out){if(in)fclose(in);return;}
 fputs("tbgp_offset,video_wrappers,audio_wrappers,metadata_wrappers,stop_reason,decision,triage_priority,interpretation\n",out);
 if(!in){fclose(out);return;}
 fgets(line,sizeof(line),in);
 while(fgets(line,sizeof(line),in)){
  char *cells[16]={0};unsigned n=0;cells[n++]=line;
  for(char *c=line;*c && n<16;c++)if(*c==','){*c=0;cells[n++]=c+1;}
  if(n<13)continue;
  if(!strstr(cells[12],"NO_VIDEO_OWNER") && !strstr(cells[12],"PARTIAL_VIDEO_PREFIX_ONLY") && !strstr(cells[12],"INSUFFICIENT_VIDEO") && !strstr(cells[12],"AMBIGUOUS_FULL_VIDEO_MATCH"))continue;
  unsigned v=(unsigned)strtoul(cells[3],NULL,10);
  const char *priority=v>=128?"HIGH":v>=4?"MEDIUM":"LOW";
  fprintf(out,"%s,%u,%s,%s,%s,%s,%s,UNRESOLVED_NOT_OVERWRITTEN\n",cells[0],v,cells[4],cells[5],cells[6],cells[12],priority);
 }
 fclose(in);fclose(out);
}
/* Candidate joins never modify accepted ownership or export eligibility. */
static void write_orphan_joins(const char *dir){
 typedef struct {uint64_t off,first,last;unsigned n,reversals;char decision[64];} Item;
 char p[4096],line[2048];snprintf(p,sizeof(p),"%s%corphan_units.csv",dir,SEP);
 FILE *in=fopen(p,"rb");Item *a=NULL;size_t n=0,cap=0;
 if(in){fgets(line,sizeof(line),in);while(fgets(line,sizeof(line),in)){
  Item x={0};unsigned lastbytes=0;uint64_t firstpos=0,lastpos=0;char stop[64]={0};
  if(sscanf(line,"%"SCNu64",%u,%"SCNu64",%"SCNu64",%u,%"SCNu64",%"SCNu64",%u,%63[^,],%63[^,]",
    &x.off,&x.n,&x.first,&x.last,&x.reversals,&firstpos,&lastpos,&lastbytes,stop,x.decision)!=10)continue;
  if(n==cap){size_t c=cap?cap*2:64;Item *b=realloc(a,c*sizeof(*a));if(!b)break;a=b;cap=c;}a[n++]=x;
 }fclose(in);}
 snprintf(p,sizeof(p),"%s%corphan_join_candidates.csv",dir,SEP);FILE *out=fopen(p,"wb");
 if(out){fputs("left_anchor,right_anchor,left_samples,right_samples,left_last_clock_us,right_first_clock_us,clock_gap_us,clock_reversals,join_class,interpretation\n",out);
  for(size_t i=0;i<n;i++)for(size_t j=0;j<n;j++){
   if(i==j||!a[i].n||!a[j].n||a[i].reversals||a[j].reversals||!a[i].last||!a[j].first)continue;
   if(a[j].first<=a[i].last)continue;
   uint64_t d=a[j].first-a[i].last;
   /* Only report plausible short clock gaps; do not infer frame count or ownership. */
   if(d>600000000ULL)continue;
   const char *kind=d>=30000 && d<=37000?"CLOCK_ADJACENT":"CLOCK_GAP_CANDIDATE";
   fprintf(out,"%"PRIu64",%"PRIu64",%u,%u,%"PRIu64",%"PRIu64",%"PRIu64",0,%s,UNVERIFIED_NO_HEVC_CROSS_BOUNDARY_PROOF\n",a[i].off,a[j].off,a[i].n,a[j].n,a[i].last,a[j].first,d,kind);
  }fclose(out);
 }free(a);
}
static void mkpath(const char *path){
#ifdef _WIN32
 CreateDirectoryA(path,0);
#else
 mkdir(path,0777);
#endif
}
static int mp4_time(uint64_t raw, char output[40]){
 const uint64_t diff=2082844800ULL;
 if(raw<diff || raw>diff+4102444800ULL){output[0]=0;return 0;}
 time_t unix_t=(time_t)(raw-diff);struct tm *tmv=gmtime(&unix_t);
 if(!tmv){output[0]=0;return 0;}
 strftime(output,40,"%Y-%m-%dT%H:%M:%SZ",tmv);return 1;
}
static int date_ascii(const unsigned char *b,size_t n,size_t i){
 if(i+19>n)return 0; // ISO candidate YYYY-MM-DD HH:MM:SS or T
 for(int j=0;j<19;j++){
  if(j==4||j==7){if(b[i+j]!='-')return 0;}
  else if(j==10){if(b[i+j]!=' '&&b[i+j]!='T')return 0;}
  else if(j==13||j==16){if(b[i+j]!=':')return 0;}
  else if(!isdigit((unsigned char)b[i+j]))return 0;
 }
 int year=(b[i]-'0')*1000+(b[i+1]-'0')*100+(b[i+2]-'0')*10+b[i+3]-'0';
 int month=(b[i+5]-'0')*10+b[i+6]-'0',day=(b[i+8]-'0')*10+b[i+9]-'0';
 int h=(b[i+11]-'0')*10+b[i+12]-'0',mi=(b[i+14]-'0')*10+b[i+15]-'0',s=(b[i+17]-'0')*10+b[i+18]-'0';
 return year>=2010 && year<=2099 && month>=1&&month<=12&&day>=1&&day<=31&&h<=23&&mi<=59&&s<=60;
}
typedef struct {FILE *fields,*strings,*records;uint64_t moov;WBEvent ev;void *ctx;WBProgress *p;} Parse;

/* Non-authoritative recording inventory: parse only verified box boundaries.
 * Counts describe surviving metadata, not recovery success. */
static void inventory_moov(FILE *o,const unsigned char *b,size_t n,uint64_t off){
 uint32_t tracks=0, video=0,audio=0,meta=0;uint64_t v_samples=0,a_samples=0;
 for(size_t i=8;i+8<=n;){
  uint64_t sz=be32(b+i);size_t head=8;
  if(sz==1 && i+16<=n){sz=be64(b+i+8);head=16;}
  if(!sz)sz=n-i;
  if(sz<head||sz>n-i)break;
  if(!memcmp(b+i+4,"trak",4)){
   tracks++;int handler=0;uint64_t samples=0;
   /* Read only nested well-formed atom trees, no byte-pattern scanning across payloads. */
   size_t stack_end[16]={0},at=i+head,end=i+(size_t)sz;int depth=0;
   while(at+8<=end){
    uint64_t z=be32(b+at);size_t h=8;
    if(z==1 && at+16<=end){z=be64(b+at+8);h=16;}
    if(z==0)z=end-at;
    if(z<h || z>end-at){if(depth){at=end;end=stack_end[--depth];continue;}break;}
    const unsigned char *tag=b+at+4;
    if(!memcmp(tag,"hdlr",4)&&z>=h+12){const unsigned char *ty=b+at+h+8;
     if(!memcmp(ty,"vide",4))handler=1;else if(!memcmp(ty,"soun",4))handler=2;else if(!memcmp(ty,"meta",4))handler=3;}
    if(!memcmp(tag,"stsz",4)&&z>=h+12){const unsigned char *q=b+at+h;
     samples=be32(q+8);}
    if((!memcmp(tag,"mdia",4)||!memcmp(tag,"minf",4)||!memcmp(tag,"stbl",4)) && depth<15){stack_end[depth++]=end;end=at+(size_t)z;at+=h;continue;}
    at+=(size_t)z;
    while(depth && at>=end){at=end;end=stack_end[--depth];}
   }
   if(handler==1){video++;v_samples+=samples;}else if(handler==2){audio++;a_samples+=samples;}else if(handler==3)meta++;
  }
  i+=(size_t)sz;
 }
 fprintf(o,"%"PRIu64",%zu,%u,%u,%u,%u,%"PRIu64",%"PRIu64",DISCOVERED_NOT_VERIFIED\n",off,n,tracks,video,audio,meta,v_samples,a_samples);
}
static void parse_boxes(Parse *s,const unsigned char *b,size_t len,uint64_t relative,int level,const char *parent){
 if(level>12)return;
 size_t i=0;
 while(i+8<=len){
  uint64_t sz=be32(b+i); size_t head=8;
  if(sz==1 && i+16<=len){sz=be64(b+i+8);head=16;}
  if(sz==0)sz=len-i;
  if(sz<head || sz>len-i)break;
  const unsigned char *body=b+i+head;size_t bodylen=(size_t)sz-head;
  char tag[5]={0};memcpy(tag,b+i+4,4);
  if(!memcmp(tag,"mvhd",4)||!memcmp(tag,"tkhd",4)||!memcmp(tag,"mdhd",4)){
   if(bodylen>=12){unsigned ver=body[0];size_t needed=ver?20:12;
    if((ver==0||ver==1)&&bodylen>=needed){uint64_t creation=ver?be64(body+4):be32(body+4);
     uint64_t modified=ver?be64(body+12):be32(body+8);char c[40],m[40];int cv=mp4_time(creation,c),mv=mp4_time(modified,m);
     fprintf(s->fields,"%"PRIu64",%"PRIu64",%s,%u,%"PRIu64",%"PRIu64",",s->moov,relative+i,tag,ver,creation,modified);
     csv_s(s->fields,cv?c:"");fputc(',',s->fields);csv_s(s->fields,mv?m:"");
     fprintf(s->fields,",%s\n",(cv&&mv)?"PLAUSIBLE_UNVERIFIED":"UNKNOWN_OR_OUT_OF_RANGE");s->p->date_fields++;
     if(s->ev){char detail[160];snprintf(detail,sizeof(detail),"%s creation=%s raw=%"PRIu64,tag,cv?c:"unknown",creation);s->ev(s->ctx,"DATE_FIELD",relative+i,detail);}
    }
   }
  }
  // ISO-shaped embedded text is evidence only, never promoted to an absolute recording time.
  if(memcmp(tag,"moov",4) && memcmp(tag,"trak",4) && memcmp(tag,"mdia",4) && memcmp(tag,"minf",4) && memcmp(tag,"stbl",4) && memcmp(tag,"udta",4) && memcmp(tag,"meta",4) && memcmp(tag,"ilst",4) && memcmp(tag,"edts",4)) for(size_t j=0;j+19<=bodylen;j++)if(date_ascii(body,bodylen,j)){
   char stamp[20];memcpy(stamp,body+j,19);stamp[19]=0;
   fprintf(s->strings,"%"PRIu64",%"PRIu64",%s,",s->moov,relative+i+head+j,tag);csv_s(s->strings,stamp);fputs(",UNVERIFIED_EMBEDDED_STRING\n",s->strings);
   j+=18;
  }
  if(!memcmp(tag,"moov",4)||!memcmp(tag,"trak",4)||!memcmp(tag,"mdia",4)||!memcmp(tag,"minf",4)||!memcmp(tag,"stbl",4)||!memcmp(tag,"udta",4)||!memcmp(tag,"meta",4)||!memcmp(tag,"ilst",4)||!memcmp(tag,"edts",4)){
   size_t skip=!memcmp(tag,"meta",4)&&bodylen>=4?4:0;
   parse_boxes(s,body+skip,bodylen-skip,relative+i+head+skip,level+1,tag);
  }
  i+=(size_t)sz;
 }
 (void)parent;
}
static int readat(FILE *f,uint64_t off,unsigned char *p,size_t n){if(FSEEK(f,(int64_t)off,SEEK_SET))return 0;return fread(p,1,n,f)==n;}

typedef struct {WBEvent event; WBStop stop; void *user;} WBPhaseCtx;
static int phase_stop(void *arg){WBPhaseCtx *p=(WBPhaseCtx*)arg;return p->stop?p->stop(p->user):0;}
static void phase_match(void *arg,size_t done,size_t total,size_t accepted){
 WBPhaseCtx *p=(WBPhaseCtx*)arg; if(!p->event)return;
 if(done!=total && done!=1 && (done%64)!=0)return;
 char detail[128];snprintf(detail,sizeof(detail),"%zu/%zu | %zu uniquely mapped",done,total,accepted);
 p->event(p->user,"MATCH_PROGRESS",(uint64_t)done,detail);
}
int wb_scan(const char *image_path,const char *outdir,WBEvent ev,WBStop stop,void *ctx,WBProgress *p){
 memset(p,0,sizeof(*p));FILE *f=fopen(image_path,"rb");if(!f)return -1;
 if(FSEEK(f,0,SEEK_END)){fclose(f);return -2;}p->size=(uint64_t)FTELL(f);
 mkpath(outdir);char path[4096];
 #define OUTFILE(name) snprintf(path,sizeof(path),"%s%c%s",outdir,SEP,name)
 OUTFILE("container_time_fields.csv");FILE *fields=fopen(path,"wb");
 OUTFILE("embedded_date_candidates.csv");FILE *strings=fopen(path,"wb");
 OUTFILE("tbgp_wrapper_clocks.csv");FILE *clocks=fopen(path,"wb");
 OUTFILE("scan_summary.txt");FILE *summary=fopen(path,"wb");
 OUTFILE("recording_inventory.csv");FILE *records=fopen(path,"wb");
 OUTFILE("sample_table_index.csv");FILE *tables=fopen(path,"wb");
 OUTFILE("fat_geometry_candidates.csv");FILE *geometry=fopen(path,"wb");
 WBGeometryStats geometry_stats={0};
 if(!fields||!strings||!clocks||!summary||!records||!tables||!geometry){if(geometry)fclose(geometry);if(fields)fclose(fields);if(strings)fclose(strings);if(clocks)fclose(clocks);if(summary)fclose(summary);if(records)fclose(records);if(tables)fclose(tables);fclose(f);return -3;}
 fputs("moov_offset,field_offset,atom,version,raw_creation_1904,raw_modification_1904,creation_utc,modification_utc,assessment\n",fields);
 fputs("moov_offset,byte_offset,container_atom,text,assessment\n",strings);
 fputs("tbgp_offset,tbgp_size,wrapper_offset,class,clock_be_microseconds_candidate,payload_length,assessment\n",clocks);
 fputs("moov_offset,moov_size,track_count,video_tracks,audio_tracks,metadata_tracks,video_samples,audio_samples,status\n",records);
 fputs("moov_offset,track_number,handler,sample_count,chunk_count,constant_sample_size,first_size_1,first_size_2,first_size_3,first_size_4,implied_samples_from_stsc,status\n",tables);
 fputs("boot_offset,bytes_per_sector,sectors_per_cluster,reserved_sectors,fat_copies,fat_sectors,total_sectors,root_cluster,bpb_hidden_sectors,fat_offset,data_offset,volume_bytes,cluster_count,fsinfo_sector,backup_sector,status\n",geometry);
 WBIndex *ownership=wb_owner_new();
 WBJpeg *jpeg=wb_jpeg_new(outdir);
 const size_t BLOCK=4*1024*1024,OVERLAP=32;unsigned char *buf=malloc(BLOCK+OVERLAP);
 if(!buf){fclose(f);fclose(fields);fclose(strings);fclose(clocks);fclose(summary);fclose(records);fclose(tables);return -4;}
 uint64_t off=0;
 while(off<p->size){
  if(stop&&stop(ctx)){p->cancelled=1;break;}
  size_t n=(size_t)((p->size-off)<BLOCK+OVERLAP?(p->size-off):BLOCK+OVERLAP);
  if(!readat(f,off,buf,n))break;
  size_t scan=n<BLOCK?n:BLOCK;
  wb_geometry_block(buf,scan,off,p->size,geometry,&geometry_stats);
  for(size_t i=0;i+8<=n && i<scan;i++){
   const unsigned char *at=buf+i;uint32_t sz=be32(at);uint64_t abs=off+i;
   if(!memcmp(at+4,"moov",4) && sz>=16 && sz<=64*1024*1024 && abs+sz<=p->size){
    unsigned char *box=malloc(sz);if(box){if(readat(f,abs,box,sz)){
      p->moovs++;Parse ps={fields,strings,records,abs,ev,ctx,p};parse_boxes(&ps,box,sz,abs,0,"");
      inventory_moov(records,box,sz,abs);
      wb_index_moov(box,sz,abs,tables);
      if(ownership)wb_owner_add_moov(ownership,box,sz,abs);
      if(ev){char d[100];snprintf(d,sizeof(d),"moov size %u bytes",sz);ev(ctx,"MOOV",abs,d);}
     }free(box);}
   }
   if(!memcmp(at+4,"tbgp",4) && sz>=8 && sz<=16*1024*1024 && abs+sz+20<=p->size){
    p->tbgps++;
    wb_jpeg_anchor(jpeg,abs);
    if(ownership)wb_owner_add_anchor(ownership,abs,sz);
    unsigned char w[20];if(readat(f,abs+sz,w,20)){
     uint32_t cls=be32(w),payload=be32(w+16);uint64_t timestamp=be64(w+8);
     if(cls>=1&&cls<=3 && payload>0 && payload<16*1024*1024 && abs+sz+20+payload<=p->size){
      fprintf(clocks,"%"PRIu64",%u,%"PRIu64",%u,%"PRIu64",%u,RELATIVE_CLOCK_ORIGIN_UNKNOWN\n",abs,sz,abs+sz,cls,timestamp,payload);p->wrappers++;
     }
    }
   }
  }
  wb_jpeg_feed(jpeg,buf,scan,off);
  if(ownership)wb_owner_stage1_feed(ownership,buf,n,off,scan);
  off+=scan;p->scanned=off;
  if(ev)ev(ctx,"PROGRESS",off,"");
 }
 OUTFILE("physical_ownership.csv");FILE *owners=fopen(path,"wb");
 OUTFILE("logical_placements.csv");FILE *placements=fopen(path,"wb");
 OUTFILE("association_decisions.csv");FILE *assocdiag=fopen(path,"wb");
 OUTFILE("physical_video_samples.csv");FILE *videodiag=fopen(path,"wb");
 OUTFILE("ownership_candidates.csv");FILE *candidates=fopen(path,"wb");
 OUTFILE("orphan_units.csv");FILE *orphan=fopen(path,"wb");
 if(!assocdiag||!videodiag||!candidates||!orphan){
  if(ev)ev(ctx,"ERROR",p->scanned,"Cannot open association evidence CSV output");
  p->cancelled=1;
 }
 int owned=-1;
 if(!p->cancelled && owners && placements){
  if(ev){char d[160];snprintf(d,sizeof(d),"Stage 2/3: Resolving ownership (%zu/%zu wrapper prefixes cached during Stage 1)",wb_owner_stage1_ready(ownership),(size_t)p->tbgps);ev(ctx,"STAGE",p->scanned,d);}
  WBPhaseCtx phase={ev,stop,ctx};
  owned=wb_owner_resolve_progress(ownership,image_path,owners,placements,assocdiag,videodiag,candidates,orphan,phase_stop,&phase,phase_match);
  if(stop && stop(ctx))p->cancelled=1;
 }
 if(owners)fclose(owners);
 if(assocdiag)fclose(assocdiag);
 if(videodiag)fclose(videodiag);
 if(candidates)fclose(candidates);
 if(orphan)fclose(orphan);
 if(!p->cancelled){ WBOrphanStats orphan_stats={0}; int orc=wb_orphan_extract(image_path,outdir,&orphan_stats,stop,ctx); if(ev){char msg[180];snprintf(msg,sizeof(msg),"Orphan HEVC: %"PRIu64" structurally valid of %"PRIu64" candidates (read-only)",orphan_stats.valid_samples,orphan_stats.samples);ev(ctx,orc==0?"STAGE":"ERROR",p->scanned,msg);} }
 write_orphan_joins(outdir);
 write_unmapped_priorities(outdir);
 if(placements)fclose(placements);
 OUTFILE("logical_placements.csv");char placepath[4096];snprintf(placepath,sizeof(placepath),"%s",path);
 if(ev && !p->cancelled)ev(ctx,"STAGE",p->scanned,"Stage 3/3: Preparing recording inventory");
 OUTFILE("candidate_unit_runs.csv");if(!p->cancelled)wb_write_runs(placepath,path);
 if(ev&&!p->cancelled)ev(ctx,"PREP_PROGRESS",1,"1/2 | Recording runs grouped");
 OUTFILE("recording_summary.csv");if(!p->cancelled)wb_write_recording_summary(placepath,path);
 if(ev&&!p->cancelled)ev(ctx,"PREP_PROGRESS",2,"2/2 | Recording inventory completed");
 size_t indexed_count=0,fallback_count=0;
 wb_owner_stage1_usage(ownership,&indexed_count,&fallback_count);
 fprintf(summary,"Stage-2 wrapper probes served by sequential Stage-1 index: %zu\nStage-2 wrapper probes using legacy image reads: %zu\n",indexed_count,fallback_count);
 wb_owner_free(ownership);
 uint64_t jpg_recovered=0,jpg_rejected=0;wb_jpeg_finish(jpeg,&jpg_recovered,&jpg_rejected);
 fprintf(summary,"JPEG status: %s (jpeg_evidence.csv and jpeg_scan_summary.txt written on completed scan)\n",jpeg?"EXECUTED":"NOT_INITIALIZED");
 fprintf(summary,"JPEG recovered: %"PRIu64"; rejected or oversize candidates: %"PRIu64"\n",jpg_recovered,jpg_rejected);
 fprintf(summary,"Unique strictly contiguous ownership prefixes: %d\n",owned);
 fprintf(summary,"FAT32 geometry candidates: %"PRIu64" (plausible geometry: %"PRIu64")\n",geometry_stats.candidates,geometry_stats.accepted);
 fprintf(summary,"Source: %s\nSource bytes: %"PRIu64"\nScanned: %"PRIu64"\nRaw moov candidates: %"PRIu64"\nRaw tbgp candidates: %"PRIu64"\nPlausible first wrappers: %"PRIu64"\nCreation/modification fields: %"PRIu64"\nCancelled: %s\n",image_path,p->size,p->scanned,p->moovs,p->tbgps,p->wrappers,p->date_fields,p->cancelled?"yes":"no");
 fputs("IMPORTANT: MP4 creation date is metadata, not verified clip recording time. Wrapper clocks are relative until absolute calibration is established. Embedded date strings are unverified. FAT32 geometry discovery only; FAT chains are NOT interpreted in this build. Historical geometry candidates are not proven active.\n",summary);
 free(buf);fclose(f);fclose(fields);fclose(strings);fclose(clocks);fclose(summary);fclose(records);fclose(tables);fclose(geometry);return p->cancelled?1:0;
}
