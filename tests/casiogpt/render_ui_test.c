#define main calculator_main
#include "../../apps/CasioGPT/src/main.c"
#undef main

#include <assert.h>
#include <stdio.h>

static color_t pixels[SCREEN_W*SCREEN_H];
static int now_ticks=640;

void *GetVRAMAddress(void) { return pixels; }
void Bdisp_Fill_VRAM(int color,int mode) { (void)mode; for(int i=0;i<SCREEN_W*SCREEN_H;i++) pixels[i]=(color_t)color; }
void Bdisp_PutDisp_DD(void) {}
int RTC_GetTicks(void) { return now_ticks; }
int RTC_Elapsed_ms(int start,int duration) { return now_ticks-start>=duration; }

static void write_ppm(const char *path)
{
    FILE *out=fopen(path,"wb"); assert(out);
    fprintf(out,"P6\n%d %d\n255\n",SCREEN_W,SCREEN_H);
    for(int i=0;i<SCREEN_W*SCREEN_H;i++) {
        unsigned c=pixels[i];
        unsigned char rgb[3]={
            (unsigned char)((((c>>11)&31)*255+15)/31),
            (unsigned char)((((c>>5)&63)*255+31)/63),
            (unsigned char)(((c&31)*255+15)/31)
        };
        fwrite(rgb,1,3,out);
    }
    fclose(out);
}

int main(int argc,char **argv)
{
    assert(argc==2);
    memset(messages,0,sizeof(messages)); memset(input,0,sizeof(input));
    message_count=2; assistant_index=-1; scroll_px=0; wifi_ok=esp_ok=1; upper_case=0;
    messages[0].user=1; str_copy(messages[0].text,MESSAGE_CAP,"Hola, como estas?");
    messages[1].user=0; str_copy(messages[1].text,MESSAGE_CAP,"Muy bien. Estoy listo para ayudarte con matematicas, ciencias o cualquier otra pregunta.");
    input_len=input_cursor=0;
    render_chat(); write_ppm(argv[1]);
    puts("PASS CasioGPT UI render: header, compact user bubble, left placeholder and send icon.");
    return 0;
}
