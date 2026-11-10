#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <math.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>
#include <rsx/rsx.h>
#include <io/pad.h>

static gcmContextData *ctx;
static u32 W=1280, H=720, *fb[2], fboff[2], *back;
static int cur=0;
static volatile int running=1;
static void cb(u64 s,u64 p,void*u){ if(s==SYSUTIL_EXIT_GAME) running=0; }

static void rect(int x,int y,int w,int h,u32 c){
  int x0=x<0?0:x, y0=y<0?0:y, x1=x+w, y1=y+h;
  if(x1>(int)W)x1=W; if(y1>(int)H)y1=H;
  if(x0>=x1||y0>=y1) return;
  for(int yy=y0;yy<y1;yy++){ u32 *p=back+yy*W; for(int xx=x0;xx<x1;xx++) p[xx]=c; }
}
static void circle(int cx,int cy,int r,u32 c){
  for(int y=-r;y<=r;y++) for(int x=-r;x<=r;x++) if(x*x+y*y<=r*r){
    int px=cx+x, py=cy+y;
    if(px>=0&&px<(int)W&&py>=0&&py<(int)H) back[py*W+px]=c;
  }
}
typedef struct{char c; const char *p;} Glyph;
static const Glyph glyphs[]={
 {'0',"###" "#.#" "#.#" "#.#" "###"},{'1',".#." "##." ".#." ".#." "###"},{'2',"###" "..#" "###" "#.." "###"},{'3',"###" "..#" "###" "..#" "###"},{'4',"#.#" "#.#" "###" "..#" "..#"},{'5',"###" "#.." "###" "..#" "###"},{'6',"###" "#.." "###" "#.#" "###"},{'7',"###" "..#" "..#" "..#" "..#"},{'8',"###" "#.#" "###" "#.#" "###"},{'9',"###" "#.#" "###" "..#" "###"},
 {'A',".#." "#.#" "###" "#.#" "#.#"},{'B',"##." "#.#" "##." "#.#" "##."},{'C',"###" "#.." "#.." "#.." "###"},{'D',"##." "#.#" "#.#" "#.#" "##."},{'E',"###" "#.." "###" "#.." "###"},{'F',"###" "#.." "###" "#.." "#.."},{'G',"###" "#.." "#.#" "#.#" "###"},{'H',"#.#" "#.#" "###" "#.#" "#.#"},{'I',"###" ".#." ".#." ".#." "###"},{'L',"#.." "#.." "#.." "#.." "###"},{'M',"#.#" "###" "###" "#.#" "#.#"},{'N',"#.#" "###" "###" "#.#" "#.#"},{'O',"###" "#.#" "#.#" "#.#" "###"},{'P',"###" "#.#" "###" "#.." "#.."},{'R',"##." "#.#" "##." "#.#" "#.#"},{'S',"###" "#.." "###" "..#" "###"},{'T',"###" ".#." ".#." ".#." ".#."},{'U',"#.#" "#.#" "#.#" "#.#" "###"},{'V',"#.#" "#.#" "#.#" "#.#" ".#."},{'X',"#.#" "#.#" ".#." "#.#" "#.#"},{'Y',"#.#" "#.#" ".#." ".#." ".#."},{'!',"###" ".#." ".#." "..." ".#."},{':',"..." ".#." "..." ".#." "..."},{'.',"..." "..." "..." "..." ".#."},{'-',"..." "..." "###" "..." "..."},{' ',"..." "..." "..." "..." "..."} };
static const char *find_g(char c){ for(unsigned i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++) if(glyphs[i].c==c) return glyphs[i].p; return NULL; }
static void text(const char *s,int x,int y,int sc,u32 c){ for(;*s;s++){ const char *g=find_g(*s); if(g) for(int r=0;r<5;r++) for(int k=0;k<3;k++) if(g[r*3+k]=='#') rect(x+k*sc,y+r*sc,sc,sc,c); x+=4*sc; } }
static void textC(const char *s,int y,int sc,u32 c){ int w=strlen(s)*4*sc; text(s,(W-w)/2,y,sc,c); }

