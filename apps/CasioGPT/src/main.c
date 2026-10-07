#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <fxcg/serial.h>
#include <fxcg/rtc.h>
#include <fxcg/system.h>
#include <fxcg/file.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "ui_aa.h"
#include "splash.h"
#include "avatar.h"
#include "wire.h"

#define SCREEN_W 384
#define SCREEN_H 216
#define HEADER_H 28
#define INPUT_TOP 173
#define CHAT_TOP 30
#define CHAT_BOTTOM 169
#define LINE_H 14
#define MAX_MESSAGES 10
#define MESSAGE_CAP 2048
#define INPUT_CAP 512
#define API_KEY_CAP 160

#define COL_BG          ((color_t)0x0841)
#define COL_HEADER      ((color_t)0x0000)
#define COL_INPUT       ((color_t)0x18E4)
#define COL_USER        ((color_t)0x2945)
#define COL_TEXT        ((color_t)0xFFFF)
#define COL_MUTED       ((color_t)0x9CF3)
#define COL_BORDER      ((color_t)0x3186)
#define COL_GREEN       ((color_t)0x2E6D)
#define COL_RED         ((color_t)0xE228)
#define COL_WHITE       ((color_t)0xFFFF)
#define COL_BLACK       ((color_t)0x0000)
#define COL_BLUE        ((color_t)0x167F)
#define COL_SOFT_BLUE   ((color_t)0x8D7F)

typedef struct {
    int user;
    char text[MESSAGE_CAP];
} chat_message_t;

static chat_message_t messages[MAX_MESSAGES];
static int message_count, assistant_index=-1;
static char input[INPUT_CAP+1], active_prompt[INPUT_CAP+1], api_key[API_KEY_CAP+1];
static int input_len, input_cursor, api_key_len;
static int alpha_once, alpha_lock, shift_pending, upper_case;
static int scroll_px, display_dirty=1;
static int esp_ok, wifi_ok, internet_ok;
static char app_error[96];

