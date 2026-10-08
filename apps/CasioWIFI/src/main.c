#include <fxcg/display.h>
#include <fxcg/keyboard.h>
#include <fxcg/serial.h>
#include <fxcg/rtc.h>
#include <fxcg/system.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include "ui_aa.h"
#include "brand.h"
#include "wire.h"

#define SCREEN_W 384
#define SCREEN_H 216
#define HEADER_H 31
#define FOOTER_Y 191

#define MAX_NETWORKS 32
#define SSID_MAX 32
#define VISIBLE_ROWS 6

/* Dark visual language shared with CasioVideo/CasioLLM. */
#define COL_BG          ((color_t)0x0841)
#define COL_HEADER      ((color_t)0x0000)
#define COL_CARD        ((color_t)0x10A3)
#define COL_CARD_ALT    ((color_t)0x18E4)
#define COL_TEXT        ((color_t)0xFFFF)
#define COL_MUTED       ((color_t)0x9CF3)
#define COL_ACCENT      ((color_t)0x167F)
#define COL_ACCENT_DARK ((color_t)0x0B39)
#define COL_BORDER      ((color_t)0x2945)
#define COL_GREEN       ((color_t)0x2E6D)
#define COL_RED         ((color_t)0xE228)
#define COL_ROW_ALT     ((color_t)0x1083)
#define COL_SCAN_BG     ((color_t)0x1128)
#define COL_PALE_BLUE   ((color_t)0x9D7F)
#define COL_WHITE       ((color_t)0xFFFF)
#define COL_BLACK       ((color_t)0x0000)

typedef struct {
    char ssid[SSID_MAX + 1];
    int rssi;
    int auth, flags;
} wifi_network_t;

static wifi_network_t networks[MAX_NETWORKS];
static int network_count = 0;
static int selected = 0;
static int scroll_top = 0;
static int uart_ready = 0;
static int wifi_connected = 0;
static char connected_ssid[33] = "";
enum { VIEW_HOME, VIEW_LIST, VIEW_PASSWORD, VIEW_SCAN, VIEW_JOIN, VIEW_ERROR };
static int view = VIEW_HOME;
static char password[65] = "";
static int password_len, password_cursor, alpha_once, alpha_lock, shift_pending, upper_case;
static int symbol_mode, symbol_cursor;
static const char symbols[] = " !\"#$%&'()*+,-./:;<=>?@[\\]^_\x60{|}~";
static char input_error[72] = "";
static unsigned long snapshot_id;
static int status_time, link_time;
static char last_error[72] = "";

