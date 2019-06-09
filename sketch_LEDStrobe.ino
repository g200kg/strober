#include <stdio.h>
#include <stdlib.h>
#include <EEPROM.h>
#include <U8glib.h>
#include <MsTimer2.h>
#include <avr/pgmspace.h> 
#include <Wire.h>
#include <avr/io.h> 
#include <avr/interrupt.h>

U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_DEV_0 | U8G_I2C_OPT_NO_ACK | U8G_I2C_OPT_FAST); // Fast I2C / TWI


#define ENCA  2
#define ENCB  3
#define ENCBTN 4

#define LEDA  17
#define LEDB  5

#define IN1   6
#define IN2   8
#define IN3   7
#define IN4   9

#define OUT1  13
#define OUT2  12
#define OUT3  11
#define OUT4  10

#define LINEIN 0
#define MICIN  1
#define PARAMIN 2

#define ISR_INTERVAL 8
#define MAX_PATTERN 8
#define PROG_ID (((long)'S'<<24)|((long)'t'<<16)|((long)'r'<<8)|'1')

void saveEep();
void initAll();


typedef struct {
  int duty;
  int step;
  char patternTab[16];
} Pattern;

typedef struct {
  unsigned long int id;
  Pattern pattern[MAX_PATTERN];
  volatile int mode;
  volatile int pat;
  volatile int flashBpm;
  volatile int audioIn;
  volatile int reTrig;
  volatile int clock;
  volatile int autoTrig;
  volatile int speed;
  volatile int assign;
} SetupData;

const unsigned char setupFactory[] PROGMEM ={
  'S', 't', 'r', '1',
  50,  0,  8, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  50,  0,  8, 0, 0x0f, 0x00, 0xf0, 0x00, 0x0f, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  50,  0,  8, 0, 0x03, 0x0c, 0x30, 0xc0, 0xc0, 0x30, 0x0c, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  100, 0,  8, 0, 0x03, 0x0f, 0x3f, 0xff, 0x3f, 0x0f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  100, 0,  8, 0, 0x00, 0x03, 0x0f, 0x3f, 0xff, 0xfc, 0xf0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  50,  0, 12, 0, 0xcc, 0x33, 0xcc, 0x33, 0xc0, 0x30, 0x0c, 0x03, 0x0f, 0x3f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
  50,  0, 16, 0, 0xff, 0x00, 0xf0, 0x0f, 0x00, 0xcc, 0x33, 0x00, 0xf0, 0x00, 0x0f, 0xf0, 0x00, 0x33, 0xcc, 0x00,
  90,  0, 16, 0, 0x0f, 0x03, 0x3f, 0xcc, 0x33, 0x0c, 0x0c, 0x03, 0xf0, 0xc0, 0xfc, 0x33, 0x30, 0xc0, 0xf0, 0x00,
  0, 0,     //mode
  0, 0,     //pat
  120, 0,   //bpm
  0, 0,     //audioin
  0, 0,     //retrig
  0, 0,     //clock
  64, 0,    //autotrig
  5, 0,     //speed
  0, 0,     //assign
};

SetupData setupData;

int flashStep = -1;
int flashCountMax;
int flashCount = -1;
int dutyCountMax;
int dutyCount = -1;
int dutyCount1 =0;
int dutyCount2 =0;
int dutyCount3 =0;
int dutyCount4 =0;

int autoTrigCount = 1;

int currentPat;
int audioVol;

int ledVal=0;
int isrCount=0;
volatile int clockInterval=1;
int btnCount=0;

class MenuData {
public:
  char type;
  char ena;
  char* title;
  int minVal;
  int maxVal;
  char offset;
  char p1;
  volatile int* val;
};

int cur_MENU = 0;
int cur_SAVE = 0;
int cur_INIT = 0;
int cur_dummy = 0;
#define M_EDITSTEP 7
#define M_EDITDUTY 8

