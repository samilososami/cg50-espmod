#define main calculator_main
#include "../../apps/CasioWIFI/src/main.c"
#undef main
static color_t buffer[384*216];
static const char *output_dir;
void *GetVRAMAddress(void) { return buffer; }
void Bdisp_Fill_VRAM(int c,int mode) { (void)mode;for(int i=0;i<384*216;i++) buffer[i]=c; }
void Bdisp_PutDisp_DD(void) {}
void CMT_Delay_100micros(int n) { (void)n; }
static void save(const char *name)
{
    char path[512];
    snprintf(path,sizeof(path),"%s/%s",output_dir,name);
    FILE *f=fopen(path,"wb"); if(!f) exit(1);
    fprintf(f,"P6\n384 216\n255\n");
    for(int i=0;i<384*216;i++) {
        unsigned char rgb[3]={((buffer[i]>>11)&31)*255/31,((buffer[i]>>5)&63)*255/63,(buffer[i]&31)*255/31};
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    output_dir=argv[1];
    draw_splash();save("splash.ppm");
    render_home();save("home.ppm");
    view=VIEW_SCAN;uart_ready=1;render_scan(4,0);save("scan.ppm");
    const char *names[]={"Casa","Biblioteca","Una red con un nombre muy largo","Guest Wi-Fi","Estudio","Taller"};
    network_count=6;wifi_connected=1;
    for(int i=0;i<6;i++) {str_copy(networks[i].ssid,33,names[i]);networks[i].rssi=-40-i*9;networks[i].auth=i==1 ? 0 : 3;}
    networks[0].flags=1;view=VIEW_LIST;render_results();save("list.ppm");
    view=VIEW_PASSWORD;str_copy(password,65,"My_Wifi-123!");password_len=password_cursor=strlen(password);
    render_password();save("password.ppm");
    symbol_mode=1;symbol_cursor=17;render_password();save("symbols.ppm");
    view=VIEW_JOIN;render_scan(7,0);save("join.ppm");
    puts("PASS CasioWIFI UI render: splash, home, scan, list, password, symbols and join screens.");
    return 0;
}
