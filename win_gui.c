#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "recovery_core.h"
#include "scan_cache.h"
#include "video_export.h"
#include "mp4_export.h"
#include "audio_mapping.h"
#define ID_ICON 301
#define ID_LBL_SOURCE 201
#define ID_LBL_DEST 202
#define ID_LBL_RESULTS 203
#define ID_LBL_ACTIVITY 204
#define ID_SOURCE 101
#define ID_SOURCE_BROWSE 102
#define ID_DEST 103
#define ID_DEST_BROWSE 104
#define ID_ANALYZE 105
#define ID_STOP 106
#define ID_LOG 107
#define ID_STATUS 108
#define ID_BAR 109
#define ID_RESULTS 110
#define ID_EXPORT 111
#define ID_SELECT_ALL 112
#define ID_CLEAR_ALL 113
#define ID_LBL_DATE_NOTE 205
#define ID_ORPHAN_MP4 114
#define WM_ORPHAN_DONE (WM_APP+5)
#define WM_BATCH_EVENT (WM_APP+4)
#define WM_EXPORT_DONE (WM_APP+3)
#define WM_SCAN_EVENT (WM_APP+1)
#define WM_SCAN_DONE (WM_APP+2)
static HWND mainwnd, source, dest, analyze, exportbtn, stopbtn, progress, status, logbox, results, allbtn, clearbtn, orphanbtn;
static int dpi_x=96;
static int stage_number=0;
static ULONGLONG stage_started=0;
static uint64_t source_bytes=0;
static uint64_t stage_done=0,stage_total=0;
static double recent_rate=0;
static ULONGLONG recent_at=0;
static uint64_t recent_done=0;
typedef struct {
 uint64_t anchor, owner, base, end;
 unsigned units;
 char date[40], state[96];
 int hasdate;
} RecordingRow;
static int sort_column=0, sort_descending=0;
static RecordingRow **record_rows=NULL;
static size_t record_count=0;
static int operation=0; /* 0 idle, 1 scanning, 2 exporting */
static uint64_t *batch_anchors=NULL;
static unsigned batch_count=0;
static volatile LONG cancel_flag=0;static HANDLE threadhandle=0;static char sourcepath[4096],outpath[4096];
typedef struct{char *type,*details;uint64_t offset;} Event;
static void setfont(HWND h){SendMessage(h,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);}
static HWND control(const char *cls,const char *title,DWORD style,int x,int y,int w,int h,int id){HWND t=CreateWindowExA(0,cls,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,mainwnd,(HMENU)(intptr_t)id,GetModuleHandleA(0),0);setfont(t);return t;}
static int scale(int n){return MulDiv(n,dpi_x,96);}
static void place(HWND h,int x,int y,int w,int height){
 if(h)MoveWindow(h,scale(x),scale(y),scale(w),scale(height),TRUE);
}
static void layout(HWND w){
 RECT r;GetClientRect(w,&r);
 int width=MulDiv(r.right,96,dpi_x),height=MulDiv(r.bottom,96,dpi_x);
 if(width<900)width=900;
 if(height<440)height=440;
 const int margin=16, gap=9, labelwidth=103, buttonwidth=98;
 int fieldsx=margin+labelwidth+gap;
 int fieldw=width-fieldsx-margin-buttonwidth-gap;
 place(GetDlgItem(w,ID_LBL_SOURCE),margin,19,labelwidth,21);
 place(source,fieldsx,15,fieldw,27);
 place(GetDlgItem(w,ID_SOURCE_BROWSE),fieldsx+fieldw+gap,15,buttonwidth,27);
 place(GetDlgItem(w,ID_LBL_DEST),margin,57,labelwidth,21);
 place(dest,fieldsx,53,fieldw,27);
 place(GetDlgItem(w,ID_DEST_BROWSE),fieldsx+fieldw+gap,53,buttonwidth,27);
 int x=margin,y=96;
 place(analyze,x,y,126,31);x+=135;
 place(stopbtn,x,y,74,31);x+=83;
 place(exportbtn,x,y,153,31);x+=162;
 place(allbtn,x,y,88,31);x+=97;
 place(clearbtn,x,y,88,31);x+=97;
 place(orphanbtn,x,y,150,31);
 place(progress,margin,142,width-2*margin,15);
 place(status,margin,164,width-2*margin,25);
 place(GetDlgItem(w,ID_LBL_RESULTS),margin,191,width-2*margin,20);
 place(GetDlgItem(w,ID_LBL_DATE_NOTE),margin,209,width-2*margin,16);
 int act_label_y=height-151;
 place(results,margin,229,width-2*margin,act_label_y-229-10);
 place(GetDlgItem(w,ID_LBL_ACTIVITY),margin,act_label_y,width-2*margin,20);
 place(logbox,margin,act_label_y+23,width-2*margin,height-(act_label_y+23)-margin);
}
static void stage_begin(int number){
 stage_number=number;stage_started=GetTickCount64();stage_done=0;stage_total=0;recent_rate=0;recent_at=0;recent_done=0;
 SendMessage(progress,PBM_SETRANGE32,0,1000);SendMessage(progress,PBM_SETPOS,0,0);
}
static void show_progress(uint64_t done,uint64_t total,const char *label){
 ULONGLONG now=GetTickCount64();if(total==0)total=1; if(done>total)done=total;
 if(!recent_at){recent_at=now;recent_done=done;}
 if(now>recent_at+2500 && done>=recent_done){
  double rate=(double)(done-recent_done)*1000.0/(double)(now-recent_at);
  if(rate>0)recent_rate=recent_rate?recent_rate*0.70+rate*0.30:rate;
  recent_at=now;recent_done=done;
 }
 stage_done=done;stage_total=total;
 SendMessage(progress,PBM_SETPOS,(WPARAM)((done*1000)/total),0);
 ULONGLONG elapsed=(now-stage_started)/1000;char eta[48]="Calculating...";
 if(done==total)snprintf(eta,sizeof(eta),"Complete");
 else if(recent_rate>0 && elapsed>=4){
  unsigned long long secs=(unsigned long long)((double)(total-done)/recent_rate);
  snprintf(eta,sizeof(eta),"~%lluh %02llum",secs/3600,(secs%3600)/60);
 }
 char msg[320];snprintf(msg,sizeof(msg),"Stage %d/3: %s | %.1f%% | %llu/%llu | elapsed %02llu:%02llu | remaining %s",
 stage_number,label,100.0*(double)done/(double)total,(unsigned long long)done,(unsigned long long)total,
 (unsigned long long)(elapsed/60),(unsigned long long)(elapsed%60),eta);
 SetWindowTextA(status,msg);
}
static void logline(const char *text){int len=GetWindowTextLengthA(logbox);SendMessageA(logbox,EM_SETSEL,len,len);SendMessageA(logbox,EM_REPLACESEL,0,(LPARAM)text);SendMessageA(logbox,EM_REPLACESEL,0,(LPARAM)"\r\n");}
static void __cdecl evt(void *ctx,const char *kind,uint64_t off,const char *detail){
 (void)ctx;static uint64_t lastpct=UINT64_MAX;
 if(!strcmp(kind,"PROGRESS")){ // send coarse progress to avoid flooding the GUI
   if(off/104857600==lastpct) return;
   lastpct=off/104857600;
 }
 Event *e=(Event*)calloc(1,sizeof(Event));if(!e)return;
 e->type=_strdup(kind);e->details=_strdup(detail);e->offset=off;
 if(!PostMessage(mainwnd,WM_SCAN_EVENT,0,(LPARAM)e)){free(e->type);free(e->details);free(e);}
}
static int isstop(void *ctx){(void)ctx;return InterlockedCompareExchange(&cancel_flag,0,0)!=0;}
static DWORD WINAPI worker(LPVOID p){(void)p;WBProgress result;int r=wb_scan_cached(sourcepath,outpath,evt,isstop,0,&result);PostMessage(mainwnd,WM_SCAN_DONE,(WPARAM)(INT_PTR)r,0);return 0;}
static void batch_message(const char *message){
 char *copy=_strdup(message);
 if(copy&&!PostMessageA(mainwnd,WM_BATCH_EVENT,0,(LPARAM)copy))free(copy);
}
static int path_exists(const char *name){DWORD a=GetFileAttributesA(name);return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);}
static uint64_t filesize(const char *name){
 WIN32_FILE_ATTRIBUTE_DATA data;
 if(!GetFileAttributesExA(name,GetFileExInfoStandard,&data))return 0;
 return ((uint64_t)data.nFileSizeHigh<<32)|data.nFileSizeLow;
}
static int completed_export(const char *mp4,const char *marker,uint64_t anchor){
 if(!path_exists(mp4)||!path_exists(marker))return 0;
 FILE *f=fopen(marker,"rb");if(!f)return 0;
 unsigned long long a=0,bytes=0;
 int ok=fscanf(f,"anchor=%llu\nbytes=%llu",&a,&bytes)==2;
 fclose(f);return ok && a==anchor && bytes>0 && filesize(mp4)==bytes;
}
/* tkhd values are recorder claims, NOT validated UTC wall-clock timestamps.
   They are allowed in filenames only with an explicit unverified marker. */
