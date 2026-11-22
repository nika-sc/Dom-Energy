#pragma once
// Included after the CSV/cache helpers. Never hold a File across loop passes:
// writeSd may append meanwhile; read only the sizes captured at job start.
struct RangeReadState {
  bool active=false,failed=false,previous=false,discard=false;
  long from=0,to=0,pt=0;
  float pe=0;
  unsigned long gen=0,finishedAt=0;
  int count=0,file=0,files=0,carry=0,retries=0;
  char paths[3][40];
  char* buffer=nullptr;
  size_t sizes[3],position=0;
  RngDay* days=nullptr;
};
struct RangeReadSaved {
  long from=0,to=0;
  unsigned long gen=0,at=0;
  RngDay days[RNG_MAX];
};
static RangeReadState sRange;
static RangeReadSaved* sRangeSaved=nullptr;
static int sRangeNext=0;

static void rangeProcessLine(char* text) {
  long rt;float v,p,e,c,tm;
  if(!csvRow(String(text),&rt,&v,&p,&e,&c,&tm))return;
  if(rt<sRange.from-86400L || rt>sRange.to+2*86400L-1){sRange.previous=false;return;}
  uint32_t day=(uint32_t)(rt/86400L);
  int index=dayIndexOf(sRange.days,sRange.count,day);
  if(index<0)return;
  if(sRange.previous && (uint32_t)(sRange.pt/86400L)!=day)sRange.previous=false;
  RngDay& value=sRange.days[index];
  if(e>0 && sRange.previous && rt>sRange.pt && e>=sRange.pe){
    float delta=e-sRange.pe;
    if(delta>0 && delta<100){
      float dd,nn;uint8_t h1,h2;tarForEpoch(sRange.pt,&dd,&nn,&h1,&h2);
      float nf=nightFrac(sRange.pt,rt,h1,h2);
      value.kwh+=delta;value.nightKwh+=delta*nf;value.dayKwh+=delta*(1-nf);
    }
  }
  if(v>50){if(v<value.vmin)value.vmin=(uint16_t)v;if(v>value.vmax)value.vmax=(uint16_t)v;}
  if(tm>=-60 && tm<=125){if(tm*10<value.tmin)value.tmin=(int16_t)(tm*10);if(tm*10>value.tmax)value.tmax=(int16_t)(tm*10);}
  if(p>value.pmax)value.pmax=(uint16_t)p;
  if(c>0)value.cost=c;
  value.rows++;if(e>0){sRange.previous=true;sRange.pt=rt;sRange.pe=e;}
}

static void rangeReadFinish() {
  sRange.active=false;sRange.finishedAt=millis();
  if(sRange.failed)return;
  RangeReadSaved& saved=sRangeSaved[sRangeNext];sRangeNext=(sRangeNext+1)%3;
  saved.from=sRange.from;saved.to=sRange.to;saved.gen=sRange.gen;saved.at=millis();
  memcpy(saved.days,sRange.days,sizeof(saved.days));
  long today=nowLocalEpoch()/86400L;
  for(int i=0;i<sRange.count;++i)if(sRange.days[i].day<(uint32_t)today)dayCachePut(sRange.days[i]);
  dayCacheFlush(false);
}

static void rangeReadStep(File& reader) {
  if(!sRange.active)return;
  size_t want=sRange.sizes[sRange.file]-sRange.position;
  if(want>4096)want=4096;
  int got=want && reader ? reader.read((uint8_t*)sRange.buffer+sRange.carry,want):0;
  if(want && got<=0){if(++sRange.retries>=3){sRange.failed=true;rangeReadFinish();}return;}
  sRange.retries=0;sRange.position+=got;
  int total=sRange.carry+got,start=0;
  for(int i=0;i<total;++i)if(sRange.buffer[i]=='\n'){
    sRange.buffer[i]=0;
    if(!sRange.discard)rangeProcessLine(sRange.buffer+start);
    sRange.discard=false;start=i+1;
  }
  sRange.carry=total-start;
  if(sRange.carry>256){sRange.carry=0;sRange.discard=true;}
  else if(sRange.carry)memmove(sRange.buffer,sRange.buffer+start,sRange.carry);
  if(sRange.position>=sRange.sizes[sRange.file]){
    if(sRange.carry && !sRange.discard){sRange.buffer[sRange.carry]=0;rangeProcessLine(sRange.buffer);}
    sRange.carry=0;sRange.discard=false;sRange.previous=false;sRange.position=0;
    if(++sRange.file>=sRange.files)rangeReadFinish();
  }
  esp_task_wdt_reset();
}

