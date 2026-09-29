/* Original 32x32 GEM window / E / sliders icon. GPL-2.0-or-later.
 * VMU icons store the left pixel in the high nibble; palette is ARGB4444. */
static void icon_rect(uint8_t *icon, int x, int y, int w, int h, unsigned colour)
{
    for (int row=y; row<y+h; row++) {
        for (int col=x; col<x+w; col++) {
            unsigned offset=row*16+col/2, shift=(col&1) ? 0 : 4;
            icon[offset]=(icon[offset]&~(15u<<shift)) | (colour<<shift);
        }
    }
}
static void settings_icon(vmu_pkg_t *pkg, uint8_t icon[512])
{
    static const uint16_t palette[16]={0x0000,0xfeee,0xf222,0xf246,0xf088,0xffb4,0xf777,0xffff};
    static const unsigned char letter_e[7]={31,16,16,30,16,16,31};
    memset(icon,0,512);
    icon_rect(icon,4,4,27,27,6);       /* shadow */
    icon_rect(icon,2,2,27,27,2);       /* window outline */
    icon_rect(icon,3,3,25,25,1);
    icon_rect(icon,4,4,23,5,3);        /* title bar */
    icon_rect(icon,5,5,3,3,7);         /* close gadget */
    icon_rect(icon,10,6,15,1,7);
    icon_rect(icon,4,10,23,17,7);
    for (int row=0; row<7; row++)
        for (int col=0; col<5; col++)
            if (letter_e[row]&(16u>>col))icon_rect(icon,6+2*col,11+2*row,2,2,4);
    for (int row=0; row<3; row++) {
        icon_rect(icon,19,13+5*row,7,1,6);
        icon_rect(icon,20+(row==1 ? 4 : row),12+5*row,2,3,5);
    }
    memcpy(pkg->icon_pal,palette,sizeof(palette));
    pkg->icon_cnt=1;
    pkg->icon_data=icon;
}