static int tentative_date(uint64_t moov,char *date,size_t capacity){
 char path[4096],line[1024];
 if(snprintf(path,sizeof(path),"%s\\container_time_fields.csv",outpath)>=(int)sizeof(path))return 0;
 FILE*f=fopen(path,"rb");if(!f)return 0;
 int found=0;date[0]=0;
 while(fgets(line,sizeof(line),f)){
  unsigned long long owner=0,off=0,create=0,modify=0;unsigned version=0;
  char atom[32],utc[64],mutc[64],assessment[96];
  if(sscanf(line,"%llu,%llu,%31[^,],%u,%llu,%llu,\"%63[^\"]\",\"%63[^\"]\",%95[^\r\n]",
   &owner,&off,atom,&version,&create,&modify,utc,mutc,assessment)!=9)continue;
  if(owner!=(unsigned long long)moov||strcmp(atom,"tkhd")||strcmp(assessment,"PLAUSIBLE_UNVERIFIED"))continue;
  unsigned y,m,d,h,mi,sec;
  if(sscanf(utc,"%4u-%2u-%2uT%2u:%2u:%2u",&y,&m,&d,&h,&mi,&sec)!=6||y<2000||y>2100||m<1||m>12||d<1||d>31||h>23||mi>59||sec>59)continue;
  char value[32];snprintf(value,sizeof(value),"%04u_%02u_%02u_%02u%02u%02u",y,m,d,h,mi,sec);
  if(found&&strcmp(value,date)){date[0]=0;found=-1;break;} /* conflicting track times */
  snprintf(date,capacity,"%s",value);found=1;
 }
 fclose(f);return found==1;
}
static int run_owner(uint64_t anchor,uint64_t *owner){
 char path[4096],line[2048];
 if(snprintf(path,sizeof(path),"%s\\candidate_unit_runs.csv",outpath)>=(int)sizeof(path))return 0;
 FILE*f=fopen(path,"rb");if(!f)return 0;
 int found=0;fgets(line,sizeof(line),f);
 while(fgets(line,sizeof(line),f)){
  unsigned long long moov=0,lo=0,hi=0,a=0;unsigned units=0;
  if(sscanf(line,"%llu,%llu,%llu,%u,%llu",&moov,&lo,&hi,&units,&a)==5&&a==anchor){*owner=moov;found=1;break;}
 }
 fclose(f);return found;
}
static void run_filename(uint64_t anchor,char *name,size_t capacity){
 uint64_t owner=0;char date[40];
 if(run_owner(anchor,&owner)&&tentative_date(owner,date,sizeof(date)))
  snprintf(name,capacity,"%s_TKHD-UNVERIFIED_REC_%llu.partial.mp4",date,(unsigned long long)anchor);
 else snprintf(name,capacity,"REC_%llu.partial.mp4",(unsigned long long)anchor);
}

