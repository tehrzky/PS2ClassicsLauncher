void draw_image_rgba(int x, int y, int w, int h, const unsigned char *rgba, int img_w, int img_h) {
    if (!rgba || img_w <= 0 || img_h <= 0) return;

    for (int dy = 0; dy < h; dy++) {
        int sy = dy * img_h / h;
        if (sy >= img_h) sy = img_h - 1;

        for (int dx = 0; dx < w; dx++) {
            int sx = dx * img_w / w;
            if (sx >= img_w) sx = img_w - 1;

            const unsigned char *p = rgba + (sy * img_w + sx) * 4;
            // stbi_load RGBA: p[0]=R p[1]=G p[2]=B p[3]=A
            // Framebuffer ARGB: A<<24 | R<<16 | G<<8 | B
            uint32_t color = ((uint32_t)p[3] << 24) |
                             ((uint32_t)p[0] << 16) |
                             ((uint32_t)p[1] << 8)  |
                             (uint32_t)p[2];
            draw_pixel(x + dx, y + dy, color);
        }
    }
}
