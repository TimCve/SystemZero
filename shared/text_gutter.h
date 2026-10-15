#ifndef SZ_TEXT_GUTTER_H
#define SZ_TEXT_GUTTER_H
#include <stdint.h>
static inline uint32_t text_source_line(const uint8_t *data,uint32_t position) {
    uint32_t line=1; for(uint32_t i=0;i<position;i++) if(data[i]=='\n') line++; return line;
}
static inline uint32_t text_gutter_width(const uint8_t *data,uint32_t size) {
    uint32_t lines=text_source_line(data,size),digits=1;
    while(lines>=10) { lines/=10; digits++; }
    return (digits<3?3:digits)+3;
}
#endif