MenuData menuData[] = {
/* typ  ena   title                minVal maxVal offset   p1  val*/
  {'S', 7, (char*)"MENU",             0,     0,     0,    0, &cur_MENU,          }, //0
  {'S', 7, (char*)" Mode",            0,     0,     0,    0, &setupData.mode,    },
  {'C', 7, (char*)"  Pat",            0,     0,     0,    0, 0,                  },
  {'C', 7, (char*)"  Dir",            0,     0,     0,    0, 0,                  },
  {'C', 7, (char*)"  Lev",            0,     0,     0,    0, 0,                  },
  {'P', 1, (char*)" Pattern",         0,     0,     0,    0, &setupData.pat,     }, //5
  {'E', 1, (char*)"  Edit",           0,     0,     0,    0, 0,                  },
  {'V', 1, (char*)"  EditStep",       1,    16,     0,    6, 0,                  },
  {'V', 1, (char*)"  EditDuty",       1,   100,     0,    6, 0,                  },
  {'S', 5, (char*)" Clock",           0,     0,     0,    0, &setupData.clock,   },
  {'C', 5, (char*)"  Int",            0,     0,     0,    0, 0,                  }, //10
  {'C', 5, (char*)"  Ext",            0,     0,     0,    0, 0,                  },
  {'V', 1, (char*)" ClkBPM",         30,   300,     0,    0, &setupData.flashBpm,},
  {'S', 1, (char*)" ReTrig",          0,     0,     0,    0, &setupData.reTrig,  },
  {'C', 1, (char*)"  Off",            0,     0,     0,    0, 0,                  },
  {'C', 1, (char*)"  On",             0,     0,     0,    0, 0,                  }, //15
  {'V', 1, (char*)" AutoTrig",        0,   100,     0,    0, &setupData.autoTrig,},
  {'S', 5, (char*)" AudioIn",         0,     0,     0,    0, &setupData.audioIn, },
  {'C', 5, (char*)"  Mic",            0,     0,     0,    0, 0,                  },
  {'C', 5, (char*)"  Lin",            0,     0,     0,    0, 0,                  },
  {'V', 4, (char*)" Speed",           1,    20,     0,    0, &setupData.speed,   }, //20
  {'S', 2, (char*)" Assign",          0,     0,     0,    0, &setupData.assign,  },
  {'C', 2, (char*)"  4:4",            0,     0,     0,    0, 0,                  },
  {'C', 2, (char*)"  2:4",            0,     0,     0,    0, 0,                  },
  {'C', 2, (char*)"  1:4",            0,     0,     0,    0, 0,                  },
  {'s', 7, (char*)" Save",            0,     0,     0,    0, &cur_SAVE,          }, //25
  {'C', 7, (char*)"  OK",             0,     0,     0,   27, (int*)saveEep       },
  {'s', 7, (char*)"   Save Complete", 0,     0,     0,    0, &cur_dummy          },
  {'C', 7, (char*)"    OK",           0,     0,     0,    0, 0,                  },
  {'C', 7, (char*)"  Cancel",         0,     0,     0,    0, 0,                  },
  {'s', 7, (char*)" InitAll",         0,     0,     0,    0, &cur_INIT           }, //30
  {'C', 7, (char*)"  OK",             0,     0,     0,   32, (int*)initAll       },
  {'s', 7, (char*)"   Init Complate", 0,     0,     0,    0, &cur_dummy          },
  {'C', 7, (char*)"    OK",           0,     0,     0,    0, 0,                  },
  {'C', 7, (char*)"  Cancel",         0,     0,     0,    0, 0,                  },
};

#define menuMax (sizeof(menuData)/sizeof(MenuData))

int currentPage = 0;
int patOffset = 0;
int editCur = 0;