int main(){
  void *host=memalign(1024*1024, 32*1024*1024);
  rsxInit(&ctx, 0x100000, 32*1024*1024, host);
  videoConfiguration vc; memset(&vc,0,sizeof(vc));
  vc.resolution=VIDEO_RESOLUTION_720; vc.format=VIDEO_BUFFER_FORMAT_XRGB; vc.aspect=VIDEO_ASPECT_AUTO; vc.pitch=1280*4;
  videoConfigure(VIDEO_PRIMARY,&vc,NULL,0);
  gcmSetFlipMode(GCM_FLIP_VSYNC);
  for(int i=0;i<2;i++){ fb[i]=(u32*)rsxMemalign(64,1280*720*4); gcmAddressToOffset(fb[i],&fboff[i]); gcmSetDisplayBuffer(i,fboff[i],1280*4,1280,720); }
  back=(u32*)memalign(64,1280*720*4);
  ioPadInit(7);
  sysUtilRegisterCallback(0,cb,NULL);

  int state=0;
  float birdY=360, vel=0;
  int pipeX[3]={900,1350,1800};
  int pipeGapY[3]={320,400,280};
  int pipeSpeed=7;
  int score=0, best=0, frame=0;
  int prevCross=0, prevStart=0;
  int groundOff=0;
  int flash=0;

  srand(999);
  while(running){
    sysUtilCheckCallback(); frame++;
    padInfo info; padData d; memset(&d,0,sizeof(d));
    ioPadGetInfo(&info); if(info.status[0]) ioPadGetData(0,&d);
    int cross=d.BTN_CROSS, start=d.BTN_START;

    if(state==0 && start && !prevStart){
      state=1; birdY=360; vel=0; score=0; pipeSpeed=7; pipeX[0]=900; pipeX[1]=1350; pipeX[2]=1800;
      for(int i=0;i<3;i++) pipeGapY[i]=220+rand()%280;
    }
    if(state==2 && cross && !prevCross){
      state=1; birdY=360; vel=0; score=0; pipeSpeed=7; pipeX[0]=900; pipeX[1]=1350; pipeX[2]=1800;
      for(int i=0;i<3;i++) pipeGapY[i]=220+rand()%280;
    }
    prevStart=start;

    if(state==1){
      if(cross && !prevCross){ vel=-12.5f; }
      prevCross=cross;
      vel+=0.75f; if(vel>13) vel=13;
      birdY+=vel;
      groundOff=(groundOff+pipeSpeed)%40;
      for(int i=0;i<3;i++){
        pipeX[i]-=pipeSpeed;
        if(pipeX[i]<-100){
          pipeX[i]=1250; pipeGapY[i]=220+rand()%280; score++;
          if(score>best) best=score;
          if(score%5==0 && pipeSpeed<11) pipeSpeed++;
        }
        int bx=300, by=(int)birdY;
        if(bx+28>pipeX[i] && bx-28<pipeX[i]+80){
          if(by-24<pipeGapY[i]-85 || by+24>pipeGapY[i]+85){ state=2; flash=6; }
        }
      }
      if(birdY>665 || birdY<0){ state=2; flash=6; }
    } else { prevCross=cross; if(state==0) groundOff=(groundOff+2)%40; }
    if(flash>0) flash--;

    u32 sky = flash>0 ? 0x00FFFFFF : 0x0070C5CE;
    for(int i=0;i<1280*720;i++) back[i]=sky;
    circle(1100,100,55,0x00FFEE88);
    int cloudX[3]={200,700,1100};
    for(int i=0;i<3;i++){
      int cx=cloudX[i], cy=100+i*60;
      circle(cx,cy,35,0x00FFFFFF); circle(cx+35,cy,45,0x00FFFFFF); circle(cx+70,cy,35,0x00FFFFFF);
    }
    for(int i=0;i<3;i++){
      int gap=pipeGapY[i];
      rect(pipeX[i],0,80,gap-85,0x0000AA00);
      rect(pipeX[i]-6,gap-105,92,20,0x00008800);
      rect(pipeX[i],gap+85,80,720-gap-85,0x0000AA00);
      rect(pipeX[i]-6,gap+85,92,20,0x00008800);
      rect(pipeX[i]+12,0,10,gap-85,0x0033DD33);
      rect(pipeX[i]+12,gap+85,10,720-gap-85,0x0033DD33);
    }
    rect(0,670,1280,50,0x00DED895);
    rect(0,670,1280,10,0x0000AA00);
    for(int x=-40;x<1320;x+=40) rect(x+groundOff,685,20,10,0x00C0B87A);
    {
      int bx=300, by=(int)birdY;
      rect(bx-38,by+5,20,10,0x00FF4400);
      circle(bx,by,28,0x00FFFF00);
      circle(bx+5,by+10,18,0x00FFEE99);
      int wingY = (frame%12<6) ? -8 : 2;
      rect(bx-18,by+wingY,32,14,0x00FF8800);
      circle(bx+16,by-8,9,0x00FFFFFF); circle(bx+19,by-8,4,0x00000000);
      rect(bx+22,by+2,20,9,0x00FF4400);
    }
    if(state==1){
      char buf[32]; snprintf(buf,sizeof(buf),"%d",score);
      textC(buf,50,12,0x00FFFFFF);
      char b2[64]; snprintf(b2,sizeof(b2),"BEST %d",best); text(b2,20,20,5,0x00FFFFFF);
      char b3[64]; snprintf(b3,sizeof(b3),"VEL %d",pipeSpeed); text(b3,1100,20,5,0x00FFFF00);
    }
    if(state==0){
      rect(200,120,880,460,0x00000000); rect(210,130,860,440,0x00FFFF00);
      textC("FLAPPY BIRD",180,12,0x00000000);
      textC("X = VOAR",300,7,0x00000000);
      textC("QUANTO MAIS PONTOS",370,6,0x00FF0000);
      textC("MAIS RAPIDO FICA!",410,6,0x00FF0000);
      if(frame%40<25) textC("APERTE START",490,7,0x00000000);
    }
    if(state==2){
      rect(200,180,880,360,0x00000000); rect(210,190,860,340,0x00FFFFFF);
      textC("VOCE BATEU!",230,11,0x00FF0000);
      char buf[64]; snprintf(buf,sizeof(buf),"SCORE %d  BEST %d",score,best); textC(buf,360,7,0x00000000);
      if(frame%40<25) textC("APERTE X P/ TENTAR DE NOVO",440,6,0x0000AA00);
    }

    memcpy(fb[cur],back,1280*720*4);
    gcmSetFlip(ctx,cur); rsxFlushBuffer(ctx); gcmSetWaitFlip(ctx); cur^=1;
    usleep(16000);
  }
  return 0;
}