static void str_copy(char *dst, int size, const char *src)
{
    int i = 0;
    if(size <= 0) return;
    while(src && src[i] && i < size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void str_join2(char *dst, int size, const char *a, const char *b)
{
    int n = 0;
    if(size <= 0) return;
    if(a) {
        while(*a && n < size - 1) dst[n++] = *a++;
    }
    if(b) {
        while(*b && n < size - 1) dst[n++] = *b++;
    }
    dst[n] = 0;
}

static void set_error(const char *text)
{
    str_copy(last_error, (int)sizeof(last_error), text);
}

/* ---------- low-level drawing ---------- */

static color_t *vram(void)
{
    return (color_t *)GetVRAMAddress();
}

static void putpx(int x, int y, color_t c)
{
    if(x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    vram()[y * SCREEN_W + x] = c;
}

static void fill_rect(int x1, int y1, int x2, int y2, color_t c)
{
    int x, y;
    color_t *p = vram();

    if(x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if(y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if(x1 < 0) x1 = 0;
    if(y1 < 0) y1 = 0;
    if(x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    if(y2 >= SCREEN_H) y2 = SCREEN_H - 1;

    for(y = y1; y <= y2; y++) {
        color_t *row = p + y * SCREEN_W + x1;
        for(x = x1; x <= x2; x++) *row++ = c;
    }
}

static void hline(int x1, int x2, int y, color_t c)
{
    int x;
    if(y < 0 || y >= SCREEN_H) return;
    if(x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if(x1 < 0) x1 = 0;
    if(x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    for(x = x1; x <= x2; x++) putpx(x, y, c);
}

static void fill_circle(int cx, int cy, int r, color_t c)
{
    int x, y;
    int rr = r * r;
    for(y = -r; y <= r; y++) {
        for(x = -r; x <= r; x++) {
            if(x*x + y*y <= rr) putpx(cx + x, cy + y, c);
        }
    }
}

static void ring(int cx, int cy, int r, int thickness, color_t c)
{
    int x, y;
    int ro = r * r;
    int ri = (r - thickness) * (r - thickness);
    for(y = -r; y <= r; y++) {
        for(x = -r; x <= r; x++) {
            int d = x*x + y*y;
            if(d <= ro && d >= ri) putpx(cx + x, cy + y, c);
        }
    }
}

static void rounded_rect(int x1, int y1, int x2, int y2, int r, color_t c)
{
    int dx, dy;
    int rr = r * r;

    fill_rect(x1 + r, y1, x2 - r, y2, c);
    fill_rect(x1, y1 + r, x2, y2 - r, c);

    for(dy = 0; dy <= r; dy++) {
        for(dx = 0; dx <= r; dx++) {
            int ox = r - dx;
            int oy = r - dy;
            if(ox*ox + oy*oy <= rr) {
                putpx(x1 + dx, y1 + dy, c);
                putpx(x2 - dx, y1 + dy, c);
                putpx(x1 + dx, y2 - dy, c);
                putpx(x2 - dx, y2 - dy, c);
            }
        }
    }
}

static void rounded_border(int x1, int y1, int x2, int y2, int r,
                           color_t border, color_t inner)
{
    rounded_rect(x1, y1, x2, y2, r, border);
    rounded_rect(x1 + 1, y1 + 1, x2 - 1, y2 - 1,
                 r > 1 ? r - 1 : 1, inner);
}

static color_t blend(color_t fg,color_t bg,int alpha);
static void mini_text(int x, int y, const char *s, color_t fg, color_t bg)
{
    (void)bg;
    while(*s) {
        unsigned char c = (unsigned char)*s++;
        if(c < 32 || c > 126) c = '?';
        const unsigned char *g=ui_aa[c-32];
        for(int yy=0;yy<UI_AA_H;yy++) for(int xx=0;xx<UI_AA_W;xx++) {
            int alpha=g[1+yy*UI_AA_W+xx];
            int px=x+xx,py=y+yy;
            if(alpha && px>=0 && px<SCREEN_W && py>=0 && py<SCREEN_H)
                putpx(px,py,blend(fg,vram()[py*SCREEN_W+px],alpha));
        }
        x+=g[0];
    }
}

static void mini_bold(int x, int y, const char *s, color_t fg, color_t bg)
{
    mini_text(x, y, s, fg, bg);
    mini_text(x + 1, y, s, fg, bg);
}

static int approx_text_width(const char *s)
{
    int width=0;
    while(*s) {
        unsigned char c=(unsigned char)*s++;
        if(c<32 || c>126) c='?';
        width += ui_aa[c-32][0];
    }
    return width;
}

static void fitted_text(int x,int y,int width,const char *s,color_t fg,color_t bg)
{
    char text[96];
    str_copy(text,sizeof text,s);
    int n=(int)strlen(text);
    if(approx_text_width(text)>width) {
        while(n>0 && approx_text_width(text)+approx_text_width("...")>width)
            text[--n]=0;
        strcat(text,"...");
    }
    mini_text(x,y,text,fg,bg);
}

static void mini_center(int y, const char *s, color_t fg, color_t bg)
{
    int x = (SCREEN_W - approx_text_width(s)) / 2;
    if(x < 4) x = 4;
    mini_text(x, y, s, fg, bg);
}

static color_t blend(color_t fg,color_t bg,int alpha)
{
    int r=(((fg>>11)&31)*alpha+((bg>>11)&31)*(255-alpha))/255;
    int g=(((fg>>5)&63)*alpha+((bg>>5)&63)*(255-alpha))/255;
    int b=((fg&31)*alpha+(bg&31)*(255-alpha))/255;
    return (color_t)((r<<11)|(g<<5)|b);
}
/* Four subpixel samples give smooth, native-resolution arcs without floats. */
static void wifi_icon(int cx,int cy,int radius,int phase,color_t active,color_t inactive,color_t bg)
{
    int thickness=radius>20 ? 4 : 2;
    for(int y=-radius-1;y<=3;y++) for(int x=-radius-1;x<=radius+1;x++) {
        int coverage=0, level=0;
        for(int sy=0;sy<2;sy++) for(int sx=0;sx<2;sx++) {
            int xx=x*4+sx*2+1, yy=y*4+sy*2+1;
            int d=xx*xx+yy*yy;
            if(d<=thickness*thickness*9) { coverage++; level=4; continue; }
            if(yy>=0 || abs(xx)*3>-yy*4) continue;
            for(int band=1;band<=3;band++) {
                int r=(radius*band*4)/3, inner=r-thickness*4;
                if(d<=r*r && d>=inner*inner) { coverage++; level=band; break; }
            }
        }
        if(coverage) putpx(cx+x,cy+y,blend(level==4 || level<=phase ? active : inactive,bg,coverage*255/4));
    }
}
static void slash_icon(int x,int y,int size,color_t fg)
{
    for(int i=0;i<size;i++) { putpx(x+i,y+i,fg); putpx(x+i+1,y+i,fg); }
}
static void lock_icon(int x,int y,color_t fg,color_t bg)
{
    ring(x+5,y+5,4,2,fg);
    fill_rect(x,y+5,x+10,y+12,fg);
    fill_rect(x+5,y+8,x+5,y+10,bg);
}
static void draw_header(const char *title, const char *right, int connected)
{
    int right_w = (right && right[0]) ? approx_text_width(right) : 0;

    fill_rect(0, 0, SCREEN_W - 1, HEADER_H - 1, COL_HEADER);
    fill_rect(0, 0, 4, HEADER_H - 1, COL_ACCENT);
    mini_bold(12, 10, title, COL_WHITE, COL_HEADER);

    if(right_w)
        mini_text(303-right_w,10,right,COL_MUTED,COL_HEADER);
    wifi_icon(342,23,16,3,wifi_connected ? COL_GREEN : COL_RED,COL_RED,COL_HEADER);
    if(!wifi_connected) slash_icon(332,7,20,COL_RED);
    fill_circle(374,15,4,connected ? COL_GREEN : COL_RED);
}

static void draw_key_hint(int x, int y, const char *key, const char *label,
                          color_t key_bg, color_t key_fg)
{
    int kw = approx_text_width(key) + 8;
    rounded_rect(x, y, x + kw, y + 14, 4, key_bg);
    mini_bold(x + 4, y + 4, key, key_fg, key_bg);
    mini_text(x + kw + 6, y + 4, label, COL_MUTED, COL_HEADER);
}

enum {
    FOOTER_HOME,
    FOOTER_BUSY,
    FOOTER_RESULTS,
    FOOTER_ERROR
};

static void draw_footer(int mode)
{
    fill_rect(0, FOOTER_Y, SCREEN_W - 1, SCREEN_H - 1, COL_HEADER);
    hline(0, SCREEN_W - 1, FOOTER_Y, COL_BORDER);

    if(mode == FOOTER_RESULTS) {
        draw_key_hint(8,198,"F1","BUSCAR",COL_ACCENT,COL_WHITE);
        draw_key_hint(130,198,"EXE","CONECTAR",COL_CARD_ALT,COL_WHITE);
        draw_key_hint(280,198,"EXIT","SALIR",COL_CARD_ALT,COL_WHITE);
    }
    else if(mode == FOOTER_BUSY) {
        draw_key_hint(40,198,"F6","CANCELAR",COL_CARD_ALT,COL_WHITE);
        draw_key_hint(239,198,"EXIT","SALIR",COL_CARD_ALT,COL_WHITE);
    }
    else {
        draw_key_hint(60, 198, "F1", mode == FOOTER_ERROR ? "ESCANEAR" : "ESCANEAR",
                      COL_ACCENT, COL_WHITE);
        draw_key_hint(263, 198, "EXIT", "SALIR", COL_CARD_ALT, COL_WHITE);
    }
}

static void draw_wifi_mark(int cx,int cy,int phase)
{
    wifi_icon(cx,cy+24,48,1+((phase/3)%3),COL_ACCENT,COL_BORDER,COL_CARD);
}

static void draw_splash(void)
{
    Bdisp_Fill_VRAM(COL_BG,1);
    for(int y=0;y<SCREEN_H;y++) hline(0,SCREEN_W-1,y,blend(COL_SCAN_BG,COL_BG,(SCREEN_H-y)*130/SCREEN_H));
    rounded_border(145,32,239,118,20,COL_BORDER,COL_CARD);
    wifi_icon(192,100,42,3,COL_ACCENT,COL_BORDER,COL_CARD);
    int bx=(SCREEN_W-BRAND_W)/2;
    for(int y=0;y<BRAND_H;y++) for(int x=0;x<BRAND_W;x++) {
        int alpha=brand_alpha[y*BRAND_W+x];
        if(alpha) putpx(bx+x,134+y,blend(COL_PALE_BLUE,vram()[(134+y)*SCREEN_W+bx+x],alpha));
    }
    mini_center(168,"WIRELESS, EN TU CALCULADORA",COL_MUTED,COL_BG);
    const char *site="samilososami.com";
    mini_text(SCREEN_W-12-approx_text_width(site),SCREEN_H-20,site,COL_MUTED,COL_BG);
    Bdisp_PutDisp_DD();
    CMT_Delay_100micros(4500);
}

/* ---------- UART protocol ---------- */

static int open_uart(void)
{
    unsigned char mode[6] = {0, 5, 0, 0, 0, 0}; /* 9600 8N1 */

    if(Serial_IsOpen()) Serial_Close(1);
    if(Serial_Open(mode) != 0 && !Serial_IsOpen()) return 0;

    Serial_ClearRX();
    Serial_ClearTX();
    return 1;
}

/* Cooperative transport: bounded work, paced writes, explicit request IDs. */
static wire_rx receiver;
static wifi_network_t pending_networks[MAX_NETWORKS];
static unsigned long request_id, previous_id;
static int busy, expected, received, waiting, queued, attempts, tx_offset;
static int request_time, scan_time, next_poll, bad_frames, rx_bytes, tx_time;
static int request_index=-1, ui_dirty;
enum { REQ_SCAN, REQ_GET, REQ_JOIN, REQ_JSTATE, REQ_STATE };
static int request_kind;
static char tx_frame[WIRE_CAP], join_payload[210];
static int tx_length;
static void finish_error(const char *message)
{
    int background=busy==3;
    if(!background) set_error(message);
    busy=waiting=queued=0; uart_ready=0; ui_dirty=1;
    status_time=RTC_GetTicks();
    if(!background) view=VIEW_ERROR;
    memset(join_payload,0,sizeof(join_payload));
}
static unsigned long new_id(void)
{
    unsigned long id=(unsigned long)RTC_GetTicks();
    if(id<=previous_id) id=previous_id+1;
    previous_id=id; return id;
}
static void queue_request(int kind,int index)
{
    char payload[210];
    if(kind==REQ_SCAN) sprintf(payload,"SCAN:%lu",request_id);
    else if(kind==REQ_GET) sprintf(payload,"GET:%lu:%d",request_id,index);
    else if(kind==REQ_JOIN) str_copy(payload,sizeof(payload),join_payload);
    else if(kind==REQ_JSTATE) sprintf(payload,"JSTATE:%lu",request_id);
    else sprintf(payload,"STATE:%lu",request_id);
    tx_length=wire_pack(tx_frame,payload); memset(payload,0,sizeof(payload));
    request_kind=kind; request_index=index;
    queued=1; waiting=0; attempts=0; tx_offset=0;
    request_time=tx_time=RTC_GetTicks();
}
static int prepare_transaction(int type)
{
    busy=waiting=queued=0;
    /* Background link checks must not erase an error still on screen. */
    if(type!=3) last_error[0]=0;
    if(!Serial_IsOpen() && !open_uart()) { busy=type; finish_error("No se pudo abrir UART."); return 0; }
    Serial_ClearRX(); memset(&receiver,0,sizeof(receiver));
    request_id=new_id(); expected=received=bad_frames=rx_bytes=0;
    scan_time=next_poll=RTC_GetTicks(); busy=type; ui_dirty=1;
    return 1;
}
static void begin_scan(void)
{
    /* A background status query may be preempted by a user action. */
    if(!prepare_transaction(1)) return;
    view=VIEW_SCAN; queue_request(REQ_SCAN,-1);
}
static void begin_status(void)
{
    if(!prepare_transaction(3)) return;
    queue_request(REQ_STATE,-1);
}
static void begin_join(void)
{
    char hex[129]="-";
    if(password_len) wire_encode(hex,password);
    if(!prepare_transaction(2)) return;
    sprintf(join_payload,"JOIN:%lu:%lu:%d:%s",request_id,snapshot_id,selected,hex);
    memset(hex,0,sizeof(hex));
    view=VIEW_JOIN; queue_request(REQ_JOIN,-1);
    memset(password,0,sizeof(password)); password_len=password_cursor=0;
}
static void mark_connected(void)
{
    for(int i=0;i<network_count;i++) {
        networks[i].flags &= ~1;
        if(wifi_connected && !strcmp(networks[i].ssid,connected_ssid)) networks[i].flags|=1;
    }
}
static void accept_reply(char *line)
{
    char *f[8]; uint32_t id,n,number,auth,flags;
    int fields=wire_split(line,f,8);
    if(!busy || !waiting || fields<2 || !wire_number(f[1],&id) || id!=request_id) return;
    if(fields==2 && !strcmp(f[0],"WAIT") && busy==1) {
        if(request_kind!=REQ_SCAN && request_index!=-1) return;
        uart_ready=1; link_time=RTC_GetTicks(); waiting=0; next_poll=RTC_GetTicks(); return;
    }
    if(fields==3 && !strcmp(f[0],"READY") && busy==1) {
        if(request_kind!=REQ_SCAN && request_index!=-1) return;
        if(!wire_number(f[2],&n) || n>MAX_NETWORKS) { finish_error("Cantidad de redes invalida."); return; }
        uart_ready=1; link_time=RTC_GetTicks(); expected=(int)n; received=0; waiting=0;
        if(n) { queue_request(REQ_GET,0); return; }
        network_count=0; selected=scroll_top=0; snapshot_id=request_id;
        busy=0; view=VIEW_LIST; ui_dirty=1; status_time=RTC_GetTicks()-3000; return;
    }
    if(fields==7 && !strcmp(f[0],"ITEM") && busy==1) {
        if(request_kind!=REQ_GET || !wire_number(f[2],&n) || n!=(unsigned)request_index ||
           n!=(unsigned)received || n>=(unsigned)expected) return;
        if(f[3][0]!='-' || !wire_number(f[3]+1,&number) || number>127 ||
           !wire_number(f[4],&auth) || auth>32 || !wire_number(f[5],&flags) || flags>3 ||
           !wire_decode(pending_networks[n].ssid,33,f[6])) {
            finish_error("Datos de red invalidos."); return;
        }
        pending_networks[n].rssi=-(int)number; pending_networks[n].auth=(int)auth;
        pending_networks[n].flags=(int)flags;
        received++; waiting=0; link_time=RTC_GetTicks();
        if(received<expected) { queue_request(REQ_GET,received); return; }
        memcpy(networks,pending_networks,sizeof(networks));
        network_count=received; selected=scroll_top=0; snapshot_id=request_id;
        busy=0; uart_ready=1; view=VIEW_LIST; ui_dirty=1; status_time=RTC_GetTicks()-3000; return;
    }
    if(fields==5 && !strcmp(f[0],"LINK") && (busy==2 || busy==3)) {
        if(!wire_number(f[2],&n) || n>3) return;
        char ssid[33];
        if(!wire_decode(ssid,sizeof(ssid),f[3])) return;
        uart_ready=1; link_time=RTC_GetTicks(); wifi_connected=(n==2);
        str_copy(connected_ssid,sizeof(connected_ssid),ssid); mark_connected();
        waiting=0; ui_dirty=1;
        if(busy==3) { busy=0; status_time=RTC_GetTicks(); return; }
        memset(join_payload,0,sizeof(join_payload));
        if(n==1) { next_poll=RTC_GetTicks(); return; }
        busy=0; status_time=RTC_GetTicks(); view=VIEW_LIST;
        if(n==2) {
            if(!strcmp(f[4],"OK")) networks[selected].flags|=2;
            if(!strcmp(f[4],"SAVE")) str_copy(input_error,sizeof(input_error),"Conectada. No se pudo guardar.");
            return;
        }
        set_error(!strcmp(f[4],"AUTH") ? "Contrasena rechazada." :
                  !strcmp(f[4],"NO_AP") ? "La red ya no esta disponible." : "No se pudo conectar a la red.");
        view=VIEW_ERROR; return;
    }
    if(fields==3 && !strcmp(f[0],"ERROR")) {
        const char *message="La ESP32 no pudo completar la orden.";
        if(!strcmp(f[2],"SESSION")) message="Lista caducada. Pulsa F1.";
        else if(!strcmp(f[2],"PASSWORD")) message="Clave no valida: 8-63 caracteres.";
        else if(!strcmp(f[2],"AUTH_TYPE")) message="Esta seguridad no esta soportada.";
        else if(!strcmp(f[2],"BUSY")) message="ESP32 ocupada. Vuelve a intentarlo.";
        else if(!strcmp(f[2],"SCAN")) message="El escaneo Wi-Fi no se completo.";
        finish_error(message);
    }
}
static void poll_transport(void)
{
    if(!Serial_IsOpen()) return;
    for(int budget=0;budget<256 && Serial_PollRX()>0;budget++) {
        unsigned char c=0;
        if(Serial_ReadSingle(&c)!=0) break;
        rx_bytes++;
        int status=wire_feed(&receiver,c);
        if(status>0) accept_reply(receiver.data);
        else if(status<0) bad_frames++;
    }
    if(!busy) return;
    if(RTC_Elapsed_ms(scan_time,busy==3 ? 6500 : 35000)) { finish_error("Tiempo de espera agotado."); return; }
    if(waiting && RTC_Elapsed_ms(request_time,1400)) {
        if(attempts>=8) {
            finish_error(bad_frames ? "UART: datos danados." : rx_bytes ? "UART: respuesta incompleta." : "UART: no llegan bytes."); return;
        }
        if(attempts==3) {
            /* Recover the OS UART, retaining the same idempotent request. */
            if(!open_uart()) { finish_error("No se pudo recuperar UART."); return; }
            memset(&receiver,0,sizeof(receiver));
        }
        waiting=0; queued=1; tx_offset=0; request_time=RTC_GetTicks();
    }
    if(!waiting && !queued && RTC_Elapsed_ms(next_poll,300))
        queue_request(busy==2 ? REQ_JSTATE : REQ_GET,-1);
    if(queued && RTC_Elapsed_ms(tx_time,12)) {
        int chunk=tx_length-tx_offset; if(chunk>8) chunk=8;
        if(Serial_PollTX()>=chunk && Serial_Write((const unsigned char *)tx_frame+tx_offset,chunk)==0) {
            tx_offset+=chunk; tx_time=RTC_GetTicks();
            if(tx_offset==tx_length) {
                queued=0; waiting=1; attempts++; request_time=RTC_GetTicks();
            }
        } else if(RTC_Elapsed_ms(request_time,1600)) {
            if(!open_uart()) { finish_error("UART: buffer TX bloqueado."); return; }
            tx_offset=0; request_time=RTC_GetTicks();
        }
    }
}

/* ---------- screens ---------- */

static void present(void)
{
    Bdisp_PutDisp_DD();
}

static void render_home(void)
{
    Bdisp_Fill_VRAM(COL_BG, 1);
    draw_header("CasioWIFI", "", uart_ready);

    rounded_border(16, 45, 367, 175, 12, COL_BORDER, COL_CARD);
    rounded_rect(34, 62, 142, 158, 14, COL_CARD);
    draw_wifi_mark(88, 110, 0);

    mini_bold(166, 70, "Escaner Wi-Fi", COL_TEXT, COL_CARD);
    mini_text(166,91,"Busca, elige y conecta.",COL_MUTED,COL_CARD);
    mini_text(166,107,"Wi-Fi con tu ESP32 integrada.",COL_MUTED,COL_CARD);
    rounded_rect(166, 132, 330, 156, 6, COL_ACCENT_DARK);
    mini_bold(193, 141, "F1  ESCANEAR", COL_WHITE, COL_ACCENT_DARK);

    draw_footer(FOOTER_HOME);
    present();
}

static void render_scan(int phase, int finalizing)
{
    static const char *dots[4] = {"", ".", "..", "..."};
    char msg[48];
    int segment_x;

    Bdisp_Fill_VRAM(COL_BG, 1);
    draw_header("CasioWIFI", view==VIEW_JOIN ? "CONECTANDO" : "BUSCANDO", uart_ready);

    rounded_border(56, 44, 327, 178, 12, COL_BORDER, COL_CARD);
    draw_wifi_mark(192, 94, phase);

    str_join2(msg, (int)sizeof(msg),
              view==VIEW_JOIN ? "Conectando a la red" : finalizing ? "Recibiendo redes" : "Buscando redes",
              dots[(phase / 2) & 3]);
    mini_center(148, msg, COL_TEXT, COL_CARD);
    if(view==VIEW_JOIN) fitted_text(76,163,231,networks[selected].ssid,COL_MUTED,COL_CARD);
    else {
        char detail[48];
        if(finalizing) sprintf(detail,"%d / %d redes",received,expected);
        else str_copy(detail,sizeof(detail),"Explorando los canales Wi-Fi");
        mini_center(163,detail,COL_MUTED,COL_CARD);
    }

    /* indeterminate progress track */
    rounded_rect(94, 183, 290, 187, 2, COL_BORDER);
    segment_x = 96 + ((phase * 18) % 150);
    rounded_rect(segment_x, 183, segment_x + 42, 187, 2, COL_ACCENT);

    draw_footer(FOOTER_BUSY);
    present();
}

static void render_error(void)
{
    Bdisp_Fill_VRAM(COL_BG, 1);
    draw_header("CasioWIFI", "ERROR", 0);

    rounded_border(24, 50, 359, 174, 12, COL_BORDER, COL_CARD);
    fill_circle(66, 90, 17, COL_RED);
    mini_bold(62, 85, "!", COL_WHITE, COL_RED);
    mini_bold(98, 70, "No se pudo completar el escaneo", COL_TEXT, COL_CARD);
    fitted_text(98, 95, 242, last_error[0] ? last_error : "Error desconocido.",
              COL_PALE_BLUE, COL_CARD);
    mini_text(98, 119, "El escaneo se ha detenido.", COL_MUTED, COL_CARD);
    mini_text(98,134,"F1 buscar / F6 volver a la lista",COL_MUTED,COL_CARD);

    draw_footer(FOOTER_ERROR);
    present();
}

static int signal_level(int rssi)
{
    if(rssi >= -55) return 4;
    if(rssi >= -65) return 3;
    if(rssi >= -75) return 2;
    return 1;
}

static void draw_signal(int x, int y, int level, color_t active, color_t inactive)
{
    int i;
    for(i = 0; i < 4; i++) {
        int h = 4 + i * 3;
        color_t c = i < level ? active : inactive;
        fill_rect(x + i * 5, y + 13 - h, x + i * 5 + 2, y + 13, c);
    }
}

static void render_results(void)
{
    int row;
    char right[32];

    Bdisp_Fill_VRAM(COL_BG, 1);
    sprintf(right, "%d REDES", network_count);
    draw_header("CasioWIFI", right, uart_ready);

    mini_bold(10, 38, "Redes cercanas", COL_TEXT, COL_BG);
    mini_text(282, 38, "senal", COL_MUTED, COL_BG);

    if(network_count == 0) {
        rounded_border(25, 61, 358, 174, 12, COL_BORDER, COL_CARD);
        draw_wifi_mark(83, 116, 0);
        mini_bold(132, 93, "No se encontraron redes", COL_TEXT, COL_CARD);
        mini_text(132,116,"Pulsa F1 para buscar de nuevo.",COL_MUTED,COL_CARD);
    }
    else {
        for(row = 0; row < VISIBLE_ROWS; row++) {
            int idx = scroll_top + row;
            int y = 54 + row * 22;
            int is_sel;
            color_t bg;
            color_t fg;
            color_t muted;
            char rssi_text[20];

            if(idx >= network_count) break;
            is_sel = idx == selected;
            bg = is_sel ? COL_ACCENT_DARK : ((row & 1) ? COL_ROW_ALT : COL_CARD);
            fg = is_sel ? COL_WHITE : COL_TEXT;
            muted = is_sel ? COL_PALE_BLUE : COL_MUTED;

            rounded_rect(8, y, 375, y + 19, 5, bg);
            if(!is_sel) {
                hline(14, 369, y + 19, COL_BORDER);
            }

            draw_signal(17, y + 2, signal_level(networks[idx].rssi),
                        is_sel ? COL_WHITE : COL_ACCENT,
                        is_sel ? COL_MUTED : COL_BORDER);

            fitted_text(48,y+4,200,networks[idx].ssid[0] ? networks[idx].ssid : "<sin nombre>",fg,bg);
            sprintf(rssi_text,"%d dBm",networks[idx].rssi);
            mini_text(311-approx_text_width(rssi_text),y+4,rssi_text,muted,bg);
            if(networks[idx].flags&1) wifi_icon(326,y+15,10,3,COL_GREEN,COL_GREEN,bg);
            if(networks[idx].auth) lock_icon(354,y+3,fg,bg);
            else mini_text(342,y+5,"FREE",COL_GREEN,bg);
        }

        if(network_count > VISIBLE_ROWS) {
            int track_y1 = 56;
            int track_y2 = 183;
            int track_h = track_y2 - track_y1;
            int thumb_h = (VISIBLE_ROWS * track_h) / network_count;
            int max_scroll = network_count - VISIBLE_ROWS;
            int thumb_y;
            if(thumb_h < 18) thumb_h = 18;
            thumb_y = track_y1 + (max_scroll ? (scroll_top * (track_h - thumb_h)) / max_scroll : 0);
            rounded_rect(379, track_y1, 382, track_y2, 1, COL_BORDER);
            rounded_rect(379, thumb_y, 382, thumb_y + thumb_h, 1, COL_ACCENT);
        }
    }

    draw_footer(FOOTER_RESULTS);
    present();
}



static void keep_selection_visible(void)
{
    if(selected < 0) selected = 0;
    if(selected >= network_count) selected = network_count - 1;
    if(selected < 0) selected = 0;

    if(selected < scroll_top) scroll_top = selected;
    if(selected >= scroll_top + VISIBLE_ROWS)
        scroll_top = selected - VISIBLE_ROWS + 1;

    if(scroll_top < 0) scroll_top = 0;
    if(network_count > VISIBLE_ROWS && scroll_top > network_count - VISIBLE_ROWS)
        scroll_top = network_count - VISIBLE_ROWS;
}

static void render_password(void)
{
    Bdisp_Fill_VRAM(COL_BG,1);
    draw_header("CasioWIFI","CONECTAR",uart_ready);
    mini_bold(18,42,"Contrasena de la red",COL_TEXT,COL_BG);
    fitted_text(18,62,345,networks[selected].ssid,COL_PALE_BLUE,COL_BG);
    rounded_border(16,84,367,122,8,COL_ACCENT_DARK,COL_CARD);
    char visible[65], before[65];
    int start=0;
    while(start<password_cursor) {
        memcpy(before,password+start,password_cursor-start); before[password_cursor-start]=0;
        if(approx_text_width(before)<310) break;
        start++;
    }
    str_copy(visible,sizeof(visible),password+start);
    while(approx_text_width(visible)>322) visible[strlen(visible)-1]=0;
    mini_text(28,98,visible,COL_TEXT,COL_CARD);
    memcpy(before,password+start,password_cursor-start); before[password_cursor-start]=0;
    int cx=28+approx_text_width(before);
    fill_rect(cx,95,cx,113,COL_ACCENT);
    if(!password_len && (networks[selected].flags&2))
        mini_text(28,98,"EXE: usar contrasena guardada",COL_MUTED,COL_CARD);
    if(symbol_mode) {
        int count=(int)strlen(symbols);
        for(int i=0;i<count;i++) {
            int x=20+(i%11)*31, y=131+(i/11)*18;
            color_t bg=i==symbol_cursor ? COL_ACCENT_DARK : COL_BG;
            rounded_rect(x,y,x+27,y+16,3,bg);
            char s[2]={symbols[i],0}; mini_text(x+10,y+3,s,COL_TEXT,bg);
        }
    } else {
        mini_text(18,136,alpha_lock ? (upper_case ? "ABC fijo" : "abc fijo") :
                  alpha_once ? (upper_case ? "ABC" : "abc") : "123",COL_ACCENT,COL_BG);
        if(input_error[0]) fitted_text(18,156,349,input_error,COL_RED,COL_BG);
    }
    fill_rect(0,FOOTER_Y,383,215,COL_HEADER);
    draw_key_hint(8,198,"F2","a/A",COL_CARD_ALT,COL_WHITE);
    draw_key_hint(109,198,"F3","#+?",COL_CARD_ALT,COL_WHITE);
    draw_key_hint(218,198,"EXE","CONECTAR",COL_ACCENT,COL_WHITE);
    present();
}

static void insert_password(char c)
{
    if(password_len>=64) return;
    memmove(password+password_cursor+1,password+password_cursor,password_len-password_cursor+1);
    password[password_cursor++]=c; password_len++; input_error[0]=0;
}
static char password_character(int key)
{
    static const int alpha_keys[]={76,66,56,46,36,26,75,65,55,45,35,25,74,64,54,73,63,53,43,33,72,62,52,42,32,71};
    if(alpha_once || alpha_lock) {
        for(int i=0;i<26;i++) if(key==alpha_keys[i]) return (upper_case ? 'A' : 'a')+i;
        if(key==61) return ' ';
    }
    static const int number_keys[]={71,72,62,52,73,63,53,74,64,54};
    for(int i=0;i<10;i++) if(key==number_keys[i]) return '0'+i;
    switch(key) {
        case 61:return '.'; case 35:return ','; case 55:return '('; case 45:return ')';
        case 42:return '+'; case 32:case 41:return '-'; case 43:return '*'; case 33:return '/';
    }
    return 0;
}
static void password_key(int key)
{
    ui_dirty=1;
    if(symbol_mode) {
        int n=(int)strlen(symbols);
        if(key==KEY_PRGM_LEFT) symbol_cursor=(symbol_cursor+n-1)%n;
        if(key==KEY_PRGM_RIGHT) symbol_cursor=(symbol_cursor+1)%n;
        if(key==KEY_PRGM_UP) symbol_cursor=(symbol_cursor+n-11)%n;
        if(key==KEY_PRGM_DOWN) symbol_cursor=(symbol_cursor+11)%n;
        if(key==KEY_PRGM_RETURN) { insert_password(symbols[symbol_cursor]); symbol_mode=0; }
        if(key==KEY_PRGM_F3 || key==KEY_PRGM_F6) symbol_mode=0;
        return;
    }
    if(key==KEY_PRGM_F6) { memset(password,0,sizeof(password)); password_len=0; view=VIEW_LIST; return; }
    if(key==KEY_PRGM_F2) { upper_case=!upper_case; return; }
    if(key==KEY_PRGM_F3) { symbol_mode=1; symbol_cursor=0; return; }
    if(key==KEY_PRGM_SHIFT) { shift_pending=1; return; }
    if(key==KEY_PRGM_ALPHA) {
        if(shift_pending) { alpha_lock=!alpha_lock; alpha_once=0; }
        else { alpha_once=!alpha_once; if(alpha_lock) { alpha_lock=0; alpha_once=0; } }
        shift_pending=0; return;
    }
    if(key==KEY_PRGM_LEFT) { if(password_cursor) password_cursor--; return; }
    if(key==KEY_PRGM_RIGHT) { if(password_cursor<password_len) password_cursor++; return; }
    if(key==44) {
        if(password_cursor) { memmove(password+password_cursor-1,password+password_cursor,password_len-password_cursor+1); password_cursor--; password_len--; }
        return;
    }
    if(key==KEY_PRGM_ACON) { memset(password,0,sizeof(password)); password_len=password_cursor=0; return; }
    if(key==KEY_PRGM_RETURN) {
        int valid=password_len>=8 && password_len<=63;
        if(password_len==64) { valid=1; for(int i=0;i<64;i++) if(wire_hex(password[i])<0) valid=0; }
        if(!password_len && (networks[selected].flags&2)) valid=1;
        if(!valid) { str_copy(input_error,sizeof(input_error),"Usa 8-63 caracteres o una clave hex de 64."); return; }
        begin_join(); return;
    }
    char c=password_character(key);
    if(c) insert_password(c);
    alpha_once=shift_pending=0;
}

static volatile int menu_timer=0, menu_sent=0;
static void send_menu_key(void)
{
    int id=menu_timer; menu_timer=0;
    if(id>0) { Timer_Stop(id); Timer_Deinstall(id); }
    Keyboard_PutKeycode(4,9,0); /* Matrix MENU, not an ordinary returned keycode. */
    menu_sent=1;
}
static void cancel_activity(void)
{
    if(busy==1 || busy==2) {
        char payload[48], frame[WIRE_CAP];
        sprintf(payload,"CANCEL:%lu",request_id);
        int len=wire_pack(frame,payload);
        if(Serial_IsOpen() && Serial_PollTX()>=len) Serial_Write((const unsigned char *)frame,len);
    }
    busy=queued=waiting=0;
    memset(tx_frame,0,sizeof(tx_frame)); memset(join_payload,0,sizeof(join_payload));
    memset(password,0,sizeof(password)); password_len=password_cursor=0;
}
static void leave_to_menu(short keyboard_flags)
{
    cancel_activity();
    /* Brief bounded drain; Close(1) always aborts anything still pending. */
    int start=RTC_GetTicks();
    while(Serial_IsOpen() && Serial_PollTX()<256 && !RTC_Elapsed_ms(start,150))
        CMT_Delay_100micros(10);
    if(Serial_IsOpen()) Serial_Close(1);
    memset(&receiver,0,sizeof(receiver));
    memset(pending_networks,0,sizeof(pending_networks));
    uart_ready=wifi_connected=0;
    Bkey_SetAllFlags(keyboard_flags); EnableStatusArea(1);
    start=RTC_GetTicks();
    while(PRGM_GetKey()!=KEY_PRGM_NONE && !RTC_Elapsed_ms(start,500)) CMT_Delay_100micros(20);
    Bdisp_Fill_VRAM(COL_BG,1);
    mini_center(95,"CasioWIFI cerrado",COL_TEXT,COL_BG);
    mini_center(116,"MENU para volver al inicio",COL_MUTED,COL_BG);
    Bdisp_PutDisp_DD();
    menu_sent=0; menu_timer=Timer_Install(0,send_menu_key,50);
    if(menu_timer>0) Timer_Start(menu_timer);
    int key;
    do { GetKey(&key); } while(!menu_sent && menu_timer>0);
    if(menu_timer>0) { Timer_Stop(menu_timer); Timer_Deinstall(menu_timer); menu_timer=0; }
    /* Selecting this same add-in resumes GetKey; restart all our state below.
       Selecting another app lets the OS dispose of this execution context. */
}
static void reset_app(void)
{
    cancel_activity();
    memset(networks,0,sizeof(networks)); memset(pending_networks,0,sizeof(pending_networks));
    memset(&receiver,0,sizeof(receiver)); memset(connected_ssid,0,sizeof(connected_ssid));
    network_count=selected=scroll_top=uart_ready=wifi_connected=0;
    password_len=password_cursor=alpha_once=alpha_lock=shift_pending=upper_case=symbol_mode=0;
    last_error[0]=input_error[0]=0; view=VIEW_HOME; ui_dirty=0;
    Bkey_ClrAllFlags(); Bdisp_EnableColor(1); EnableStatusArea(0);
    draw_splash(); open_uart(); render_home();
    status_time=RTC_GetTicks()-3000; link_time=RTC_GetTicks();
}
int main(void)
{
    for(;;) {
        short keyboard_flags=Bkey_GetAllFlags();
        reset_app();
        int previous_key=PRGM_GetKey(), key_time=RTC_GetTicks();
        int frame_time=RTC_GetTicks(), phase=0;
        for(;;) {
            poll_transport();
            int key=PRGM_GetKey();
            int new_key=key && key!=previous_key;
            if(key!=previous_key) key_time=RTC_GetTicks();
            int repeatable=key==KEY_PRGM_UP || key==KEY_PRGM_DOWN ||
                           (view==VIEW_PASSWORD && (key==KEY_PRGM_LEFT || key==KEY_PRGM_RIGHT || key==44));
            if(repeatable && RTC_Elapsed_ms(key_time,250)) { new_key=1; key_time=RTC_GetTicks(); }
            previous_key=key;
            if(new_key && (key==KEY_PRGM_MENU || key==KEY_PRGM_EXIT)) break;
            if(new_key && key==KEY_PRGM_F6 && (busy==1 || busy==2)) {
                cancel_activity(); view=network_count ? VIEW_LIST : VIEW_HOME; ui_dirty=1;
            } else if(new_key && view==VIEW_PASSWORD) password_key(key);
            else if(new_key && key==KEY_PRGM_F1 && (busy==0 || busy==3)) {
                begin_scan(); phase=0;
            } else if(new_key && view==VIEW_ERROR && key==KEY_PRGM_F6) {
                view=network_count ? VIEW_LIST : VIEW_HOME; last_error[0]=0; ui_dirty=1;
            } else if(new_key && view==VIEW_LIST && (busy==0 || busy==3) && network_count) {
                if(key==KEY_PRGM_UP && selected>0) { selected--; ui_dirty=1; }
                if(key==KEY_PRGM_DOWN && selected+1<network_count) { selected++; ui_dirty=1; }
                keep_selection_visible();
                if(key==KEY_PRGM_RETURN) {
                    input_error[0]=0; memset(password,0,sizeof(password)); password_len=password_cursor=0;
                    alpha_once=alpha_lock=shift_pending=symbol_mode=0;
                    if(networks[selected].auth) { view=VIEW_PASSWORD; ui_dirty=1; }
                    else { begin_join(); phase=0; }
                }
            }
            if(!busy && RTC_Elapsed_ms(status_time,3000)) begin_status();
            if(uart_ready && RTC_Elapsed_ms(link_time,8000)) { uart_ready=0; ui_dirty=1; }
            if((view==VIEW_SCAN || view==VIEW_JOIN) && (ui_dirty || RTC_Elapsed_ms(frame_time,100))) {
                render_scan(phase++,expected>0); frame_time=RTC_GetTicks(); ui_dirty=0;
            } else if(ui_dirty) {
                if(view==VIEW_ERROR) render_error();
                else if(view==VIEW_PASSWORD) render_password();
                else if(view==VIEW_LIST) render_results();
                else if(view==VIEW_HOME) render_home();
                ui_dirty=0;
            }
            CMT_Delay_100micros(20);
        }
        leave_to_menu(keyboard_flags);
    }
}