int GetIndent(int n){
  if(n >= menuMax)
    return 0;
  char* p = menuData[n].title;
  int i = 0;
  while(*p && *p<=' ')
    ++i,++p;
  return i;
}
char* GetStr(int n){
  char*p = menuData[n].title;
  while(*p==' ')
    ++p;
  return p;
}
int GetNextItem(int n){
  int indent = GetIndent(n);
  char f = 1 << setupData.mode;
  for(;;){
    if(n>=menuMax)
      return -1;
    ++n;
    int ind = GetIndent(n);
    if((menuData[n].ena & f)==0)
      continue;
    if(ind == indent)
      return n;
    if(ind < indent)
      return -1;
  }
}
int GetSubIndex(int p, int v){
  int i = p + 1;
  while(i < menuMax && v--){
    i = GetNextItem(i);
    if(i < 0)
      return -1;
  }
  return i;
}
void DispMenu(){
  switch(menuData[currentPage].type){
  case 's':
  case 'S': {
      u8g.firstPage();
      do {
        int i, idx, y;
        char s[8];
        MenuData* p = &menuData[currentPage];
        int v = *p->val;
        int offset = p->offset;
        u8g.drawStr(0,15, GetStr(currentPage));
        for(i = 0; (idx = GetSubIndex(currentPage, i)) >= 0; ++i){
          MenuData* q = &menuData[idx];
          if(i >= offset && i < offset + 3){
            y = 31 + (i - offset) * 16;
            u8g.drawStr(16, y, GetStr(idx));
            if(q->type =='V'){
              sprintf(s,"%3d",*q->val);
              u8g.drawStr(100, y, s);
            }
            if(q->type =='P'){
              sprintf(s,"%3d",*q->val + 1);
              u8g.drawStr(100, y, s);
            }
            if(q->type =='S'){
              int ii = GetSubIndex(idx, *q->val);
              u8g.drawStr(100, y, GetStr(ii));
            }
          }
        }
        y = v - offset + 1;
        if(y >= 1){
          y *= 16;
          u8g.drawTriangle(0, y, 0, y + 15, 10, y + 7);
        }
        
      }while(u8g.nextPage());
    }
    break;
  case 'V':{
      char s[16];
      u8g.firstPage();
      MenuData* p = &menuData[currentPage];
      int x = ((*p->val) - p->minVal) * 100 / (p->maxVal - p->minVal + 1);
      sprintf(s, "%d", *p->val);
      do {
        u8g.drawStr(0,15, GetStr(currentPage));
        u8g.drawStr(16,33, s);
        u8g.drawFrame(8,38, 104,20);
        u8g.drawBox(10,40, x, 16);
      } while (u8g.nextPage());
    }
    break;
  case 'P':{
      if(setupData.pat < patOffset)
        patOffset = setupData.pat;
      if(setupData.pat >= patOffset + 3)
        patOffset = setupData.pat - 2;
      u8g.firstPage();
      do {
        int i;
        u8g.drawStr(0,15, "PATTERN");
        for(i=0;i<3;++i){
          char s[2]={0,0};
          s[0] = 0x31 + i + patOffset;
          u8g.drawStr(16,31 + i * 16, s);
          u8g.drawBox(32,18 + i * 16,96,1);
          Pattern* p = &setupData.pattern[i + patOffset];
          for(int x=0; x < p->step; ++x){
            int d = p->patternTab[x];
            for(int y=0; y<4; ++y,d>>=2){
              if(d&3){
                u8g.drawBox(32 + x*6, 21 + i*16 + y*3 , 4, 2);
              }
            }
          }
        }
        u8g.drawBox(32,18 + i * 16,96,1);
        int y = (setupData.pat - patOffset + 1) * 16;
        u8g.drawTriangle(0, y, 0, y + 15, 10, y + 7);
      }while(u8g.nextPage());
    }
    break;
  case 'E':{
      u8g.firstPage();
      do {
        int x,y,b,yoffs;
        Pattern* p = &setupData.pattern[setupData.pat];
        char s[16];
        sprintf(s, "PATTERN %d",setupData.pat+1);
        u8g.drawStr(0, 15, s);
        if(editCur>=64){
          for(y=0; y<2; ++y){
            u8g.drawBox(13, 19 + y * 11, p->step * 7, 1);
            sprintf(s, "%d", y+3);
            u8g.drawStr(0,30 + y * 11, s);
          }
          u8g.drawBox(13, 19 + y * 11, p->step * 7, 1);
          for(x=0;x < p->step; ++x){
            int d = (p->patternTab[x]>>4);
            u8g.drawBox(13 + x * 7, 19, 1, 22);
            for(y=0; y<2; ++y,d>>=2){
              if(d&3){
                int yy=1<<(d&3);
                u8g.drawBox(15 + x * 7, 21 + y*11+8-yy, 4,yy);
              }
            }
          }
          u8g.drawBox(13 + x * 7, 19, 1, 22);
          u8g.drawStr(16,64, "STEP");
          u8g.drawStr(80,64, "DUTY");
          if(editCur==64)
            u8g.drawTriangle(0, 48, 0, 64, 10, 56);
          else
            u8g.drawTriangle(64, 48, 64, 64, 74, 56);
        }
        else{
          for(y=0; y<4; ++y){
            u8g.drawBox(13, 19 + y * 11, p->step * 7, 1);
            sprintf(s, "%d", y+1);
            u8g.drawStr(0,30 + y * 11, s);
          }
          u8g.drawBox(13, 19 + y * 11, p->step * 7, 1);
          for(x=0;x < p->step; ++x){
            u8g.drawBox(13 + x * 7, 19, 1, 44);
            int d= p->patternTab[x];
            for(y=0; y<4; ++y,d>>=2){
              if(d&3){
                int yy=1<<(d&3);
                u8g.drawBox(15 + x * 7, 21 + y*11+8-yy, 4, yy);
              }
            }
          }
          u8g.drawBox(13 + x * 7, 19, 1, 44);
          x=editCur&15;
          y=editCur>>4;
          u8g.drawBox(14 + x*7, 20 + y*11, 6, 1);
          u8g.drawBox(14 + x*7, 29 + y*11, 6, 1);
          u8g.drawBox(14 + x*7, 20 + y*11, 1, 9);
          u8g.drawBox(19 + x*7, 20 + y*11, 1, 9);
        }
      }while(u8g.nextPage());
    }
    break;
  }
}
void NextMenu(){
  MenuData* p = &menuData[currentPage];
  int v;
  switch(p->type){
  case 's':
  case 'S': {
      v = *p->val + 1;
      int idx = GetSubIndex(currentPage, v);
      if(idx < 0)
        v = v - 1;
      if(v >= p->offset + 3)
        p->offset = v - 2;
      *p->val = v;
    }
    break;
  case 'V': {
      v = *p->val + 1;
      if(v > p->maxVal)
        v = p->maxVal;
      *p->val = v;
    }
    break;
  case 'P': {
      if(++setupData.pat >= MAX_PATTERN)
        setupData.pat = MAX_PATTERN - 1;
    }
    break;
  case 'E': {
      ++editCur;
      if(editCur>65)
        editCur=0;
      if(editCur<64 && (editCur & 0xf)>=setupData.pattern[setupData.pat].step)
        editCur = (editCur&0x30)+16;
    }
  }
}
void PrevMenu(){
  MenuData* p = &menuData[currentPage];
  int v;
  switch(p->type){
  case 's':
  case 'S':
    v = *p->val - 1;
    if(v < 0)
      v = 0;
    if(v < p->offset)
      p->offset = v;
    *p->val = v;
    break;
  case 'V':
    v = *p->val - 1;
    if(v < p->minVal)
      v = p->minVal;
    *p->val = v;
    break;
  case 'P':
    if(--setupData.pat < 0)
      setupData.pat = 0;
    break;
  case 'E':
    --editCur;
    if(editCur < 0)
      editCur = 65;
    if(editCur < 64 && (editCur & 0xf)>=setupData.pattern[setupData.pat].step)
      editCur = ((editCur&0x30)+setupData.pattern[setupData.pat].step-1)&63;
  }
}
void PressMenu(){
  MenuData* p = &menuData[currentPage];
  switch(p->type){
  case 's':
  case 'S': {
      int v = *(p->val);
      int idx = GetSubIndex(currentPage, v);
      if(menuData[idx].type =='C'){
        void* ptr = (void*)menuData[idx].val;
        if(ptr)
          ((void (*)())ptr)();
        currentPage = menuData[idx].p1;
      }
      else
        currentPage = idx;
    }
    break;
  case 'C': {
      currentPage = p->p1;
    }
    break;
  case 'V': {
      currentPage = p->p1;
    }
    break;
  case 'P': {
      currentPage = p->p1;
    }
    break;
  case 'E': {
      switch(editCur){
      case 64:
        menuData[M_EDITSTEP].val = &setupData.pattern[setupData.pat].step;
        currentPage = M_EDITSTEP;
        break;
      case 65:
        menuData[M_EDITDUTY].val = &setupData.pattern[setupData.pat].duty;
        currentPage = M_EDITDUTY;
        break;
      default:
        int x = editCur&0xf;
        int y = editCur>>4;
        int b = 0x3<<(y<<1);
        int d = setupData.pattern[setupData.pat].patternTab[x];
        int n = (d >> (y<<1))&3;
        setupData.pattern[setupData.pat].patternTab[editCur&0xf] = (d&~b) | (((n+1)&3)<<(y<<1));
      }
    }
  }
}
void LongPressMenu(){
  MenuData* p = &menuData[currentPage];
  switch(p->type){
  case 'P': {
      ++currentPage;
    }
    break;
  case 'E': {
      --currentPage;  
    }
    break;
  }
}
int loadEep(){
  unsigned long id;
  EEPROM.get(0, id);
  char s[32];
  sprintf(s, "%x %x %x %x\n",EEPROM.read(0), EEPROM.read(1), EEPROM.read(2), EEPROM.read(3));
//  Serial.write(s);
  if(id != PROG_ID)
    return 0;
  EEPROM.get(0, setupData);
  return 1;
}