static void rangeTick() {
  if(!sRange.active)return;
  if(sRange.gen!=gSdGen){sRange.active=false;sRange.failed=false;return;}
  int index=sRange.file;
  File reader=SD.open(sRange.paths[index],FILE_READ);
  if(reader && !reader.seek(sRange.position))reader.close();
  unsigned long started=millis();
  for(int step=0;step<8 && sRange.active && sRange.file==index && (step==0 || millis()-started<12UL);++step)rangeReadStep(reader);
  reader.close();
}

// 1 complete, 0 queued/in progress, -1 explicit I/O failure. Other clients
// cannot cancel a running scan; three completed periods prevent starvation.
static int rangeObtain(long from,long to,RngDay* output) {
  if(!gSdOk)return -1;
  // ESP32 has a much smaller linkable static DRAM window than its total RAM.
  // Allocate the bounded reader only when needed; fail visibly on low memory.
  if(!sRange.buffer)sRange.buffer=(char*)malloc(4353);
  if(!sRange.days)sRange.days=(RngDay*)malloc(sizeof(RngDay)*RNG_MAX);
  if(!sRangeSaved)sRangeSaved=(RangeReadSaved*)calloc(3,sizeof(RangeReadSaved));
  if(!sRange.buffer || !sRange.days || !sRangeSaved)return -1;
  long today=nowLocalEpoch()/86400L;
  for(int i=0;i<3;++i){const RangeReadSaved& saved=sRangeSaved[i];
    if(saved.from==from && saved.to==to && saved.gen==gSdGen && (to/86400L<today || millis()-saved.at<30000UL)){
      memcpy(output,saved.days,sizeof(saved.days));return 1;
    }
  }
  if(sRange.active)return 0;
  if(sRange.failed && sRange.from==from && sRange.to==to){sRange.failed=false;return -1;}
  sRange.from=from;sRange.to=to;sRange.gen=gSdGen;sRange.count=(to-from)/86400L+1;
  sRange.failed=false;sRange.previous=false;sRange.discard=false;sRange.files=0;sRange.file=0;sRange.position=0;sRange.carry=0;sRange.retries=0;
  for(int i=0;i<sRange.count;++i)sRange.days[i].day=from/86400L+i;
  rangeReset(sRange.days,sRange.count);
  // A complete past-day cache needs no SD scan. Never combine cached and
  // scanned days: that would double energy in partially cached periods.
  bool cached=to/86400L<today;
  for(int i=0;cached && i<sRange.count;++i)if(!dayCacheGet(sRange.days[i].day))cached=false;
  if(cached){
    for(int i=0;i<sRange.count;++i){DayCache* d=dayCacheGet(sRange.days[i].day);RngDay& a=sRange.days[i];
      a.kwh=d->kwh;a.cost=d->cost;a.dayKwh=d->dayKwh;a.nightKwh=d->nightKwh;a.vmin=d->vmin;a.vmax=d->vmax;a.pmax=d->pmax;a.tmin=d->tmin;a.tmax=d->tmax;a.rows=d->rows;
    }
    rangeReadFinish();memcpy(output,sRange.days,sizeof(RngDay)*RNG_MAX);return 1;
  }
  int y1,m1,d1,y2,m2,d2;String first=dateFromEpoch(from),last=dateFromEpoch(to);
  sscanf(first.c_str(),"%d-%d-%d",&y1,&m1,&d1);sscanf(last.c_str(),"%d-%d-%d",&y2,&m2,&d2);
  struct tm local;int current=getLocalTime(&local,5)?(local.tm_year+1900)*100+local.tm_mon+1:0;
  for(int y=y1,m=m1;y<y2 || (y==y2 && m<=m2);){
    char path[40];if(y*100+m==current)strcpy(path,"/energy.csv");else snprintf(path,sizeof(path),"/energy-%04d-%02d.csv",y,m);
    if(SD.exists(path)){
      File probe=SD.open(path,FILE_READ);
      if(!probe){sRange.failed=true;rangeReadFinish();return -1;}
      if(sRange.files>=3){probe.close();return -1;}
      strcpy(sRange.paths[sRange.files],path);sRange.sizes[sRange.files++]=probe.size();probe.close();
    }
    if(++m>12){m=1;++y;}
  }
  sRange.active=true;
  if(!sRange.files){rangeReadFinish();memcpy(output,sRange.days,sizeof(RngDay)*RNG_MAX);return 1;}
  return 0;
}
