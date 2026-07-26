#include "display/screen_field.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void write_ppm(const char *p, canvas_t *cv, int s){
    FILE*f=fopen(p,"wb"); fprintf(f,"P6\n%d %d\n255\n",cv->w*s,cv->h*s);
    for(int y=0;y<cv->h;y++)for(int sy=0;sy<s;sy++)for(int x=0;x<cv->w;x++){
        uint16_t c=cv->px[y*cv->w+x];
        unsigned char rgb[3]={(unsigned char)(((c>>11)&0x1F)*255/31),
            (unsigned char)(((c>>5)&0x3F)*255/63),(unsigned char)((c&0x1F)*255/31)};
        for(int sx=0;sx<s;sx++)fwrite(rgb,1,3,f);} fclose(f);}
int main(int argc,char**argv){
    int s=(argc>1)?atoi(argv[1]):3;
    static uint16_t buf[320*170]; canvas_t cv={buf,320,170};
    struct{const char*n; field_input_t in;} sh[]={
      {"f1-repos",{.arousal=0.28f,.social_hunger=0.18f,.curiosity=0.05f,.unease=0.0f,
                   .attention_x=-2.0f,.t_seconds=1.2f,.online=true,.clock="21:04",.footer=""}},
      {"f2-attentif",{.arousal=0.88f,.social_hunger=0.20f,.curiosity=0.20f,.unease=0.0f,
                   .attention_x=0.60f,.t_seconds=0.5f,.online=true,.clock="09:12",.footer=""}},
      {"f3-curieux",{.arousal=0.62f,.social_hunger=0.50f,.curiosity=0.90f,.unease=0.12f,
                   .attention_x=-0.35f,.t_seconds=2.9f,.online=true,.clock="15:47",.footer="quelque chose a bouge"}},
      {"f4-inquiet",{.arousal=0.78f,.social_hunger=0.70f,.curiosity=0.40f,.unease=0.85f,
                   .attention_x=-2.0f,.t_seconds=5.1f,.online=false,.clock="03:22",.footer="la piece a change"}},
      {"f5-seul",{.arousal=0.20f,.social_hunger=0.97f,.curiosity=0.05f,.unease=0.22f,
                   .attention_x=-2.0f,.t_seconds=3.6f,.online=true,.clock="04:10",.footer=""}},
    };
    for(unsigned i=0;i<sizeof(sh)/sizeof(sh[0]);i++){
        screen_field_draw(&cv,&sh[i].in);
        char p[128]; snprintf(p,sizeof(p),"out/%s.ppm",sh[i].n); write_ppm(p,&cv,s);
        printf("  %s\n",p);}
    for(int k=0;k<6;k++){ field_input_t in=sh[0].in; in.t_seconds=k*0.55f;
        screen_field_draw(&cv,&in); char p[128];
        snprintf(p,sizeof(p),"out/fanim-%02d.ppm",k); write_ppm(p,&cv,2);}
    return 0;}
