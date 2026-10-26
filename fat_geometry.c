#include "fat_geometry.h"
#include <inttypes.h>
#include <string.h>
static uint16_t le16(const unsigned char *p){return (uint16_t)(p[0]|(p[1]<<8));}
static uint32_t le32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int pow2(uint32_t v){return v && !(v&(v-1));}
void wb_geometry_block(const unsigned char *p,size_t bytes,uint64_t off,uint64_t size,FILE *out,WBGeometryStats *stats){
 if(!p||!out||!stats)return;
 for(size_t i=0;i+512<=bytes;i+=512){
  const unsigned char *b=p+i;
  if(b[510]!=0x55||b[511]!=0xaa)continue;
  if(b[0]!=0xeb && b[0]!=0xe9)continue;
  uint32_t bps=le16(b+11),spc=b[13],reserved=le16(b+14),fats=b[16];
  uint32_t root=le16(b+17),tot16=le16(b+19),fat16=le16(b+22),tot32=le32(b+32),fat32=le32(b+36);
  uint32_t rootcluster=le32(b+44),fsinfo=le16(b+48),backup=le16(b+50);
  if((bps!=512&&bps!=1024&&bps!=2048&&bps!=4096)||!pow2(spc)||spc>128||!reserved||!(fats==1||fats==2)||root||fat16||!fat32||!rootcluster||rootcluster<2||tot16||!tot32)continue;
  if(bps*spc>1024*1024)continue;
  uint64_t overhead=(uint64_t)reserved+(uint64_t)fats*fat32;
  if(tot32<=overhead)continue;
  uint64_t clusters=(tot32-overhead)/spc;
  if(clusters<65525 || clusters>0x0ffffff5ULL)continue;
  if((uint64_t)fat32*bps/4 < clusters+2)continue;
  uint64_t candidate=off+i,span=(uint64_t)tot32*bps;
  if(candidate>size||span>size-candidate)continue;
  stats->candidates++;
  /* Derive sector address from the physical boot-sector location; hidden-sector BPB value
     is untrusted, particularly after repartitioning. Historical boot sectors may be orphans. */
  uint64_t fat_start=candidate+(uint64_t)reserved*bps;
  uint64_t data_start=candidate+overhead*bps;
  int fsvalid=(fsinfo<reserved),bkvalid=(backup==0||backup<reserved);
  int plausible=fsvalid&&bkvalid&&data_start<size;
  if(plausible)stats->accepted++;
  fprintf(out,"%"PRIu64",%u,%u,%u,%u,%u,%u,%u,%u,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%u,%u,%s\n",
      candidate,bps,spc,reserved,fats,fat32,tot32,rootcluster,le32(b+28),fat_start,data_start,span,clusters,fsinfo,backup,
      plausible?"GEOMETRY_CANDIDATE_UNVERIFIED":"BPB_OPTIONAL_FIELDS_QUESTIONABLE");
 }
}