static color_t *vram(void) { return (color_t *)GetVRAMAddress(); }
static void putpx(int x,int y,color_t c)
{
    if(x>=0 && x<SCREEN_W && y>=0 && y<SCREEN_H) vram()[y*SCREEN_W+x]=c;
}
static void fill_rect(int x1,int y1,int x2,int y2,color_t c)
{
    if(x1>x2){int t=x1;x1=x2;x2=t;} if(y1>y2){int t=y1;y1=y2;y2=t;}
    if(x1<0)x1=0;
    if(y1<0)y1=0;
    if(x2>=SCREEN_W)x2=SCREEN_W-1;
    if(y2>=SCREEN_H)y2=SCREEN_H-1;
    for(int y=y1;y<=y2;y++){color_t *p=vram()+y*SCREEN_W+x1;for(int x=x1;x<=x2;x++)*p++=c;}
}
static void fill_circle(int cx,int cy,int r,color_t c)
{
    int rr=r*r; for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++)if(x*x+y*y<=rr)putpx(cx+x,cy+y,c);
}
static void draw_line(int x0,int y0,int x1,int y1,color_t c)
{
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){putpx(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=err*2;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}
static void rounded_rect(int x1,int y1,int x2,int y2,int r,color_t c)
{
    int rr=r*r; fill_rect(x1+r,y1,x2-r,y2,c); fill_rect(x1,y1+r,x2,y2-r,c);
    for(int y=0;y<=r;y++)for(int x=0;x<=r;x++){
        int ox=r-x,oy=r-y; if(ox*ox+oy*oy<=rr){putpx(x1+x,y1+y,c);putpx(x2-x,y1+y,c);putpx(x1+x,y2-y,c);putpx(x2-x,y2-y,c);}
    }
}
static color_t blend(color_t fg,color_t bg,int a)
{
    int r=(((fg>>11)&31)*a+((bg>>11)&31)*(255-a))/255;
    int g=(((fg>>5)&63)*a+((bg>>5)&63)*(255-a))/255;
    int b=((fg&31)*a+(bg&31)*(255-a))/255;
    return (color_t)((r<<11)|(g<<5)|b);
}
static void mini_text(int x,int y,const char *s,color_t fg)
{
    while(*s){
        unsigned char c=(unsigned char)*s++; if(c<32||c>126)c='?';
        const unsigned char *g=ui_aa[c-32];
        for(int yy=0;yy<UI_AA_H;yy++)for(int xx=0;xx<UI_AA_W;xx++){
            int a=g[1+yy*UI_AA_W+xx],px=x+xx,py=y+yy;
            if(a&&px>=0&&px<SCREEN_W&&py>=0&&py<SCREEN_H)putpx(px,py,blend(fg,vram()[py*SCREEN_W+px],a));
        }
        x+=g[0];
    }
}
static void mini_bold(int x,int y,const char *s,color_t fg)
{
    mini_text(x,y,s,fg); mini_text(x+1,y,s,fg);
}
static int text_width_n(const char *s,int n)
{
    int w=0; while(n--&&*s){unsigned char c=(unsigned char)*s++;if(c<32||c>126)c='?';w+=ui_aa[c-32][0];}return w;
}
static int text_width(const char *s){return text_width_n(s,(int)strlen(s));}
static void str_copy(char *dst,int cap,const char *src)
{
    int n=0;if(cap<=0)return;while(src&&src[n]&&n<cap-1){dst[n]=src[n];n++;}dst[n]=0;
}
static void wifi_icon(int cx,int cy,int radius,color_t active,color_t bg)
{
    int thickness=2;
    for(int y=-radius-1;y<=3;y++)for(int x=-radius-1;x<=radius+1;x++){
        int coverage=0,draw=0;
        for(int sy=0;sy<2;sy++)for(int sx=0;sx<2;sx++){
            int xx=x*4+sx*2+1,yy=y*4+sy*2+1,d=xx*xx+yy*yy;
            if(d<=thickness*thickness*9){coverage++;draw=1;continue;}
            if(yy>=0||abs(xx)*3>-yy*4)continue;
            for(int band=1;band<=3;band++){int r=radius*band*4/3,inner=r-thickness*4;if(d<=r*r&&d>=inner*inner){coverage++;draw=1;break;}}
        }
        if(draw)putpx(cx+x,cy+y,blend(active,bg,coverage*255/4));
    }
}
static void slash_icon(int x,int y,int size,color_t c){for(int i=0;i<size;i++){putpx(x+i,y+i,c);putpx(x+i+1,y+i,c);}}
static void draw_avatar(int x,int y,color_t color)
{
    for(int yy=0;yy<CASIOGPT_AVATAR_H;yy++)for(int xx=0;xx<CASIOGPT_AVATAR_W;xx++){
        int a=casiogpt_avatar_alpha[yy*CASIOGPT_AVATAR_W+xx];
        int px=x+xx,py=y+yy;if(a&&px>=0&&px<SCREEN_W&&py>=0&&py<SCREEN_H)putpx(px,py,blend(color,vram()[py*SCREEN_W+px],a));
    }
}
static void draw_header(void)
{
    fill_rect(0,0,383,HEADER_H-1,COL_HEADER);
    mini_bold(12,9,"CasioGPT",COL_WHITE);
    mini_text(82,9,upper_case?"ABC":"abc",upper_case?COL_SOFT_BLUE:COL_MUTED);
    wifi_icon(342,21,14,wifi_ok?COL_GREEN:COL_RED,COL_HEADER);
    if(!wifi_ok)slash_icon(333,6,18,COL_RED);
    fill_circle(374,13,4,esp_ok?COL_GREEN:COL_RED);
}
static void draw_send_icon(int busy)
{
    fill_circle(352,194,14,COL_WHITE);
    if(busy) rounded_rect(348,190,356,198,1,COL_BLACK);
    else {
        draw_line(352,185,345,192,COL_BLACK);
        draw_line(352,185,359,192,COL_BLACK);
        draw_line(352,185,352,202,COL_BLACK);
    }
}
static void draw_splash(void)
{
    memcpy(vram(),casiogpt_splash_pixels,sizeof(casiogpt_splash_pixels));
    Bdisp_PutDisp_DD(); CMT_Delay_100micros(15000);
}

/* Returns the next wrapped segment and its following source offset. */
static int wrap_next(const char *text,int at,int max_width,char *line,int cap,int *next)
{
    int start=at,n=0,last_space=-1,last_space_source=-1;
    if(!text[at]){line[0]=0;*next=at;return 0;}
    while(text[at]&&text[at]!='\n'&&n<cap-1){
        char c=text[at]; line[n]=c; line[n+1]=0;
        if(text_width(line)>max_width)break;
        if(c==' '){last_space=n;last_space_source=at;}
        n++;at++;
    }
    if(text[at]=='\n'){line[n]=0;*next=at+1;return n;}
    if(text[at]&&n<cap-1&&text_width_n(text+start,n+1)>max_width){
        if(last_space>0){n=last_space;line[n]=0;at=last_space_source+1;while(text[at]==' ')at++;}
        else if(n==0){line[n++]=text[at++];line[n]=0;}
    } else line[n]=0;
    *next=at; return n;
}
static int wrapped_metrics(const char *text,int width,int *max_line)
{
    int at=0,lines=0,maxw=0;char line[256];
    if(!text[0]){if(max_line)*max_line=0;return 1;}
    while(text[at]&&lines<160){int next;wrap_next(text,at,width,line,sizeof(line),&next);int w=text_width(line);if(w>maxw)maxw=w;lines++;if(next==at)break;at=next;}
    if(max_line)*max_line=maxw;
    return lines?lines:1;
}
static int message_height(const chat_message_t *m,int thinking)
{
    int lines=thinking?1:wrapped_metrics(m->text,m->user?246:314,NULL);
    return lines*LINE_H+(m->user?12:8);
}
static int conversation_height(void)
{
    int h=6;for(int i=0;i<message_count;i++)h+=message_height(&messages[i],i==assistant_index&&messages[i].text[0]==0)+8;return h;
}
static void draw_thinking(int x,int y)
{
    int phase=(RTC_GetTicks()/64)%3;
    for(int i=0;i<3;i++){int lift=i==phase?3:0;fill_circle(x+i*10,y-lift,2,COL_MUTED);}
}
static void draw_conversation(void)
{
    int total=conversation_height(),viewport=CHAT_BOTTOM-CHAT_TOP+1;
    int max_scroll=total>viewport?total-viewport:0;
    if(scroll_px<0)scroll_px=0;
    if(scroll_px>max_scroll)scroll_px=max_scroll;
    int y=total<=viewport?CHAT_TOP+5:CHAT_BOTTOM-total+scroll_px;
    for(int i=0;i<message_count;i++){
        chat_message_t *m=&messages[i];int thinking=i==assistant_index&&!m->text[0];
        int maxline=0,lines=thinking?1:wrapped_metrics(m->text,m->user?246:314,&maxline);
        int h=message_height(m,thinking);
        if(m->user){
            int bw=maxline+14;if(bw<46)bw=46;if(bw>260)bw=260;int x1=376-bw;
            if(y+h>=CHAT_TOP&&y<=CHAT_BOTTOM)rounded_rect(x1,y,376,y+h-2,10,COL_USER);
            int at=0,ly=y+7;char line[256];
            for(int l=0;l<lines&&m->text[at];l++){int next;wrap_next(m->text,at,246,line,sizeof(line),&next);if(ly>=CHAT_TOP-12&&ly<=CHAT_BOTTOM)mini_text(x1+7,ly,line,COL_TEXT);ly+=LINE_H;at=next;}
        } else {
            if(y+24>=CHAT_TOP&&y<=CHAT_BOTTOM)draw_avatar(13,y+2,COL_WHITE);
            if(thinking){if(y+16>=CHAT_TOP&&y<=CHAT_BOTTOM)draw_thinking(48,y+13);}
            else {
                int at=0,ly=y+5;char line[256];
                for(int l=0;l<lines&&m->text[at];l++){int next;wrap_next(m->text,at,314,line,sizeof(line),&next);if(ly>=CHAT_TOP-12&&ly<=CHAT_BOTTOM)mini_text(44,ly,line,COL_TEXT);ly+=LINE_H;at=next;}
            }
        }
        y+=h+8;
    }
}
static void draw_input(void)
{
    rounded_rect(10,INPUT_TOP,374,214,17,COL_INPUT);
    int start=0;char before[INPUT_CAP+1],visible[INPUT_CAP+1];
    while(start<input_cursor){memcpy(before,input+start,input_cursor-start);before[input_cursor-start]=0;if(text_width(before)<292)break;start++;}
    str_copy(visible,sizeof(visible),input+start);
    while(visible[0]&&text_width(visible)>300)visible[strlen(visible)-1]=0;
    if(!input_len)mini_text(22,188,"Escribe un mensaje",COL_MUTED);
    else mini_text(22,188,visible,COL_TEXT);
    memcpy(before,input+start,input_cursor-start);before[input_cursor-start]=0;
    if(input_len){int cx=22+text_width(before);if(cx>328)cx=328;fill_rect(cx,186,cx,201,COL_WHITE);}
    draw_send_icon(assistant_index>=0);
}
static void render_chat(void)
{
    Bdisp_Fill_VRAM(COL_BG,1);draw_header();draw_conversation();draw_input();Bdisp_PutDisp_DD();
}
static void status_row(int y,const char *label,int state,int phase)
{
    /* state: 0 pending, 1 checking, 2 success, -1 error */
    if(state==2){fill_circle(39,y+7,9,COL_GREEN);mini_bold(35,y+2,"v",COL_WHITE);}
    else if(state<0){fill_circle(39,y+7,9,COL_RED);mini_bold(36,y+2,"x",COL_WHITE);}
    else {
        fill_circle(39,y+7,9,COL_BORDER);
        for(int i=0;i<3;i++)fill_circle(34+i*5,y+7-(i==phase?3:0),1,COL_WHITE);
    }
    mini_text(58,y+2,label,state==2?COL_TEXT:state<0?COL_RED:COL_MUTED);
}
static int verify_key_state,verify_esp_state,verify_net_state;
static void render_verify(void)
{
    int phase=(RTC_GetTicks()/64)%3;
    Bdisp_Fill_VRAM(COL_BG,1);draw_header();
    mini_bold(24,44,"Preparando CasioGPT",COL_TEXT);
    mini_text(24,62,"Comprobaciones locales y de red",COL_MUTED);
    status_row(87,"Validando casiogpt_api.txt",verify_key_state,phase);
    status_row(119,verify_esp_state==2?"ESP32 verificada":"Verificando conexion con ESP32",verify_esp_state,phase);
    status_row(151,verify_net_state==2?"Internet verificado":"Verificando conexion a internet",verify_net_state,phase);
    if(app_error[0])mini_text(24,184,app_error,COL_RED);
    if(verify_key_state<0)mini_text(24,200,"Crea el archivo en la raiz y pulsa F1",COL_MUTED);
    else if(verify_esp_state<0||verify_net_state<0)mini_text(24,200,"Pulsa F1 para volver a comprobar",COL_MUTED);
    Bdisp_PutDisp_DD();
}

/* ---------- local API key ---------- */
static int load_api_key(void)
{
    static const unsigned short path[]={ '\\','\\','f','l','s','0','\\','c','a','s','i','o','g','p','t','_','a','p','i','.','t','x','t',0 };
    int fd=Bfile_OpenFile_OS(path,READ,0);if(fd<0)return 0;
    int size=Bfile_GetFileSize_OS(fd);if(size<=0||size>API_KEY_CAP+8){Bfile_CloseFile_OS(fd);return 0;}
    char raw[API_KEY_CAP+9];int got=Bfile_ReadFile_OS(fd,raw,size,0);Bfile_CloseFile_OS(fd);
    if(got!=size)return 0;
    raw[size]=0;
    int begin=0,end=size;while(begin<end&&(raw[begin]==' '||raw[begin]=='\r'||raw[begin]=='\n'||raw[begin]=='\t'))begin++;
    while(end>begin&&(raw[end-1]==' '||raw[end-1]=='\r'||raw[end-1]=='\n'||raw[end-1]=='\t'))end--;
    int n=end-begin;if(n<24||n>API_KEY_CAP)return 0;
    for(int i=0;i<n;i++){unsigned char c=raw[begin+i];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='_'||c=='-'))return 0;}
    memcpy(api_key,raw+begin,n);api_key[n]=0;api_key_len=n;memset(raw,0,sizeof(raw));return 1;
}