static DWORD WINAPI export_worker(LPVOID p){
 (void)p;
 char csv[4096],out[4096],marker[4096],report[4096],line[640];
 if(snprintf(csv,sizeof(csv),"%s\\logical_placements.csv",outpath)>=(int)sizeof(csv)||
    snprintf(report,sizeof(report),"%s\\batch_results.csv",outpath)>=(int)sizeof(report)){
  PostMessage(mainwnd,WM_EXPORT_DONE,2,0);return 0;
 }
 FILE *f=fopen(report,"wb");
 if(!f){PostMessage(mainwnd,WM_EXPORT_DONE,2,0);return 0;}
 fputs("run_anchor,status,export_code,output,date_provenance\n",f);
 unsigned good=0,failed=0,skipped=0;
 for(unsigned i=0;i<batch_count;i++){
  if(isstop(NULL)){
   batch_message("Stop requested: remaining recordings were not started.");
   fprintf(f,"0,CANCELLED_BEFORE_START,9,,\n");
   break;
  }
  uint64_t anchor=batch_anchors[i];
  char basename[240];run_filename(anchor,basename,sizeof(basename));
  /* Canonical lookup is only an integrity check, never used to multiply the queue. */
  uint64_t canonical=wb_run_canonical_anchor(csv,anchor);
  if(!canonical||canonical!=anchor){
   failed++;fprintf(f,"%llu,INVALID_RUN_ANCHOR,3,,\n",(unsigned long long)anchor);continue;
  }
  if(snprintf(out,sizeof(out),"%s\\%s",outpath,basename)>=(int)sizeof(out)||
     snprintf(marker,sizeof(marker),"%s.ok",out)>=(int)sizeof(marker)){
   failed++;continue;
  }
  const char *provenance=strstr(basename,"TKHD-UNVERIFIED")?"TKHD_UNVERIFIED":"UNKNOWN";
  if(completed_export(out,marker,anchor)){
   snprintf(line,sizeof(line),"[%u/%u] Previously completed run %llu",i+1,batch_count,(unsigned long long)anchor);
   batch_message(line);
   fprintf(f,"%llu,SKIPPED_COMPLETED,0,%s,%s\n",(unsigned long long)anchor,basename,provenance);
   skipped++;continue;
  }
  if(path_exists(out)){
   snprintf(line,sizeof(line),"[%u/%u] Refused overwrite of existing run %llu",i+1,batch_count,(unsigned long long)anchor);
   batch_message(line);
   fprintf(f,"%llu,EXISTS_UNVERIFIED,7,%s,%s\n",(unsigned long long)anchor,basename,provenance);
   failed++;continue;
  }
  snprintf(line,sizeof(line),"[%u/%u] Reconstructing run %llu",i+1,batch_count,(unsigned long long)anchor);
  batch_message(line);
  int rc=wb_export_mp4_cancellable(sourcepath,csv,out,anchor,isstop,NULL);
  if(rc==0){
   FILE *ok=fopen(marker,"wb");
   if(ok){
    fprintf(ok,"anchor=%llu\nbytes=%llu\nsource=verified_consecutive_units\ndate_provenance=%s\n",
     (unsigned long long)anchor,(unsigned long long)filesize(out),provenance);
    if(fclose(ok)!=0)rc=8;
   }else rc=8;
  }
  if(rc==9){
   fprintf(f,"%llu,CANCELLED,9,%s,%s\n",(unsigned long long)anchor,basename,provenance);
   batch_message("Current recording cancelled. Remaining recordings will not be started.");
   break;
  }
  if(rc==0){
   /* AAC evidence is captured during the buffered MP4 traversal. */
   good++;fprintf(f,"%llu,EXPORTED,0,%s,%s\n",(unsigned long long)anchor,basename,provenance);
  }
  else{failed++;fprintf(f,"%llu,FAILED,%d,%s,%s\n",(unsigned long long)anchor,rc,basename,provenance);}
  snprintf(line,sizeof(line),"[%u/%u] run %llu: %s (code %d)",i+1,batch_count,(unsigned long long)anchor,rc==0?"exported":"failed",rc);
  batch_message(line);fflush(f);
 }
 fclose(f);
 snprintf(line,sizeof(line),"Batch: %u exported, %u previously complete, %u failed. batch_results.csv",good,skipped,failed);
 batch_message(line);
 PostMessage(mainwnd,WM_EXPORT_DONE,(WPARAM)(INT_PTR)(isstop(NULL)?9:(failed?1:0)),0);
 return 0;
}
/* One row per logical recording run, with persistent identity independent of row order. */
static void clear_recordings(void){
 if(results)ListView_DeleteAllItems(results);
 for(size_t i=0;i<record_count;i++)free(record_rows[i]);
 free(record_rows);record_rows=NULL;record_count=0;
}
static int CALLBACK compare_recordings(LPARAM aa,LPARAM bb,LPARAM ctx){
 const RecordingRow *a=(const RecordingRow*)(ULONG_PTR)aa;
 const RecordingRow *b=(const RecordingRow*)(ULONG_PTR)bb;
 int col=(int)(ctx&255),desc=(int)((ctx>>8)&1);
 int v=0;
 if(col==0){
  /* Unknown timestamps ALWAYS sort after known, even for descending order. */
  if(a->hasdate!=b->hasdate)return a->hasdate?-1:1;
  if(a->hasdate)v=strcmp(a->date,b->date);
 }else if(col==1){v=(a->anchor>b->anchor)-(a->anchor<b->anchor);}
 else if(col==2){v=(a->units>b->units)-(a->units<b->units);}
 else if(col==3){v=strcmp(a->state,b->state);}
 if(desc)v=-v;
 if(!v)v=(a->anchor>b->anchor)-(a->anchor<b->anchor);
 return v;
}
static void sort_recordings(void){
 if(results)ListView_SortItems(results,compare_recordings,(LPARAM)(sort_column|(sort_descending<<8)));
}
static unsigned load_recordings(void){
 char path[4096];
 if(snprintf(path,sizeof(path),"%s\\candidate_unit_runs.csv",outpath)>=(int)sizeof(path))return 0;
 FILE *f=fopen(path,"rb");if(!f)return 0;
 char line[2048];unsigned count=0;
 fgets(line,sizeof(line),f);
 while(fgets(line,sizeof(line),f)){
  unsigned long long owner,base,end,anchor;unsigned units;
  char state[96];
  if(sscanf(line,"%llu,%llu,%llu,%u,%llu,%95[^\r\n]",&owner,&base,&end,&units,&anchor,state)!=6)continue;
  if(strcmp(state,"CANDIDATE_PREFIX_CONTINUITY_ONLY")||!units)continue;
  RecordingRow *r=(RecordingRow*)calloc(1,sizeof(*r));if(!r)break;
  r->owner=owner;r->base=base;r->end=end;r->anchor=anchor;r->units=units;
  snprintf(r->state,sizeof(r->state),"%s",state);
  r->hasdate=tentative_date(r->owner,r->date,sizeof(r->date));
  RecordingRow **grown=(RecordingRow**)realloc(record_rows,(record_count+1)*sizeof(*record_rows));
  if(!grown){free(r);break;}
  record_rows=grown;record_rows[record_count++]=r;
  char dt[64],id[48],nunits[32];
  if(r->hasdate){
   unsigned y=0,m=0,d=0,h=0,mi=0,sec=0;
   sscanf(r->date,"%4u_%2u_%2u_%2u%2u%2u",&y,&m,&d,&h,&mi,&sec);
   snprintf(dt,sizeof(dt),"%04u-%02u-%02u %02u:%02u:%02u *",y,m,d,h,mi,sec);
  }else snprintf(dt,sizeof(dt),"Unknown");
  snprintf(id,sizeof(id),"%llu",(unsigned long long)r->anchor);
  snprintf(nunits,sizeof(nunits),"%u",r->units);
  LVITEMA item={0};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=(int)count;item.pszText=dt;
  item.lParam=(LPARAM)(ULONG_PTR)r;
  int index=ListView_InsertItem(results,&item);
  if(index<0){record_count--;free(r);continue;}
  ListView_SetItemText(results,index,1,id);
  ListView_SetItemText(results,index,2,nunits);
  ListView_SetItemText(results,index,3,"Candidate / partial");
  count++;
 }
 fclose(f);sort_recordings();return count;
}
static DWORD WINAPI orphan_mp4_worker(LPVOID unused){
 (void)unused;
 char exe[MAX_PATH],dir[MAX_PATH],script[MAX_PATH],command[2*MAX_PATH+80];
 DWORD n=GetModuleFileNameA(NULL,exe,MAX_PATH);
 if(!n||n>=MAX_PATH){PostMessage(mainwnd,WM_ORPHAN_DONE,2,0);return 0;}
 strcpy(dir,exe);char *slash=strrchr(dir,'\\');if(!slash){PostMessage(mainwnd,WM_ORPHAN_DONE,2,0);return 0;}
 *slash=0;
 if(snprintf(script,sizeof(script),"%s\\Run-Recover-Playable.bat",dir)>=(int)sizeof(script)){
  PostMessage(mainwnd,WM_ORPHAN_DONE,2,0);return 0;
 }
 DWORD attrs=GetFileAttributesA(script);
 if(attrs==INVALID_FILE_ATTRIBUTES || (attrs&FILE_ATTRIBUTE_DIRECTORY)){
  PostMessage(mainwnd,WM_ORPHAN_DONE,3,0);return 0;
 }
 if(snprintf(command,sizeof(command),"cmd.exe /d /s /c \"\"%s\"\"",script)>=(int)sizeof(command)){
  PostMessage(mainwnd,WM_ORPHAN_DONE,2,0);return 0;
 }
 STARTUPINFOA si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
 BOOL ok=CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,dir,&si,&pi);
 if(!ok){PostMessage(mainwnd,WM_ORPHAN_DONE,4,0);return 0;}
 WaitForSingleObject(pi.hProcess,INFINITE);
 DWORD code=999;GetExitCodeProcess(pi.hProcess,&code);
 CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
 PostMessage(mainwnd,WM_ORPHAN_DONE,(WPARAM)code,0);return 0;
}
static void browsefile(void){OPENFILENAMEA d={0};char b[4096]="";d.lStructSize=sizeof(d);d.hwndOwner=mainwnd;d.lpstrFile=b;d.nMaxFile=sizeof(b);d.lpstrFilter="Disk images (*.dsk;*.img;*.dd;*.raw;*.bin)\0*.dsk;*.img;*.dd;*.raw;*.bin\0All files\0*.*\0";d.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;if(GetOpenFileNameA(&d))SetWindowTextA(source,b);}
static void browseout(void){
 BROWSEINFOA bi={0};char folder[MAX_PATH]={0};
 bi.hwndOwner=mainwnd;
 bi.lpszTitle="Select an output folder (no filename required)";
 bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
 LPITEMIDLIST pidl=SHBrowseForFolderA(&bi);
 if(!pidl)return;
 if(SHGetPathFromIDListA(pidl,folder))SetWindowTextA(dest,folder);
 CoTaskMemFree(pidl);
}
static LRESULT CALLBACK proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
 switch(m){
 case WM_CREATE:{mainwnd=w;
 control("STATIC","Source image",0,0,0,100,22,ID_LBL_SOURCE);
 source=control("EDIT","",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,0,0,100,27,ID_SOURCE);
 control("BUTTON","Browse...",WS_TABSTOP,0,0,94,27,ID_SOURCE_BROWSE);
 control("STATIC","Output folder",0,0,0,100,22,ID_LBL_DEST);
 dest=control("EDIT","",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,0,0,100,27,ID_DEST);
 control("BUTTON","Browse...",WS_TABSTOP,0,0,94,27,ID_DEST_BROWSE);
 analyze=control("BUTTON","Analyze Image",WS_TABSTOP,0,0,125,31,ID_ANALYZE);
 stopbtn=control("BUTTON","Stop",WS_TABSTOP,0,0,80,31,ID_STOP);EnableWindow(stopbtn,FALSE);
 exportbtn=control("BUTTON","Recover Selected",WS_TABSTOP,0,0,155,31,ID_EXPORT);EnableWindow(exportbtn,FALSE);
 allbtn=control("BUTTON","Select All",WS_TABSTOP,0,0,88,31,ID_SELECT_ALL);EnableWindow(allbtn,FALSE);
 clearbtn=control("BUTTON","Clear Selection",WS_TABSTOP,0,0,88,31,ID_CLEAR_ALL);EnableWindow(clearbtn,FALSE);
 orphanbtn=control("BUTTON","Recover Orphan MP4",WS_TABSTOP,0,0,150,31,ID_ORPHAN_MP4);
 progress=control(PROGRESS_CLASSA,"",0,0,0,100,15,ID_BAR);
 status=control("STATIC","Select a WolfBox sector image. FAT and recording dates are optional.",SS_CENTERIMAGE,0,0,100,25,ID_STATUS);
 control("STATIC","Recoverable recording candidates",0,0,0,100,20,ID_LBL_RESULTS);
 control("STATIC","* Embedded tkhd date, camera timezone/start time not yet verified. Unknown dates always appear last.",0,0,0,100,16,ID_LBL_DATE_NOTE);
 results=control(WC_LISTVIEWA,"",WS_BORDER|WS_TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS,0,0,100,185,ID_RESULTS);
 ListView_SetExtendedListViewStyle(results,LVS_EX_FULLROWSELECT|LVS_EX_CHECKBOXES|LVS_EX_DOUBLEBUFFER);
 {LVCOLUMNA c={0};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;
  c.pszText="Recording date / time";c.cx=215;c.iSubItem=0;ListView_InsertColumn(results,0,&c);
  c.pszText="Physical recording ID";c.cx=195;c.iSubItem=1;ListView_InsertColumn(results,1,&c);
  c.pszText="Units";c.cx=75;c.iSubItem=2;ListView_InsertColumn(results,2,&c);
  c.pszText="Recovery status";c.cx=165;c.iSubItem=3;ListView_InsertColumn(results,3,&c);
 }
 control("STATIC","Activity log",0,0,0,100,20,ID_LBL_ACTIVITY);
 logbox=control("EDIT","",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,100,95,ID_LOG);
 layout(w);
 return 0;}
 case WM_SIZE: if(source)layout(w); return 0;
 case WM_GETMINMAXINFO:{MINMAXINFO *mi=(MINMAXINFO*)lp;
  mi->ptMinTrackSize.x=scale(950);mi->ptMinTrackSize.y=scale(580);return 0;}
 case WM_NOTIFY:{NMHDR *hdr=(NMHDR*)lp;
  if(hdr&&hdr->idFrom==ID_RESULTS&&hdr->code==LVN_COLUMNCLICK){
   NMLISTVIEW *nm=(NMLISTVIEW*)lp;
   if(nm->iSubItem==sort_column)sort_descending=!sort_descending;
   else{sort_column=nm->iSubItem;sort_descending=0;}
   sort_recordings();return 0;
  }
  break;
 }
 case WM_COMMAND:switch(LOWORD(wp)){
 case ID_ORPHAN_MP4:
  if(threadhandle)return 0;
  operation=3;EnableWindow(orphanbtn,FALSE);EnableWindow(analyze,FALSE);EnableWindow(exportbtn,FALSE);
  SetWindowTextA(status,"Validating orphan HEVC and generating playable MP4 (see export report)...");
  logline("Running Run-Recover-Playable.bat from application folder. Only verified candidates are published.");
  threadhandle=CreateThread(NULL,0,orphan_mp4_worker,NULL,0,NULL);
  if(!threadhandle){operation=0;EnableWindow(orphanbtn,TRUE);EnableWindow(analyze,TRUE);EnableWindow(exportbtn,TRUE);}
  return 0;
 case ID_SOURCE_BROWSE:browsefile();return 0;
 case ID_DEST_BROWSE:browseout();return 0;
 case ID_ANALYZE:if(threadhandle)return 0;
   GetWindowTextA(source,sourcepath,sizeof(sourcepath));GetWindowTextA(dest,outpath,sizeof(outpath));
   if(!sourcepath[0]||!outpath[0]){MessageBoxA(w,"Choose an image and output folder.","WolfBox Recovery",MB_OK|MB_ICONWARNING);return 0;}
   InterlockedExchange(&cancel_flag,0);operation=1;stage_begin(1);
   {WIN32_FILE_ATTRIBUTE_DATA fd;if(GetFileAttributesExA(sourcepath,GetFileExInfoStandard,&fd))source_bytes=((uint64_t)fd.nFileSizeHigh<<32)|fd.nFileSizeLow;else source_bytes=0;}EnableWindow(analyze,FALSE);EnableWindow(exportbtn,FALSE);EnableWindow(stopbtn,TRUE);SendMessage(progress,PBM_SETPOS,0,0);
   clear_recordings();SetWindowTextA(logbox,"");SetWindowTextA(status,"Scanning image read-only...");
   threadhandle=CreateThread(0,0,worker,0,0,0);if(!threadhandle){EnableWindow(analyze,TRUE);EnableWindow(stopbtn,FALSE);}return 0;
 case ID_SELECT_ALL:{
   if(threadhandle)return 0;
   for(int i=0,n=ListView_GetItemCount(results);i<n;i++)ListView_SetCheckState(results,i,TRUE);return 0;
 }
 case ID_CLEAR_ALL:{if(!threadhandle)for(int i=0,n=ListView_GetItemCount(results);i<n;i++)ListView_SetCheckState(results,i,FALSE);return 0;}
 case ID_EXPORT:{
   if(threadhandle)return 0;
   int total=0,n=ListView_GetItemCount(results);
   for(int i=0;i<n;i++)if(ListView_GetCheckState(results,i))total++;
   if(total<=0){MessageBoxA(w,"Check one or more recording candidates.","WolfBox Recovery",MB_OK|MB_ICONINFORMATION);return 0;}
   uint64_t *anchors=(uint64_t*)calloc((size_t)total,sizeof(uint64_t));
   if(!anchors)return 0;
   int got=0;
   for(int i=0;i<n;i++){
    if(!ListView_GetCheckState(results,i))continue;
    LVITEMA item={0};item.mask=LVIF_PARAM;item.iItem=i;
    if(!ListView_GetItem(results,&item)||!item.lParam){free(anchors);return 0;}
    RecordingRow *r=(RecordingRow*)(ULONG_PTR)item.lParam;
    anchors[got++]=r->anchor;
   }
   GetWindowTextA(source,sourcepath,sizeof(sourcepath));GetWindowTextA(dest,outpath,sizeof(outpath));
   if(!sourcepath[0]||!outpath[0]){free(anchors);return 0;}
   batch_anchors=anchors;batch_count=(unsigned)total;
   InterlockedExchange(&cancel_flag,0);operation=2;EnableWindow(analyze,FALSE);EnableWindow(exportbtn,FALSE);EnableWindow(allbtn,FALSE);EnableWindow(clearbtn,FALSE);EnableWindow(stopbtn,TRUE);
   SetWindowTextA(status,"Batch exporting verified recording runs...");
   threadhandle=CreateThread(0,0,export_worker,0,0,0);
   if(!threadhandle){operation=0;EnableWindow(analyze,TRUE);EnableWindow(exportbtn,TRUE);EnableWindow(allbtn,TRUE);EnableWindow(clearbtn,TRUE);free(batch_anchors);batch_anchors=NULL;batch_count=0;}
   return 0;
 }
 case ID_STOP:
  if(operation==1||operation==2){
   InterlockedExchange(&cancel_flag,1);
   EnableWindow(stopbtn,FALSE);
   SetWindowTextA(status,"Stop requested; finishing current read and leaving completed MP4s intact...");
   logline("Stop requested by operator.");
  }
  return 0;
 }break;
 case WM_SCAN_EVENT:{Event *e=(Event*)lp;if(e){
  if(!strcmp(e->type,"STAGE")){
    stage_begin(strstr(e->details,"2/3")?2:strstr(e->details,"3/3")?3:1);
    SetWindowTextA(status,e->details);logline(e->details);
   }
  else if(!strcmp(e->type,"PROGRESS")){show_progress(e->offset,source_bytes,"Scanning source bytes");}
   else if(!strcmp(e->type,"MATCH_PROGRESS")){
    unsigned long long done=0,total=0,unique=0;
    if(sscanf(e->details,"%llu/%llu | %llu",&done,&total,&unique)==3){
     char name[100];snprintf(name,sizeof(name),"Matching media (%llu mapped)",unique);
     show_progress(done,total,name);
    }
   }
   else if(!strcmp(e->type,"PREP_PROGRESS")){show_progress(e->offset,2,"Preparing recording inventory");}
  else{char s[512];snprintf(s,sizeof(s),"%s at %llu: %s",e->type,(unsigned long long)e->offset,e->details);if(strcmp(e->type,"MOOV") && strcmp(e->type,"DATE_FIELD"))logline(s);}
  free(e->type);free(e->details);free(e);}return 0;}
 case WM_SCAN_DONE:{int r=(int)(INT_PTR)wp;if(threadhandle){CloseHandle(threadhandle);threadhandle=0;}operation=0;EnableWindow(analyze,TRUE);EnableWindow(stopbtn,FALSE);
  if(r==0){
   clear_recordings();
   unsigned count=load_recordings();
   char message[200];snprintf(message,sizeof(message),"Analysis complete. %u recording runs available.",count);
   SetWindowTextA(status,message);
   logline("Analysis ready. Select recording runs for partial MP4 recovery; dates are optional and unverified.");
   EnableWindow(exportbtn,count>0);EnableWindow(allbtn,count>0);EnableWindow(clearbtn,count>0);
  }
  else if(r==1){SetWindowTextA(status,"Stopped. Partial results retained.");logline("Stopped; data is incomplete.");}
  else{char s[150];snprintf(s,sizeof(s),"Scan failed (error %d). Check paths and permissions.",r);SetWindowTextA(status,s);logline(s);}
  return 0;}
 case WM_ORPHAN_DONE:{
  DWORD code=(DWORD)wp;
  if(threadhandle){CloseHandle(threadhandle);threadhandle=NULL;}
  operation=0;EnableWindow(orphanbtn,TRUE);EnableWindow(analyze,TRUE);
  EnableWindow(exportbtn,record_count>0);
  char msg[512];
  if(code==0)snprintf(msg,sizeof(msg),"Orphan MP4 export completed. See Orphan_v215_Output\\playable_export_results.csv");
  else snprintf(msg,sizeof(msg),"Orphan MP4 export failed (code %lu). See Orphan_v215_Output\\playable_export_results.csv and decode logs.",(unsigned long)code);
  SetWindowTextA(status,msg);logline(msg);
  return 0;
 }
 case WM_BATCH_EVENT:{char *msg=(char*)lp;if(msg){logline(msg);SetWindowTextA(status,msg);free(msg);}return 0;}
 case WM_EXPORT_DONE:{
  int r=(int)(INT_PTR)wp;
  if(threadhandle){CloseHandle(threadhandle);threadhandle=0;}
  operation=0;EnableWindow(analyze,TRUE);EnableWindow(exportbtn,TRUE);EnableWindow(allbtn,TRUE);EnableWindow(clearbtn,TRUE);EnableWindow(stopbtn,FALSE);
  free(batch_anchors);batch_anchors=NULL;batch_count=0;
  if(r==9){SetWindowTextA(status,"Batch stopped. Completed files retained; incomplete temporary MP4 removed.");logline("Batch cancelled by operator.");}
  else if(r==0){SetWindowTextA(status,"Batch finished. See batch_results.csv.");logline("All selected recording-run exports completed or safely skipped.");}
  else{SetWindowTextA(status,"Batch finished with failures. See batch_results.csv.");logline("Some exports failed or were refused. Other recordings continued.");}
  return 0;
 }
 case WM_CLOSE:if(threadhandle){MessageBoxA(w,"Use Stop to cancel the active operation before closing.","Scan running",MB_OK|MB_ICONINFORMATION);return 0;}DestroyWindow(w);return 0;
 case WM_DESTROY:clear_recordings();PostQuitMessage(0);return 0;
 }return DefWindowProc(w,m,wp,lp);
}
int WINAPI WinMain(HINSTANCE h,HINSTANCE prev,LPSTR cmd,int show){(void)prev;(void)cmd;
 HDC dc=GetDC(NULL);if(dc){dpi_x=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(NULL,dc);}
 if(dpi_x<72||dpi_x>384)dpi_x=96;
 INITCOMMONCONTROLSEX cc={sizeof(cc),ICC_PROGRESS_CLASS};InitCommonControlsEx(&cc);
 WNDCLASSA wc={0};wc.lpfnWndProc=proc;wc.hInstance=h;wc.lpszClassName="WolfBoxRecoveryStandalone";wc.hCursor=LoadCursor(0,IDC_ARROW);wc.hIcon=LoadIconA(h,MAKEINTRESOURCEA(ID_ICON));wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);
 if(!RegisterClassA(&wc))return 2;
 mainwnd=CreateWindowExA(0,wc.lpszClassName,"WolfBox Recovery 2.16",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,scale(1100),scale(670),0,0,h,0);
 if(!mainwnd) return 3;
 ShowWindow(mainwnd,show);UpdateWindow(mainwnd);
 MSG msg;
 while(GetMessage(&msg,0,0,0)>0){TranslateMessage(&msg);DispatchMessage(&msg);}
 return (int)msg.wParam;
}
#endif
