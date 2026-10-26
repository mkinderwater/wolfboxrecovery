#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#define SEEK64 _fseeki64
#define MKDIR(x) _mkdir(x)
#define SEP '\\'
#else
#include <sys/stat.h>
#define SEEK64 fseeko
#define MKDIR(x) mkdir(x,0777)
#define SEP '/'
#endif
#define MAX_SAMPLE (16u*1024u*1024u)
#define NAL_LIMIT 128
/* Standalone targeted verification: never imports an owner or anchor list. */
typedef struct {uint64_t anchor,offset;unsigned length,ordinal;int irap, vps,sps,pps;} Sample;
typedef struct {uint64_t anchor;uint64_t first_clock,last_clock;int has_clock;Sample *a;size_t n,cap;unsigned char *ps[3];size_t ps_len[3];int conflicted;uint64_t bytes;unsigned bad;int entry;} Group;
static uint32_t be32(const unsigned char *b){return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];}
static int samplecmp(const void *a,const void*b){const Sample*x=a,*y=b;return x->ordinal<y->ordinal?-1:x->ordinal>y->ordinal?1:0;}
static int groupcmp(const void*a,const void*b){const Group*x=a,*y=b;return x->anchor<y->anchor?-1:x->anchor>y->anchor?1:0;}
static int get_sample(FILE *image,Sample*s,unsigned char **buf,size_t *cap){if(s->length<6||s->length>MAX_SAMPLE)return -1;if(s->length>*cap){unsigned char*p=realloc(*buf,s->length);if(!p)return -2;*buf=p;*cap=s->length;}if(SEEK64(image,(int64_t)s->offset,SEEK_SET)||fread(*buf,1,s->length,image)!=s->length)return -3;return 0;}
/* Fully bound HEVC NAL framing check; exact bytes retained. */
static int parse(const unsigned char *buf,size_t sz,Group*g,int *irap,int *vcl,int store_ps){size_t p=0;unsigned n=0;*irap=0;*vcl=0;while(p<sz){if(sz-p<6||n++>NAL_LIMIT)return -1;uint32_t len=be32(buf+p);p+=4;if(len<2||len>sz-p)return -2;const unsigned char*h=buf+p;unsigned type=(h[0]>>1)&63;if((h[0]&128)||!(h[1]&7))return -3;if(type<32)(*vcl)++;if(type>=16&&type<=23)(*irap)++;if(store_ps&&type>=32&&type<=34){int k=(int)type-32;if(!g->ps[k]){g->ps[k]=malloc(len);if(!g->ps[k])return -4;memcpy(g->ps[k],h,len);g->ps_len[k]=len;}else if(g->ps_len[k]!=len||memcmp(g->ps[k],h,len))g->conflicted=1;}p+=len;}return *vcl?0:-5;}
static void write_annex(FILE *out,const unsigned char*b,size_t sz){size_t p=0;const unsigned char prefix[4]={0,0,0,1};while(p<sz){uint32_t n=be32(b+p);p+=4;fwrite(prefix,1,4,out);fwrite(b+p,1,n,out);p+=n;}}
static int same_ps(Group*a,Group*b){if(a->conflicted||b->conflicted)return 0;for(int k=0;k<3;k++)if(!a->ps[k]||!b->ps[k]||a->ps_len[k]!=b->ps_len[k]||memcmp(a->ps[k],b->ps[k],a->ps_len[k]))return 0;return 1;}
static Group *findgroup(Group **groups,size_t *ng,size_t *capacity,uint64_t anchor){for(size_t j=0;j<*ng;j++)if((*groups)[j].anchor==anchor)return &(*groups)[j];if(*ng==*capacity){size_t cap=*capacity?*capacity*2:64;Group*p=realloc(*groups,cap*sizeof(Group));if(!p)return NULL;*groups=p;*capacity=cap;}Group*g=&(*groups)[(*ng)++];memset(g,0,sizeof(*g));g->anchor=anchor;g->entry=-1;return g;}
int main(int argc,char**argv){if(argc!=5){fprintf(stderr,"Usage: orphan_decode IMAGE orphan_hevc_samples.csv orphan_units.csv OUTPUT_DIR\n");return 2;}FILE*csv=fopen(argv[2],"rb");if(!csv){perror(argv[2]);return 2;}FILE*image=fopen(argv[1],"rb");if(!image){perror(argv[1]);fclose(csv);return 2;}MKDIR(argv[4]);char line[4096];Group *groups=NULL;size_t ng=0,gcap=0;fgets(line,sizeof line,csv);while(fgets(line,sizeof line,csv)){char*save=NULL;char*cols[12]={0};int n=0;for(char*p=strtok_r(line,",\r\n",&save);p&&n<12;p=strtok_r(NULL,",\r\n",&save))cols[n++]=p;if(n<5||strcmp(cols[4],"STRUCTURAL_PASS"))continue;uint64_t anchor=strtoull(cols[0],NULL,10);Group*g=findgroup(&groups,&ng,&gcap,anchor);if(!g){fprintf(stderr,"memory allocation failure\n");return 3;}if(g->n==g->cap){size_t c=g->cap?g->cap*2:256;Sample*p=realloc(g->a,c*sizeof(Sample));if(!p)return 3;g->a=p;g->cap=c;}Sample*s=&g->a[g->n++];memset(s,0,sizeof(*s));s->anchor=anchor;s->offset=strtoull(cols[1],NULL,10);s->length=(unsigned)strtoul(cols[2],NULL,10);s->ordinal=(unsigned)strtoul(cols[3],NULL,10);}
fclose(csv);qsort(groups,ng,sizeof(Group),groupcmp);
 FILE *clocks=fopen(argv[3],"rb");if(!clocks){perror(argv[3]);return 2;}
 fgets(line,sizeof line,clocks);
 while(fgets(line,sizeof line,clocks)){
  char *save=NULL,*col[6]={0};int n=0;
  for(char *v=strtok_r(line,",\r\n",&save);v&&n<6;v=strtok_r(NULL,",\r\n",&save))col[n++]=v;
  if(n<4)continue;
  uint64_t a=strtoull(col[0],NULL,10);
  for(size_t k=0;k<ng;k++)if(groups[k].anchor==a){groups[k].first_clock=strtoull(col[2],NULL,10);groups[k].last_clock=strtoull(col[3],NULL,10);groups[k].has_clock=1;break;}
 }
 fclose(clocks);char path[4096];snprintf(path,sizeof path,"%s%corphan_decode_evidence.csv",argv[4],SEP);FILE*report=fopen(path,"wb");if(!report)return 4;fprintf(report,"anchor,observed_samples,reverified_samples,failed_samples,first_ordinal,last_ordinal,first_decodable_ordinal,codec_sets_found,parameter_set_conflict,decode_candidate_path,claim\n");unsigned char*buf=NULL;size_t cap=0;size_t output_count=0;for(size_t i=0;i<ng;i++){Group*g=&groups[i];qsort(g->a,g->n,sizeof(Sample),samplecmp);int entry=-1;size_t good=0;unsigned last=0;int broken=0;for(size_t j=0;j<g->n;j++){Sample*s=&g->a[j];if(j&&s->ordinal!=last+1)broken=1;last=s->ordinal;int irap=0,vcl=0;if(get_sample(image,s,&buf,&cap)||parse(buf,s->length,g,&irap,&vcl,1)){g->bad++;broken=1;continue;}good++;g->bytes+=s->length;if(entry<0&&!broken&&irap>0)entry=(int)j;}g->entry=entry;
 char rel[256]="";
 if(entry>=0&&g->ps[0]&&g->ps[1]&&g->ps[2]&&!g->conflicted){
  snprintf(rel,sizeof rel,"orphan_%" PRIu64 "_from_%u.hevc",g->anchor,g->a[entry].ordinal);
  snprintf(path,sizeof path,"%s%c%s",argv[4],SEP,rel);
  FILE*out=fopen(path,"wb");if(out){
   int failure=0;
   for(size_t j=(size_t)entry;j<g->n;j++){
    if(j>(size_t)entry&&g->a[j].ordinal!=g->a[j-1].ordinal+1){failure=1;break;}
    int irap,vcl;
    if(get_sample(image,&g->a[j],&buf,&cap)||parse(buf,g->a[j].length,g,&irap,&vcl,0)){failure=1;break;}
    write_annex(out,buf,g->a[j].length);
   }
   fclose(out);if(!failure)output_count++;
   else {remove(path);rel[0]=0;}
  }else rel[0]=0;
 }
 fprintf(report,"%" PRIu64 ",%zu,%zu,%u,%u,%u,%d,%d,%d,%s,%s\n",g->anchor,g->n,good,g->bad,g->n?g->a[0].ordinal:0,g->n?g->a[g->n-1].ordinal:0,entry>=0?(int)g->a[entry].ordinal:-1,!!(g->ps[0]&&g->ps[1]&&g->ps[2]),g->conflicted,rel,rel[0]?"DECODER_TEST_REQUIRED":"NOT_EXPORTED");
 }
 fclose(report);
 snprintf(path,sizeof path,"%s%corphan_codec_comparisons.csv",argv[4],SEP);
 FILE*c=fopen(path,"wb");if(c){
  fputs("left_anchor,right_anchor,parameter_sets_identical,interpretation\n",c);
  for(size_t i=0;i<ng;i++)for(size_t j=i+1;j<ng;j++)if(groups[i].n>=128&&groups[j].n>=128)
   fprintf(c,"%" PRIu64 ",%" PRIu64 ",%s,%s\n",groups[i].anchor,groups[j].anchor,same_ps(&groups[i],&groups[j])?"YES":"NO",same_ps(&groups[i],&groups[j])?"CODEC_COMPATIBLE_NOT_A_JOIN":"CODEC_DIFFERENT_OR_UNPROVEN");
  fclose(c);
 }
 /* Candidate joining is evidence-driven: exact codec bytes AND a recorded 40,000 us boundary.
    Never use a hardcoded owner, anchor address, or a prepopulated membership list. */
 snprintf(path,sizeof path,"%s%cjoin_evidence.csv",argv[4],SEP);
 FILE *joins=fopen(path,"wb");
 if(!joins)return 5;
 fputs("left_anchor,right_anchor,clock_delta_us,codec_equal,verified_sample_counts,decision\n",joins);
 
 size_t seqs=0;
 for(size_t i=0;i<ng;){
  if(!groups[i].n||groups[i].bad||groups[i].conflicted||groups[i].entry<0){i++;continue;}
  size_t j=i;
  while(j+1<ng){Group *left=&groups[j],*right=&groups[j+1];
   int64_t dt=right->has_clock&&left->has_clock?(int64_t)right->first_clock-(int64_t)left->last_clock:INT64_MIN;
   int codec=same_ps(left,right);int count_ok=right->n&&right->bad==0&&right->a[0].ordinal==0;
   /* Enforce 25fps boundary interval reported in source clock evidence. */
   int accepted=dt==40000&&codec&&count_ok&&!right->conflicted;
   fprintf(joins,"%" PRIu64 ",%" PRIu64 ",%" PRId64 ",%s,%s,%s\n",left->anchor,right->anchor,dt,codec?"YES":"NO",count_ok?"YES":"NO",accepted?"CLOCK_CODEC_CANDIDATE":"REJECTED");
   if(!accepted)break;
   j++;
  }
  if(j>i){
   snprintf(path,sizeof path,"%s%ccandidate_%" PRIu64 "_to_%" PRIu64 ".hevc",argv[4],SEP,groups[i].anchor,groups[j].anchor);
   FILE *out=fopen(path,"wb");int bad=out?0:1;
   char manifest[4096];snprintf(manifest,sizeof manifest,"%s%cjoin_samples_%" PRIu64 ".csv",argv[4],SEP,groups[i].anchor);
   FILE *provenance=fopen(manifest,"wb");if(!provenance)bad=1;
   if(provenance)fputs("sequence_index,anchor,video_ordinal,physical_payload_offset,bytes\n",provenance);
   size_t nout=0;
   for(size_t k=i;k<=j&&!bad;k++){
    size_t from=k==i?(size_t)groups[i].entry:0;
    for(size_t z=from;z<groups[k].n;z++){
     Sample *a=&groups[k].a[z];int irap=0,vcl=0;
     if(z>from&&a->ordinal!=groups[k].a[z-1].ordinal+1){bad=1;break;}
     if(get_sample(image,a,&buf,&cap)||parse(buf,a->length,&groups[k],&irap,&vcl,0)){bad=1;break;}
     write_annex(out,buf,a->length);
     if(provenance)fprintf(provenance,"%zu,%" PRIu64 ",%u,%" PRIu64 ",%u\n",nout,groups[k].anchor,a->ordinal,a->offset,a->length);
     nout++;
    }
   }
   if(out)fclose(out);
   if(provenance)fclose(provenance);
   if(bad){remove(path);remove(manifest);}else{seqs++;printf("Candidate group %" PRIu64 " .. %" PRIu64 ": %zu samples; cross-boundary picture decode NOT YET PROVEN.\n",groups[i].anchor,groups[j].anchor,nout);}
  }
  i=j+1;
 }
 fclose(joins);
 printf("Anchors: %zu; IRAP-start streams: %zu; multi-anchor candidates: %zu. Full decode still required.\n",ng,output_count,seqs);
 for(size_t i=0;i<ng;i++){for(int k=0;k<3;k++)free(groups[i].ps[k]);free(groups[i].a);}free(groups);free(buf);fclose(image);return 0;
}