/* ---------- non-blocking UART RPC ---------- */
typedef struct {
    int active,queued,waiting,result,attempts,tx_len,tx_offset,tx_at,timeout,bad_frames;
    unsigned long id,start,request_at;
    char frame[WIRE_CAP],response[WIRE_CAP],expect[20];
    wire_rx rx;
} rpc_state_t;
static rpc_state_t rpc;
static int open_uart(void)
{
    unsigned char mode[6]={0,5,0,0,0,0};
    if(Serial_IsOpen())Serial_Close(1);
    if(Serial_Open(mode)!=0&&!Serial_IsOpen())return 0;
    Serial_ClearRX();Serial_ClearTX();return 1;
}
static int response_id(const char *line,unsigned long *id)
{
    const char *p=strchr(line,':');if(!p)return 0;p++;uint32_t n=0;if(!*p)return 0;
    while(*p&&*p!=':'){unsigned d=(unsigned char)*p++-'0';if(d>9)return 0;n=n*10+d;}*id=n;return 1;
}
static int expected_response(const char *line,const char *expect)
{
    if(!strcmp(expect,"GPT_GET"))return !strncmp(line,"GPT_CHUNK:",10)||!strncmp(line,"GPT_WAIT:",9)||!strncmp(line,"GPT_DONE:",9)||!strncmp(line,"GPT_ERROR:",10)||!strncmp(line,"GPT_CANCELLED:",14);
    if(!strcmp(expect,"NET"))return !strncmp(line,"NET_WAIT:",9)||!strncmp(line,"NET_DONE:",9)||!strncmp(line,"NET_ERROR:",10);
    return !strncmp(line,expect,strlen(expect))&&line[strlen(expect)]==':';
}
static void rpc_abort(void){memset(&rpc,0,sizeof(rpc));}
static int rpc_start(const char *payload,unsigned long id,const char *expect,int timeout)
{
    rpc_abort();if(!Serial_IsOpen()&&!open_uart())return 0;
    rpc.tx_len=wire_pack(rpc.frame,payload);if(!rpc.tx_len)return 0;
    rpc.id=id;rpc.active=rpc.queued=1;rpc.timeout=timeout;rpc.start=rpc.request_at=RTC_GetTicks();rpc.tx_at=RTC_GetTicks()-20;
    str_copy(rpc.expect,sizeof(rpc.expect),expect);return 1;
}
static void rpc_fail(void){rpc.active=rpc.queued=rpc.waiting=0;rpc.result=-1;esp_ok=0;display_dirty=1;}
static void rpc_poll(void)
{
    if(!rpc.active)return;
    for(int budget=0;budget<256&&Serial_PollRX()>0;budget++){
        unsigned char c=0;if(Serial_ReadSingle(&c)!=0)break;int s=wire_feed(&rpc.rx,c);
        if(s>0){unsigned long id;if(response_id(rpc.rx.data,&id)&&id==rpc.id&&expected_response(rpc.rx.data,rpc.expect)){
            str_copy(rpc.response,sizeof(rpc.response),rpc.rx.data);rpc.active=rpc.waiting=rpc.queued=0;rpc.result=1;esp_ok=1;display_dirty=1;return;}
        } else if(s<0) rpc.bad_frames++;
    }
    if(RTC_Elapsed_ms(rpc.start,rpc.timeout)){rpc_fail();return;}
    if(rpc.waiting&&RTC_Elapsed_ms(rpc.request_at,1400)){
        if(rpc.attempts>=8){rpc_fail();return;}
        if(rpc.attempts==4&&!open_uart()){rpc_fail();return;}
        rpc.waiting=0;rpc.queued=1;rpc.tx_offset=0;rpc.tx_at=RTC_GetTicks();
    }
    if(rpc.queued&&RTC_Elapsed_ms(rpc.tx_at,12)){
        int left=rpc.tx_len-rpc.tx_offset,chunk=left>8?8:left;
        if(Serial_PollTX()>=chunk&&Serial_Write((const unsigned char *)rpc.frame+rpc.tx_offset,chunk)==0){
            rpc.tx_offset+=chunk;rpc.tx_at=RTC_GetTicks();
            if(rpc.tx_offset==rpc.tx_len){rpc.tx_offset=0;rpc.queued=0;rpc.waiting=1;rpc.attempts++;rpc.request_at=RTC_GetTicks();}
        }
    }
}
static unsigned long previous_id;
static unsigned long new_id(void){unsigned long id=RTC_GetTicks();if(id<=previous_id)id=previous_id+1;previous_id=id;return id;}