void saveEep(){
//  Serial.write("saveEep\n");
  EEPROM.put(0, setupData);
}
void initAll(){
//  setupData = setupDefault;
  memcpy_P(&setupData, &setupFactory, sizeof(setupData)) ;
}
void trig() {
  if(flashStep < 0 || setupData.reTrig) {
    flashStep = 0;
    currentPat = setupData.pat;
  }
}
void writeLED(char c) {
  digitalWrite(OUT1, (c&1)?1:0);
  digitalWrite(OUT2, (c&2)?1:0);
  digitalWrite(OUT3, (c&4)?1:0);
  digitalWrite(OUT4, (c&8)?1:0);
}

void ISRPatClock(){
  char p = 0;
  if(flashStep>=0){
    char d=setupData.pattern[currentPat].patternTab[flashStep];
    char d2=d | (d>>1);
    p=(d2&1) | ((d2>>1)&2) | ((d2>>2)&4) | ((d2>>3)&8);
    ledVal=p;

    if(p&1){
      dutyCount1 = dutyCountMax >> (3 - (d&3));
      if(dutyCount1<=0)
        dutyCount1=1;
    }
    if(p&2){
      dutyCount2 = dutyCountMax >> (3 - ((d>>2)&3));
      if(dutyCount2<=0)
        dutyCount2=1;
    }
    if(p&4){
      dutyCount3 = dutyCountMax >> (3 - ((d>>4)&3));
      if(dutyCount3<=0)
        dutyCount3=1;
    }
    if(p&8){
      dutyCount4 = dutyCountMax >> (3 - ((d>>6)&3));
      if(dutyCount4<=0)
        dutyCount4=1;
    }
    
    dutyCount = 0;
    if(++flashStep >= setupData.pattern[currentPat].step){
      flashStep = -1;
      autoTrigCount = setupData.autoTrig;
    }
  }
  if(autoTrigCount > 0){
    if(--autoTrigCount == 0){
      trig();
    }
  }
  else if(setupData.autoTrig > 0)
    autoTrigCount = 1;
  flashCount = 0;
  writeLED(ledVal);
}
void ISRPat(){
/*  if (--autoTrigCount < 0) {
    if(setupData.autoTrig)
      autoTrigCount = 60000 / setupData.autoTrig / ISR_INTERVAL;
  }
  if (setupData.autoTrig > 0 && autoTrigCount == 0)
    trig();
*/

  if(setupData.clock == 1)
    flashCountMax = clockInterval;
  else
    flashCountMax = 15000 / setupData.flashBpm / ISR_INTERVAL;
  if(flashCountMax <=0)
    flashCountMax = 1;
    
  dutyCountMax = flashCountMax * setupData.pattern[currentPat].duty / 100;
  if(dutyCountMax <= 0)
    dutyCountMax = 1;
    
  if(dutyCount1 > 0){
    if(--dutyCount1 == 0){
      ledVal &= ~1;
      writeLED(ledVal);
    }
  }
  if(dutyCount2 > 0){
    if(--dutyCount2 == 0){
      ledVal &= ~2;
      writeLED(ledVal);
    }
  }
  if(dutyCount3 > 0){
    if(--dutyCount3 == 0){
      ledVal &= ~4;
      writeLED(ledVal);
    }
  }
  if(dutyCount4 > 0){
    if(--dutyCount4 == 0){
      ledVal &= ~8;
      writeLED(ledVal);
    }
  }
  if(setupData.clock == 0){
    if(++flashCount >= flashCountMax)
      ISRPatClock();
  }
}
void ISRDir(){
  switch(setupData.assign){
  case 0:
    digitalWrite(OUT1, digitalRead(IN1)^1);
    digitalWrite(OUT2, digitalRead(IN2)^1);
    digitalWrite(OUT3, digitalRead(IN3)^1);
    digitalWrite(OUT4, digitalRead(IN4)^1);
    break;
  case 1:
    digitalWrite(OUT1, digitalRead(IN1)^1);
    digitalWrite(OUT2, digitalRead(IN1)^1);
    digitalWrite(OUT3, digitalRead(IN2)^1);
    digitalWrite(OUT4, digitalRead(IN2)^1);
    break;
  case 2:
    digitalWrite(OUT1, digitalRead(IN1)^1);
    digitalWrite(OUT2, digitalRead(IN1)^1);
    digitalWrite(OUT3, digitalRead(IN1)^1);
    digitalWrite(OUT4, digitalRead(IN1)^1);
    break;
  }
}
void ISRLev(){
  static int lv=0;
  int c = 0;
  int v = audioVol;
  v*=5;
  int s = 50 / setupData.speed;
  lv = (lv * s + v) / (s + 1);
  if(lv > 160) c = 0xf;
  else if(lv > 80) c = 0x7;
  else if(lv > 40) c = 0x3;
  else if(lv > 20) c = 0x1;
  writeLED(c);
}
void isrTimer() {
  ++isrCount;
  switch(setupData.mode){
  case 0:
    ISRPat();
    break;
  case 1:
    ISRDir();
    break;
  case 2:
    ISRLev();
    break;
  }
}

