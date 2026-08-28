#ifndef PS2ICON_H
#define PS2ICON_H

#include <stdint.h>
#include <stddef.h>

/* 3D icon structs */
typedef struct {
    uint32_t file_id;
    uint32_t animation_shapes;
    uint32_t texture_type;
    uint32_t reserved;
    uint32_t n_vertices;
} Icon_Header;

typedef struct {
    uint32_t id_tag;
    uint32_t frame_length;
    float    anim_speed;
    uint32_t play_offset;
    uint32_t n_frames;
} Animation_Header;

typedef struct {
    uint32_t shape_id;
    uint32_t n_keys;
} Frame_Data;

typedef struct {
    float time;
    float value;
} Frame_Key;

typedef struct {
    int16_t x, y, z, unknown;
} Vertex_Coord;

typedef struct {
    int16_t u, v;
    uint32_t color;
} Texture_Data;

/* Functions */
int ps2icon_decode(const uint8_t *ico_data, size_t ico_size,
                   uint32_t **out_rgba, int *out_w, int *out_h);
void ps2icon_parse_title(const uint8_t *icon_sys, size_t icon_sys_size,
                         char *out_title, size_t out_len);
uint8_t* getIconPS2(const char* folder, const char* iconfile);

#endif