/* ---------- verification state ---------- */
enum { VERIFY_IDLE,VERIFY_ESP,VERIFY_NET_BEGIN,VERIFY_NET_POLL,VERIFY_NEW,VERIFY_READY,VERIFY_FAILED };
static int verify_stage=VERIFY_IDLE;
static unsigned long verify_id,verify_poll_at;
static void begin_verify(void)
{
    rpc_abort();app_error[0]=0;internet_ok=wifi_ok=esp_ok=0;
    api_key_len=0;memset(api_key,0,sizeof(api_key));
    verify_key_state=load_api_key()?2:-1;verify_esp_state=verify_net_state=0;
    if(verify_key_state<0){str_copy(app_error,sizeof(app_error),"Clave ausente o formato no valido.");verify_stage=VERIFY_FAILED;display_dirty=1;return;}
    if(!open_uart()){verify_esp_state=-1;str_copy(app_error,sizeof(app_error),"No se pudo abrir UART.");verify_stage=VERIFY_FAILED;display_dirty=1;return;}
    verify_esp_state=1;verify_stage=VERIFY_ESP;verify_id=new_id();char cmd[48];sprintf(cmd,"STATE:%lu",verify_id);
    if(!rpc_start(cmd,verify_id,"LINK",10000)){verify_esp_state=-1;verify_stage=VERIFY_FAILED;}
    display_dirty=1;
}
static int split_response(char *text,char **f,int cap){return wire_split(text,f,cap);}
static void process_verify(void)
{
    if(verify_stage==VERIFY_NET_POLL&&!rpc.active&&!rpc.result&&RTC_Elapsed_ms(verify_poll_at,180)){
        char cmd[48];sprintf(cmd,"NET_GET:%lu",verify_id);rpc_start(cmd,verify_id,"NET",10000);
    }
    if(!rpc.result)return;
    if(rpc.result<0){
        if(verify_stage==VERIFY_ESP)verify_esp_state=-1;else verify_net_state=-1;
        str_copy(app_error,sizeof(app_error),verify_stage==VERIFY_ESP?"La ESP32 no responde.":"No se pudo verificar Internet.");
        verify_stage=VERIFY_FAILED;rpc.result=0;display_dirty=1;return;
    }
    char text[WIRE_CAP],*f[7];str_copy(text,sizeof(text),rpc.response);int n=split_response(text,f,7);rpc.result=0;
    if(verify_stage==VERIFY_ESP){
        uint32_t phase;
        if(n==5&&!strcmp(f[0],"LINK")&&wire_number(f[2],&phase)){
            esp_ok=verify_esp_state=2;wifi_ok=phase==2;
            if(!wifi_ok){verify_net_state=-1;str_copy(app_error,sizeof(app_error),"Sin Wi-Fi. Conecta primero desde CasioWIFI.");verify_stage=VERIFY_FAILED;}
            else {verify_net_state=1;verify_stage=VERIFY_NET_BEGIN;verify_id=new_id();char cmd[48];sprintf(cmd,"NET_BEGIN:%lu",verify_id);rpc_start(cmd,verify_id,"NET",10000);}
        } else {verify_esp_state=-1;verify_stage=VERIFY_FAILED;str_copy(app_error,sizeof(app_error),"Respuesta ESP32 no valida.");}
    } else if(verify_stage==VERIFY_NET_BEGIN||verify_stage==VERIFY_NET_POLL){
        if(n>=2&&!strcmp(f[0],"NET_DONE")){
            internet_ok=1;verify_net_state=2;verify_stage=VERIFY_NEW;verify_id=new_id();char cmd[48];sprintf(cmd,"GPT_NEW:%lu",verify_id);rpc_start(cmd,verify_id,"GPT_ACK",8000);
        } else if(n>=2&&!strcmp(f[0],"NET_WAIT")) {verify_stage=VERIFY_NET_POLL;verify_poll_at=RTC_GetTicks();}
        else {verify_net_state=-1;verify_stage=VERIFY_FAILED;str_copy(app_error,sizeof(app_error),n>=3&&!strcmp(f[2],"NO_WIFI")?"La ESP32 no esta conectada a Wi-Fi.":"Internet no disponible.");}
    } else if(verify_stage==VERIFY_NEW){
        if(n>=2&&!strcmp(f[0],"GPT_ACK")){verify_stage=VERIFY_READY;display_dirty=1;}
        else {verify_stage=VERIFY_FAILED;str_copy(app_error,sizeof(app_error),"No se pudo iniciar la sesion.");}
    }
}