void checkBtn() {
  if (digitalRead(ENCBTN) == LOW) {
    ++btnCount;
    if(btnCount == 1) {
      digitalWrite(LEDA, 1);
      digitalWrite(LEDB, 0);
    }
    if(btnCount == 25){
      digitalWrite(LEDB,1);
      LongPressMenu();
    }
  }
  else {
    digitalWrite(LEDA, 0);
    digitalWrite(LEDB, 1);
    if(btnCount>0 && btnCount<25){
      PressMenu();
    }
    btnCount = 0;
  }
}

void ISREnc(int s) {
  static int phase = 0;
//  char str[8];
//  sprintf(str,"%x %d\n",s,phase);
//  Serial.write(str);
  if((s&0x10)==0){
    
  }
  switch(s&0xc){
  case 0x0:{
      switch(phase){
      case 1:
        phase = 2; break;
      case -1:
        phase = -2; break;
      }
    }
    break;
  case 0x8:{
      if(phase==0) phase=1;
    }
    break;
  case 0x4:{
      if(phase==0) phase=-1;
    }
    break;
  case 0xc:{
      switch(phase){
      case -2:
        PrevMenu();
        break;
      case 2:
        NextMenu();
        break;
      }
      phase=0;
    }
    break;
  }
}
void lineIn(){
  int p = analogRead(PARAMIN) / 2;
  audioVol = abs((int)analogRead(LINEIN) - 512) * p / 512;
  if (audioVol > 10)
    trig();
}
void micIn() {
  int p = analogRead(PARAMIN) / 2;
  int v = analogRead(MICIN);
  audioVol = abs(v - 512) * p / 256;
  if (audioVol > 10)
    trig();
}
ISR(PCINT0_vect){
  static int in_old=1;
  int in = PINB&1;
  if(in==1 && in_old==0) {
//    char str[8];
//    sprintf(str,"%d\n",isrCount);
//    Serial.write(str);
    clockInterval = isrCount;
    isrCount = 0;
    if(setupData.mode==0 && setupData.clock==1)
      ISRPatClock();
  }
  in_old=in;
}
ISR(PCINT2_vect){
  ISREnc(PIND & 0x1c);
}
void PCINTSetup(){
  PCMSK2 |= (1<<PCINT18)|(1<<PCINT19)|(1<<PCINT20);   // D4
  PCICR  |= (1 << PCIE2)|(1<<PCIE0);                  // PCINT16-23 (D0-D7), PCINT0-5 (D8-D13)
  PCMSK0 |= (1<<PCINT0);                              // D8
}

