#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>

#include "ps2icon.h"
#include "mcio.h"


static uint32_t TIM2RGBA(const uint8_t *buf)
{
	uint16_t lRGB = (buf[1] << 8) | buf[0];
	uint8_t r = ((lRGB >> 10) & 0x1F) << 3;
	uint8_t g = ((lRGB >>  5) & 0x1F) << 3;
	uint8_t b = ((lRGB >>  0) & 0x1F) << 3;

	/* ARGB8888 — alpha in high byte, matching draw_icon_rgba and ps2icon_decode */
	return ((uint32_t)0xFF << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static void* ps2IconTexture(const uint8_t* iData)
{
	int i;
	uint16_t j;
	Icon_Header header;
	Animation_Header anim_header;
	Frame_Data animation;
	uint32_t *lTexturePtr, *lRGBA;

	lTexturePtr = (uint32_t *) calloc(128 * 128, sizeof(uint32_t));

	//read header:
	memcpy(&header, iData, sizeof(Icon_Header));
	iData += sizeof(Icon_Header);

	//n_vertices has to be divisible by three, that's for sure:
	if(header.file_id != 0x010000 || header.n_vertices % 3 != 0)
		return lTexturePtr;

	//read icon data from file: https://ghulbus-inc.de/projects/ps2iconsys/
	///Vertex data
	// each vertex consists of animation_shapes tuples for vertex coordinates,
	// followed by one vertex coordinate tuple for normal coordinates
	// followed by one texture data tuple for texture coordinates and color
	for(i=0; i<header.n_vertices; i++) {
		iData += sizeof(Vertex_Coord) * header.animation_shapes;
		iData += sizeof(Vertex_Coord);
		iData += sizeof(Texture_Data);
	}

	//animation data
	// preceeded by an animation header, there is a frame data/key set for every frame:
	memcpy(&anim_header, iData, sizeof(Animation_Header));
	iData += sizeof(Animation_Header);

	//read animation data:
	for(i=0; i<anim_header.n_frames; i++) {
		memcpy(&animation, iData, sizeof(Frame_Data));
		iData += sizeof(Frame_Data);

		if(animation.n_keys > 0)
			iData += sizeof(Frame_Key) * animation.n_keys;
	}

	lRGBA = lTexturePtr;

	if (header.texture_type <= 7)
	{	// Uncompressed texture
		for (i = 0; i < (128 * 128); i++)
		{
			*lRGBA = TIM2RGBA(iData);
			lRGBA++;
			iData += 2;
		}
	}
	else
	{	//Compressed texture
		iData += 4;
		do
		{
			j = (int16_t) (iData[1] << 8) | iData[0];
			if (0xFF00 == (j & 0xFF00))
			{
				for (j = (0x0000 - j) & 0xFFFF; j > 0; j--)
				{
					iData += 2;
					*lRGBA = TIM2RGBA(iData);
					lRGBA++;
				}
			}
			else
			{
				iData += 2;
				for (; j > 0; j--)
				{
					*lRGBA = TIM2RGBA(iData);
					lRGBA++;
				}
			}
			iData += 2;
		} while ((lRGBA - lTexturePtr) < 0x4000);
	}

	return (lTexturePtr);
}

//Get icon data as bytes
uint8_t* getIconPS2(const char* folder, const char* iconfile)
{
	int fd;
	uint8_t *buf, *out;
	char filePath[256];
	struct io_dirent st;

	snprintf(filePath, sizeof(filePath), "%s/%s", folder, iconfile);

	if (mcio_mcStat(filePath, &st) < 0)
		return calloc(128 * 128, sizeof(uint32_t));

	fd = mcio_mcOpen(filePath, sceMcFileAttrReadable | sceMcFileAttrFile);
	if (fd < 0)
		return calloc(128 * 128, sizeof(uint32_t));

	buf = malloc(st.stat.size);
	mcio_mcRead(fd, buf, st.stat.size);
	mcio_mcClose(fd);

	/* Try 3D icon first (large files, > 1KB) */
	if (st.stat.size > 1024) {
		out = ps2IconTexture(buf);
	} else {
		out = NULL;
	}

	/* If 3D parse failed or file is small, try standard 16x16 icon */
	if (!out || ((uint32_t*)out)[0] == 0) {
		uint32_t *rgba = NULL;
		int w = 0, h = 0;
		if (ps2icon_decode(buf, st.stat.size, &rgba, &w, &h)) {
			free(out);  /* free failed 3D attempt */
			/* Upscale 16x16 to 128x128 for consistent rendering */
			out = calloc(128 * 128, sizeof(uint32_t));
			uint32_t *dst = (uint32_t *)out;
			for (int y = 0; y < 128; y++) {
				for (int x = 0; x < 128; x++) {
					int sx = x * w / 128;
					int sy = y * h / 128;
					dst[y * 128 + x] = rgba[sy * w + sx];
				}
			}
			free(rgba);
		} else if (!out) {
			out = calloc(128 * 128, sizeof(uint32_t));
		}
	}

	free(buf);
	return out;
}

/* ============================================================
   TITLE PARSER (S-JIS from icon.sys offset 0xC0)
   ============================================================ */

void ps2icon_parse_title(const uint8_t *icon_sys, size_t icon_sys_size,
                         char *out_title, size_t out_len)
{
    out_title[0] = '\0';
    if (!icon_sys || icon_sys_size < 0xC1) return;
    if (memcmp(icon_sys, "PS2D", 4) != 0) return;

    const uint8_t *src = icon_sys + 0xC0;
    int max = 68;
    if (icon_sys_size < 0xC0 + (size_t)max)
        max = (int)(icon_sys_size - 0xC0);

    int j = 0;
    for (int i = 0; i < max && j < (int)out_len - 1; i++) {
        uint8_t c = src[i];
        if (c == '\0') break;

        if (c < 0x80) {
            out_title[j++] = (char)c;
        } else if (i + 1 < max) {
            uint16_t sjis = ((uint16_t)c << 8) | src[i + 1];
            char ascii = '?';

            if (sjis >= 0x824F && sjis <= 0x8258)
                ascii = (char)('0' + (sjis - 0x824F));
            else if (sjis >= 0x8260 && sjis <= 0x8279)
                ascii = (char)('A' + (sjis - 0x8260));
            else if (sjis >= 0x8281 && sjis <= 0x829A)
                ascii = (char)('a' + (sjis - 0x8281));
            else if (sjis == 0x8140)
                ascii = ' ';
            else if (sjis == 0x8144)
                ascii = '.';
            else if (sjis == 0x8143)
                ascii = ',';
            else if (sjis == 0x8146)
                ascii = ':';
            else if (sjis == 0x8147)
                ascii = ';';
            else if (sjis == 0x814A)
                ascii = '/';
            else if (sjis == 0x815F)
                ascii = '-';
            else if (sjis == 0x814B)
                ascii = '~';
            else if (sjis == 0x8149)
                ascii = '!';
            else if (sjis == 0x8148)
                ascii = '?';
            else if (sjis == 0x8152)
                ascii = '(';
            else if (sjis == 0x8153)
                ascii = ')';
            else if (sjis == 0x815E)
                ascii = '+';
            else if (sjis == 0x8160)
                ascii = '=';
            else if (sjis == 0x8161)
                ascii = '<';
            else if (sjis == 0x8162)
                ascii = '>';
            else if (sjis == 0x8163)
                ascii = '\\';
            else if (sjis == 0x8164)
                ascii = '$';
            else if (sjis == 0x8165)
                ascii = '%';
            else if (sjis == 0x8166)
                ascii = '#';
            else if (sjis == 0x8167)
                ascii = '&';
            else if (sjis == 0x8168)
                ascii = '*';
            else if (sjis == 0x8169)
                ascii = '@';

            out_title[j++] = ascii;
            i++;
        }
    }
    out_title[j] = '\0';
}

/* ============================================================
   16x16 PALETTED ICON DECODER (fallback)
   ============================================================ */

int ps2icon_decode(const uint8_t *ico_data, size_t ico_size,
                   uint32_t **out_rgba, int *out_w, int *out_h)
{
    *out_rgba = NULL;
    *out_w = 0;
    *out_h = 0;

    if (!ico_data || ico_size < 160) return 0;

    const uint16_t *palette = (const uint16_t *)ico_data;
    const uint8_t  *bitmap  = ico_data + 32;

    int w = 16, h = 16;
    uint32_t *rgba = (uint32_t *)malloc(w * h * sizeof(uint32_t));
    if (!rgba) return 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            int byte_idx = idx / 2;
            int shift = (idx & 1) ? 0 : 4;
            int nibble = (bitmap[byte_idx] >> shift) & 0x0F;

            uint16_t p = palette[nibble];

            uint8_t r = ((p >> 10) & 0x1F) << 3;
            uint8_t g = ((p >>  5) & 0x1F) << 3;
            uint8_t b = ((p >>  0) & 0x1F) << 3;
            uint8_t a = (p & 0x8000) ? 0xFF : 0x00;

            r |= (r >> 5);
            g |= (g >> 5);
            b |= (b >> 5);

            rgba[idx] = ((uint32_t)a << 24) | ((uint32_t)r << 16) |
                        ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 1;
}