/* ---------- chat upload/stream state ---------- */
enum { FLOW_IDLE,FLOW_BEGIN,FLOW_KEY,FLOW_PROMPT,FLOW_RUN,FLOW_POLL,FLOW_CANCEL };
static int flow=FLOW_IDLE,key_offset,prompt_offset,response_offset;
static unsigned long generation_id,poll_at,status_at;
static void ensure_capacity(int count)
{
    while(message_count+count>MAX_MESSAGES){memmove(messages,messages+1,sizeof(messages[0])*(MAX_MESSAGES-1));message_count--;if(assistant_index>=0)assistant_index--;}
}
static int add_message(int user,const char *text)
{
    ensure_capacity(1);messages[message_count].user=user;str_copy(messages[message_count].text,MESSAGE_CAP,text);return message_count++;
}
static void append_assistant(const char *piece)
{
    if(assistant_index<0||assistant_index>=message_count)return;
    char *dst=messages[assistant_index].text;
    int used=strlen(dst),left=MESSAGE_CAP-1-used;
    if(left>0)strncat(dst,piece,left);
    display_dirty=1;
}
static void finish_generation(const char *error)
{
    if(error&&*error&&assistant_index>=0&&!messages[assistant_index].text[0])append_assistant(error);
    assistant_index=-1;flow=FLOW_IDLE;response_offset=0;display_dirty=1;status_at=RTC_GetTicks()-5000;
}
static void start_chunk(const char *kind,const char *data,int total,int *offset)
{
    char raw[65],hex[129],cmd[WIRE_CAP];int n=total-*offset;if(n>64)n=64;
    memcpy(raw,data+*offset,n);raw[n]=0;wire_encode(hex,raw);
    sprintf(cmd,"%s:%lu:%d:%s",kind,generation_id,*offset,hex);
    rpc_start(cmd,generation_id,"GPT_ACK",12000);
}
static void begin_prompt(void)
{
    while(input_len>0&&input[input_len-1]==' ')input[--input_len]=0;
    if(input_cursor>input_len)input_cursor=input_len;
    if(!input_len||assistant_index>=0)return;
    if(rpc.active)rpc_abort();
    str_copy(active_prompt,sizeof(active_prompt),input);ensure_capacity(2);add_message(1,active_prompt);assistant_index=add_message(0,"");
    input[0]=0;input_len=input_cursor=0;scroll_px=0;
    generation_id=new_id();key_offset=prompt_offset=response_offset=0;flow=FLOW_BEGIN;
    char cmd[80];sprintf(cmd,"GPT_BEGIN:%lu:%d:%d",generation_id,api_key_len,(int)strlen(active_prompt));
    if(!rpc_start(cmd,generation_id,"GPT_ACK",12000))finish_generation("No se pudo abrir UART.");
    display_dirty=1;
}
static void request_next_output(void)
{
    char cmd[64];sprintf(cmd,"GPT_GET:%lu:%d",generation_id,response_offset);rpc_start(cmd,generation_id,"GPT_GET",10000);
}
static void process_chat_rpc(void)
{
    if(flow==FLOW_POLL&&!rpc.active&&!rpc.result&&RTC_Elapsed_ms(poll_at,90))request_next_output();
    if(flow==FLOW_IDLE&&!rpc.active&&!rpc.result&&RTC_Elapsed_ms(status_at,5000)){
        unsigned long id=new_id();char cmd[48];sprintf(cmd,"STATE:%lu",id);rpc_start(cmd,id,"LINK",5000);status_at=RTC_GetTicks();
    }
    if(!rpc.result)return;
    if(rpc.result<0){
        rpc.result=0;
        if(flow!=FLOW_IDLE) {
            const char *stage=flow==FLOW_BEGIN?"inicio":flow==FLOW_KEY?"clave":flow==FLOW_PROMPT?"mensaje":flow==FLOW_RUN?"API":flow==FLOW_POLL?"respuesta":"cancelacion";
            char error[80];sprintf(error,"Enlace ESP32 interrumpido (%s).",stage);finish_generation(error);
        }
        return;
    }
    char text[WIRE_CAP],*f[7];str_copy(text,sizeof(text),rpc.response);int n=split_response(text,f,7);rpc.result=0;
    if(flow==FLOW_IDLE){
        uint32_t phase;if(n==5&&!strcmp(f[0],"LINK")&&wire_number(f[2],&phase)){esp_ok=1;wifi_ok=phase==2;}display_dirty=1;return;
    }
    if(flow==FLOW_BEGIN){
        if(n>=4&&!strcmp(f[0],"GPT_ACK")&&!strcmp(f[2],"B")){flow=FLOW_KEY;start_chunk("GPT_KEY",api_key,api_key_len,&key_offset);}
        else finish_generation("No se pudo iniciar la consulta.");
        return;
    }
    if(flow==FLOW_KEY){
        uint32_t confirmed;
        if(n>=4&&!strcmp(f[0],"GPT_ACK")&&!strcmp(f[2],"K")&&wire_number(f[3],&confirmed)&&confirmed>=(unsigned)key_offset&&confirmed<=(unsigned)api_key_len){
            key_offset=(int)confirmed;
            if(key_offset<api_key_len)start_chunk("GPT_KEY",api_key,api_key_len,&key_offset);
            else {flow=FLOW_PROMPT;start_chunk("GPT_PROMPT",active_prompt,strlen(active_prompt),&prompt_offset);}
        } else finish_generation("Fallo al transferir la clave.");return;
    }
    if(flow==FLOW_PROMPT){
        uint32_t confirmed;int total=strlen(active_prompt);
        if(n>=4&&!strcmp(f[0],"GPT_ACK")&&!strcmp(f[2],"P")&&wire_number(f[3],&confirmed)&&confirmed>=(unsigned)prompt_offset&&confirmed<=(unsigned)total){
            prompt_offset=(int)confirmed;
            if(prompt_offset<total)start_chunk("GPT_PROMPT",active_prompt,total,&prompt_offset);
            else {flow=FLOW_RUN;char cmd[48];sprintf(cmd,"GPT_RUN:%lu",generation_id);rpc_start(cmd,generation_id,"GPT_ACK",12000);}
        } else finish_generation("Fallo al transferir el mensaje.");return;
    }
    if(flow==FLOW_RUN){
        if(n>=4&&!strcmp(f[0],"GPT_ACK")&&!strcmp(f[2],"R")){flow=FLOW_POLL;poll_at=RTC_GetTicks()-100;}
        else finish_generation("La API no pudo arrancar.");
        return;
    }
    if(flow==FLOW_POLL){
        if(n>=4&&!strcmp(f[0],"GPT_CHUNK")){
            uint32_t offset;if(!wire_number(f[2],&offset)||offset!=(unsigned)response_offset){finish_generation("Flujo fuera de orden.");return;}
            char piece[65];if(!wire_decode(piece,sizeof(piece),f[3])){finish_generation("Fragmento no valido.");return;}
            append_assistant(piece);response_offset+=(int)strlen(piece);poll_at=RTC_GetTicks()-100;
        } else if(n>=2&&!strcmp(f[0],"GPT_WAIT"))poll_at=RTC_GetTicks();
        else if(n>=2&&!strcmp(f[0],"GPT_DONE"))finish_generation(messages[assistant_index].text[0]?NULL:"[sin respuesta]");
        else if(n>=2&&!strcmp(f[0],"GPT_CANCELLED"))finish_generation(messages[assistant_index].text[0]?NULL:"[detenido]");
        else if(n>=3&&!strcmp(f[0],"GPT_ERROR")){char error[80];sprintf(error,"Error: %s",f[2]);if(!strcmp(f[2],"NO_WIFI"))wifi_ok=0;finish_generation(error);}
        else finish_generation("Respuesta de streaming no valida.");
        return;
    }
    if(flow==FLOW_CANCEL){finish_generation(messages[assistant_index].text[0]?NULL:"[detenido]");return;}
}
static void cancel_generation(void)
{
    if(assistant_index<0)return;
    rpc_abort();flow=FLOW_CANCEL;char cmd[48];sprintf(cmd,"GPT_CANCEL:%lu",generation_id);
    if(!rpc_start(cmd,generation_id,"GPT_CANCELLED",8000))finish_generation("[detenido]");
}