void setup() {
  pinMode(ENCA, INPUT_PULLUP);
  pinMode(ENCB, INPUT_PULLUP);
  pinMode(ENCBTN, INPUT_PULLUP);

  pinMode(IN1, INPUT_PULLUP);
  pinMode(IN2, INPUT_PULLUP);
  pinMode(IN3, INPUT_PULLUP);
  pinMode(IN4, INPUT_PULLUP);

  pinMode(LEDA, OUTPUT);
  pinMode(LEDB, OUTPUT);
  
  pinMode(OUT1, OUTPUT);
  pinMode(OUT2, OUTPUT);
  pinMode(OUT3, OUTPUT);
  pinMode(OUT4, OUTPUT);

//  attachInterrupt(0, isrEnc, CHANGE);
//  attachInterrupt(1, isrEnc, CHANGE);

  PCINTSetup();
  Wire.begin();
  
//  Serial.begin (9600);

  u8g.setFont(u8g_font_unifont);
  u8g.setColorIndex(1);

//  setupData = setupDefault;
  memcpy_P(&setupData, &setupFactory, sizeof(setupData)) ;
  loadEep();
  MsTimer2::set(ISR_INTERVAL, isrTimer);
  MsTimer2::start();

//  Serial.write("Setup complete\n");

}
void loop() {
  checkBtn();
  switch(setupData.audioIn){
  case 0:
    micIn();
    break;
  case 1:
    lineIn();
    break;
  }
  DispMenu();
}