/* ---------- keyboard and lifecycle ---------- */
static void insert_char(char c)
{
    if(input_len>=INPUT_CAP)return;
    memmove(input+input_cursor+1,input+input_cursor,input_len-input_cursor+1);input[input_cursor++]=c;input_len++;display_dirty=1;
}
static char input_character(int key)
{
    static const int alpha_keys[]={76,66,56,46,36,26,75,65,55,45,35,25,74,64,54,73,63,53,43,33,72,62,52,42,32,71};
    if(alpha_once||alpha_lock){for(int i=0;i<26;i++)if(key==alpha_keys[i])return (upper_case?'A':'a')+i;if(key==61)return ' ';}
    static const int number_keys[]={71,72,62,52,73,63,53,74,64,54};for(int i=0;i<10;i++)if(key==number_keys[i])return '0'+i;
    switch(key){case 61:return '.';case 35:return ',';case 55:return '(';case 45:return ')';case 42:return '+';case 32:case 41:return '-';case 43:return '*';case 33:return '/';}
    return 0;
}
static void handle_chat_key(int key)
{
    if(key==KEY_PRGM_F6&&assistant_index>=0){cancel_generation();return;}
    if(key==KEY_PRGM_F2){upper_case=!upper_case;display_dirty=1;return;}
    if(key==KEY_PRGM_SHIFT){shift_pending=1;return;}
    if(key==KEY_PRGM_ALPHA){if(shift_pending){alpha_lock=!alpha_lock;alpha_once=0;}else{alpha_once=!alpha_once;if(alpha_lock){alpha_lock=0;alpha_once=0;}}shift_pending=0;display_dirty=1;return;}
    if(key==KEY_PRGM_RETURN){if(assistant_index<0)begin_prompt();return;}
    if(key==KEY_PRGM_UP){scroll_px+=LINE_H;display_dirty=1;return;}
    if(key==KEY_PRGM_DOWN){if(scroll_px>0)scroll_px-=LINE_H;display_dirty=1;return;}
    if(key==KEY_PRGM_LEFT){if(input_cursor>0)input_cursor--;display_dirty=1;return;}
    if(key==KEY_PRGM_RIGHT){if(input_cursor<input_len)input_cursor++;display_dirty=1;return;}
    if(key==44){if(input_cursor>0){memmove(input+input_cursor-1,input+input_cursor,input_len-input_cursor+1);input_cursor--;input_len--;}display_dirty=1;return;}
    if(key==KEY_PRGM_ACON){input[0]=0;input_len=input_cursor=0;display_dirty=1;return;}
    char c=input_character(key);if(c)insert_char(c);alpha_once=shift_pending=0;
}
static volatile int menu_timer,menu_sent;
static void send_menu_key(void)
{
    int id=menu_timer;menu_timer=0;if(id>0){Timer_Stop(id);Timer_Deinstall(id);}Keyboard_PutKeycode(4,9,0);menu_sent=1;
}
static void best_effort_cancel(void)
{
    if(assistant_index<0||!Serial_IsOpen())return;
    char payload[48],frame[WIRE_CAP];sprintf(payload,"GPT_CANCEL:%lu",generation_id);int n=wire_pack(frame,payload);if(Serial_PollTX()>=n)Serial_Write((const unsigned char *)frame,n);
}
static void leave_to_menu(short flags)
{
    best_effort_cancel();rpc_abort();int start=RTC_GetTicks();while(Serial_IsOpen()&&Serial_PollTX()<256&&!RTC_Elapsed_ms(start,150))CMT_Delay_100micros(10);
    if(Serial_IsOpen())Serial_Close(1);
    memset(api_key,0,sizeof(api_key));memset(active_prompt,0,sizeof(active_prompt));memset(messages,0,sizeof(messages));
    Bkey_SetAllFlags(flags);EnableStatusArea(1);start=RTC_GetTicks();while(PRGM_GetKey()!=KEY_PRGM_NONE&&!RTC_Elapsed_ms(start,500))CMT_Delay_100micros(20);
    menu_sent=0;menu_timer=Timer_Install(0,send_menu_key,50);if(menu_timer>0)Timer_Start(menu_timer);int key;do{GetKey(&key);}while(!menu_sent&&menu_timer>0);
    if(menu_timer>0){Timer_Stop(menu_timer);Timer_Deinstall(menu_timer);menu_timer=0;}
}
static void reset_app(void)
{
    memset(messages,0,sizeof(messages));memset(input,0,sizeof(input));memset(active_prompt,0,sizeof(active_prompt));
    message_count=input_len=input_cursor=scroll_px=0;assistant_index=-1;alpha_once=alpha_lock=shift_pending=upper_case=0;flow=FLOW_IDLE;status_at=0;
    app_error[0]=0;Bkey_ClrAllFlags();Bdisp_EnableColor(1);EnableStatusArea(0);draw_splash();begin_verify();
}
int main(void)
{
    for(;;){
        short flags=Bkey_GetAllFlags();reset_app();int previous=PRGM_GetKey(),held_at=RTC_GetTicks(),frame_at=0;
        for(;;){
            rpc_poll();if(verify_stage!=VERIFY_READY&&verify_stage!=VERIFY_FAILED)process_verify();else if(verify_stage==VERIFY_READY)process_chat_rpc();
            int key=PRGM_GetKey(),pressed=key&&key!=previous;if(key!=previous)held_at=RTC_GetTicks();
            int repeat=key==KEY_PRGM_UP||key==KEY_PRGM_DOWN||key==KEY_PRGM_LEFT||key==KEY_PRGM_RIGHT||key==44;
            if(repeat&&RTC_Elapsed_ms(held_at,250)){pressed=1;held_at=RTC_GetTicks();}previous=key;
            if(pressed&&(key==KEY_PRGM_EXIT||key==KEY_PRGM_MENU))break;
            if(pressed&&verify_stage==VERIFY_FAILED&&key==KEY_PRGM_F1)begin_verify();
            else if(pressed&&verify_stage==VERIFY_READY)handle_chat_key(key);
            if(verify_stage==VERIFY_READY){
                if(display_dirty||RTC_Elapsed_ms(frame_at,120)){render_chat();display_dirty=0;frame_at=RTC_GetTicks();}
            }else if(display_dirty||RTC_Elapsed_ms(frame_at,120)){render_verify();display_dirty=0;frame_at=RTC_GetTicks();}
            CMT_Delay_100micros(20);
        }
        leave_to_menu(flags);
    }
}
