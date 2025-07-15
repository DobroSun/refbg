
#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>

#include "wayland/protocols/xdg-shell.h"
#include "wayland/protocols/xdg-decoration.h"
#include "wayland/protocols/tablet.h"
#include "wayland/protocols/cursor-shape.h"
#include "wayland/libdecor.h"

#include "gdb.h"
#include "types.h"
#include "print.h"
#include "temporary_storage.h"


#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define clamp(x, a, b) (min(max(x, a), b))
#define lerp(a, b, t) ((a) + ((b)-(a)) * (t))

//
// @TODO:
// !! load a file in the debugger, and load a file into the editor.
// !! allow scrolling the file in the editor and setting breakpoints (visually + gdb) on the side.
// !! refactor (draw scene) create virtual representation of all of these boxes, so that we can interact with them separately from update().
// !! implement simple UI system with dirty rectangles, and interactive boxes
//
// .. blend circles correctly
// .. remove old blend function.
// .. refactor draw_textured_box_internal and pass texture there instead of uint8_t* bitmap
// !! Once we have reasonable UI defaults we can start parsing GDB commands.
//
// !! hot realoding
//


typedef struct {
    float r, g, b, a;
} color_t;

typedef struct {
    s32 x, y, w, h;
} rectangle_t;

typedef struct {
    s32 x, y, w, h;
} Rect_s32;

typedef struct {
    f32 x, y, w, h;
} Rect_f32;

typedef struct {
    f32 x, y, r;
} Circle_f32;

typedef struct {
    s32 x, y, r;
} Circle_s32;

typedef struct buffer_t {
    uint32_t* data;
    int32_t width, height;
    struct wl_buffer* buffer;
} buffer_t;

static float ortho_projection_f1;
static float ortho_projection_f2;
static void update_orthographic_projection(int32_t width, int32_t height) {
    ortho_projection_f1 = width;
    ortho_projection_f2 = height;
}

static bool valid_orthographic_projection() {
    return ortho_projection_f1 != 0.0f && ortho_projection_f2 != 0.0f;
}

static void transform_screen_into_world(double* x, double* y) {
    *x =         *x / ortho_projection_f1;
    *y = 1.0f - (*y / ortho_projection_f2);
}

static void transform_world_into_screen(float* x, float* y) {
    *x = *x          * ortho_projection_f1;
    *y = (1.0f - *y) * ortho_projection_f2;
}

static void transform_world_into_screen_rect(float* x, float* y, float* w, float* h) {

    float origin_x = *x;
    float origin_y = *y;

    *x = clamp(*x, 0.0f, 1.0f);
    *y = clamp(*y, 0.0f, 1.0f);
    *w = clamp(*w, 0.0f, 1.0f);
    *h = clamp(*h, 0.0f, 1.0f);


    *x =       *x  * ortho_projection_f1;
    *y = (1.0f-*y) * ortho_projection_f2;
    *w =       *w  * ortho_projection_f1;
    *h =       *h  * ortho_projection_f2;

    if (origin_x == 1.0f) { // since we are writing into our buffer, we don't want to include the very last bit.
        *x -= 1;
    }

    if (origin_y == 0.0f) {
        *y -= 1;
    }
}

static void transform_world_into_screen_circle(float* x, float* y, float* r) {

    float origin_x = *x;
    float origin_y = *y;

    *x = clamp(*x, 0.0f, 1.0f);
    *y = clamp(*y, 0.0f, 1.0f);
    *r = clamp(*r, 0.0f, 1.0f);

    *x =       *x  * ortho_projection_f1;
    *y = (1.0f-*y) * ortho_projection_f2;
    *r =       *r  * min(ortho_projection_f1, ortho_projection_f2);

    if (origin_x == 1.0f) { // since we are writing into our buffer, we don't want to include the very last bit.
        *x -= 1;
    }

    if (origin_y == 0.0f) {
        *y -= 1;
    }
}

static void transform_screen_into_world_disk(float* x, float* y, float* r0, float* r1) {

    f32 origin_x = *x;
    f32 origin_y = *y;

    *x  = clamp(*x,  0.0f, 1.0f);
    *y  = clamp(*y,  0.0f, 1.0f);
    *r0 = clamp(*r0, 0.0f, 1.0f);
    *r1 = clamp(*r1, 0.0f, 1.0f);

    *x  =       *x  * ortho_projection_f1;
    *y  = (1.0f-*y) * ortho_projection_f2;
    *r0 =      *r0  * min(ortho_projection_f1, ortho_projection_f2);
    *r1 =      *r1  * min(ortho_projection_f1, ortho_projection_f2);

    if (origin_x == 1.0f) { // since we are writing into our buffer, we don't want to include the very last bit.
        *x -= 1.0f;
    }

    if (origin_y == 0.0f) {
        *y -= 1.0f;
    }
}


static uint32_t make_u32_from_color(color_t color) {
    uint32_t aa = (uint8_t) (color.a * 255.0f);
    uint32_t rr = (uint8_t) (color.r * 255.0f);
    uint32_t gg = (uint8_t) (color.g * 255.0f);
    uint32_t bb = (uint8_t) (color.b * 255.0f);

    return (aa << 24) | (rr << 16) | (gg << 8) | (bb << 0);
}


static uint32_t make_u32_from_u8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint32_t aa = a;
    uint32_t rr = r;
    uint32_t gg = g;
    uint32_t bb = b;

    return (aa << 24) | (rr << 16) | (gg << 8) | (bb << 0);
}


static void memset4(void* data, uint32_t value, uint32_t size) {
    uint32_t* ptr = data;
    for (uint32_t i = 0; i < size; i++) {
        ptr[i] = value;
    }
}

static void set_pixel(buffer_t* b, uint32_t x, uint32_t y, uint32_t c) {
    b->data[y * b->width + x] = c;
}

static u32 blend_color_new(u32 background, u32 foreground, float alpha) {

    float foreground_r;
    float foreground_g;
    float foreground_b;
    {
        u32 color = foreground;
        uint8_t aa = (color >> 24) & 0xFF;
        uint8_t rr = (color >> 16) & 0xFF;
        uint8_t gg = (color >> 8)  & 0xFF;
        uint8_t bb = (color >> 0)  & 0xFF;

        foreground_r = rr / 255.0f;
        foreground_g = gg / 255.0f;
        foreground_b = bb / 255.0f;
    }

    float background_r;
    float background_g;
    float background_b;
    {
        u32 color = background;
        uint8_t aa = (color >> 24) & 0xFF;
        uint8_t rr = (color >> 16) & 0xFF;
        uint8_t gg = (color >> 8)  & 0xFF;
        uint8_t bb = (color >> 0)  & 0xFF;

        background_r = rr / 255.0f;
        background_g = gg / 255.0f;
        background_b = bb / 255.0f;
    }

    color_t c = {
        .r = background_r * (1.0f - alpha) + foreground_r * alpha,
        .g = background_g * (1.0f - alpha) + foreground_g * alpha,
        .b = background_b * (1.0f - alpha) + foreground_b * alpha,
        .a = 1.0f,
    };

    return make_u32_from_color(c);
}

static u32 blend_color_with_background(u32 color, float intensity) {
    // @Incomplete: add background color to lerp against.

    uint8_t aa = (color >> 24) & 0xFF;
    uint8_t rr = (color >> 16) & 0xFF;
    uint8_t gg = (color >> 8)  & 0xFF;
    uint8_t bb = (color >> 0)  & 0xFF;

    float a = aa / 255.0f;
    float r = rr / 255.0f;
    float g = gg / 255.0f;
    float b = bb / 255.0f;

    color_t c = {
        .r = r * intensity,
        .g = g * intensity,
        .b = b * intensity,
        .a = 1.0f,
    };

    return make_u32_from_color(c);
}


typedef enum {
    COMMAND_TYPE_NONE = 0,
    COMMAND_TYPE_DRAW_EVERYTHING,
    COMMAND_TYPE_DRAW_BOX,
    COMMAND_TYPE_DRAW_CIRCLE,
    COMMAND_TYPE_DRAW_DISK,
} command_type_t;

typedef struct {
    command_type_t type;
    union {
        struct {
            float x, y, w, h;
            u32 color;
        } box;

        struct {
            float x, y;
            float r;
            u32 color;
        } circle;

        struct {
            float x, y;
            float r0, r1;
            u32 color;
        } disk;
    };

} command_t;

enum {
    MAX_RENDERING_COMMANDS = 32,
};

typedef struct {

    // @Incomplete: if we are not rendering fast enough, we might get more stuff into commands array than we expect and crash on an assert...

    command_t commands[MAX_RENDERING_COMMANDS];
    int32_t length;

} command_buffer_t;

static const buffer_t empty = {};


typedef struct {
    float x, y, w, h;

    // @Incomplete: color here?

    bool hovered; // @Incomplete: move this into general ui context.

    // bool hover_just_activated;   // dirty flag.
    // bool hover_just_deactivated; // dirty flag.

} button_t;

typedef struct {
    struct wl_compositor* compositor;
    struct wl_seat* seat;
    struct wl_shm* shm;

    struct wl_pointer* pointer;
    struct wl_touch* touch;

    struct xdg_wm_base* xdg_wm_base;
    struct wl_output* output;

    struct wl_surface* surface;
    struct xdg_surface* xdg_surface;
    struct xdg_toplevel* xdg_toplevel;

    struct wl_surface* current_surface;

    buffer_t buffer;
    buffer_t used_by_compositor;
    buffer_t request_destruction;

    command_buffer_t command_buffer;

    button_t button;

    struct wp_cursor_shape_manager_v1* cursor_shape_manager;
    struct wp_cursor_shape_device_v1* cursor_shape_device;

    struct zxdg_decoration_manager_v1* xdg_decoration_manager;

#ifdef HAS_LIBDECOR
    struct libdecor* context;
    struct libdecor_frame* frame;
    enum libdecor_window_state window_state;
#endif

    u32 serial;

    u32 display_width;
    u32 display_height;

    u32 width;
    u32 height;

    u32 cursor_x;
    u32 cursor_y;

    bool request_quit;
} client_state_t;


static bool is_our_surface(client_state_t* state) {
    return state->surface == state->current_surface;
}

static bool is_libdecor_surface(client_state_t* state) {
    return state->surface != state->current_surface; // @Note: assuming we only have two surfaces per application.
}

static bool buffer_is_ready_for_drawing(client_state_t* state) {
    return state->buffer.buffer != NULL && state->used_by_compositor.buffer == NULL;
}

static void check_buffer_is_not_used_by_the_compositor(client_state_t* state) {
    assert(state->buffer.buffer             != NULL);
    assert(state->used_by_compositor.buffer == NULL);
}

static void draw_textured_box_internal(buffer_t* buffer, uint8_t* bitmap, f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 v0, f32 u1, f32 v1, u32 c) {
    // @Incomplete: path texture width/height.
    //

    if (x0 == x1 && y0 == y1) {
        return;
    }

    f32 w = x1 - x0;
    f32 h = y1 - y0;

    assert(w >= 0.0f);
    assert(h >= 0.0f);

    for (s32 y = (s32) y0; y < y1; y++) {
        for (s32 x = (s32) x0; x <= x1; x++) {

            f32 u = u0 + (x-x0) * (u1 - u0) / w;
            f32 v = v0 + (y-y0) * (v1 - v0) / h;

            s32 index = (s32) (u*512.f + 512.f * v * 512.f);
            u8 pixel = bitmap[index];

            uint32_t background = buffer->data[y * buffer->width + x];

            float alpha = pixel / 255.0f;
            u32 color = blend_color_new(background, c, alpha);

            set_pixel(buffer, (s32) x, (s32) y, color);
        }
    }
}

static void draw_box_internal(buffer_t* buffer, s32 x, s32 y, s32 w, s32 h, u32 c) {
    //
    // @Incomplete: check_buffer_is_not_used_by_the_compositor. Same for all other draw_* functions.
    //

    s32 orig_y = y;
    for (y = orig_y; y >= orig_y-h; y--) {
        memset4(buffer->data + (y * buffer->width + x), c, w);
    }
}

static void draw_box(buffer_t* buffer, float x, float y, float w, float h, u32 c, rectangle_t* bounding) {

    transform_world_into_screen_rect(&x, &y, &w, &h);
    s32 x0 = (s32) x;
    s32 y0 = (s32) y;
    s32 w0 = (s32) w;
    s32 h0 = (s32) h;

    if (bounding) {
        *bounding = (rectangle_t) {
            .x = x0,
            .y = y0 - h0,
            .w = w0,
            .h = h0,
        };
    }

    draw_box_internal(buffer, x0, y0, w0, h0, c);
}

static void draw_circle_internal(buffer_t* buffer, s32 x0, s32 y0, s32 r, u32 c) {
    //
    // @Incomplete: Implement Wu's circle algorithm instead of this crap.
    //

    float r_inner = r*r - r; // Approximation of (r - 0.5)^2 = r*r - 2*r + 0.25 = r*r - r
    float r_outer = r*r + r; // Approximation of (r + 0.5)^2 = r*r + 2*r + 0.25 = r*r + r
    for (s32 y = y0-r; y <= y0+r; y++) {
        float sqy = (y-y0) * (y-y0);

        for (s32 x = x0-r; x <= x0+r; x++) {
            float distance = (x-x0) * (x-x0) + sqy;

            float intensity = (r_outer-distance+0.5f) / (2.0f*r); // +0.5f to round up.
            u32 i0_ = blend_color_with_background(c, intensity);

            bool inner = distance <= r_inner;
            bool outer = distance > r_inner && distance < r_outer;
            if (inner) {
                set_pixel(buffer, x, y, c);
                continue;
            }

            if (outer) {
                set_pixel(buffer, x, y, i0_);
                continue;
            }
        }
    }
}

static void draw_circle(buffer_t* buffer, float x, float y, float r, u32 c, rectangle_t* bounding) {

    transform_world_into_screen_circle(&x, &y, &r);
    s32 x0 = (s32) x;
    s32 y0 = (s32) y;
    s32 r0 = (s32) r;

    if (bounding) {
        *bounding = (rectangle_t) {
            .x = x0 - r0,
            .y = y0 - r0,
            .w = r0 * 2,
            .h = r0 * 2,
        };
    }

    draw_circle_internal(buffer, x0, y0, r0, c);
}

static void draw_disk_internal(buffer_t* buffer, s32 x0, s32 y0, s32 r0, s32 r1, u32 c) {
    assert(r0 < r1 && "Inner radius should be always less than outer radius");

    float r0_inner = r0*r0 - r0;
    float r0_outer = r0*r0 + r0;

    float r1_inner = r1*r1 - r1;
    float r1_outer = r1*r1 + r1;

    for (s32 y = y0-r1; y <= y0+r1; y++) {
        s32 sqy = (y-y0) * (y-y0);

        for (s32 x = x0-r1; x <= x0+r1; x++) {
            s32 distance = (x-x0) * (x-x0) + sqy;
            float i1 = (r1_outer-distance+0.5f) / (2.0f*r1);
            float i0 = (distance-r0_inner-0.5f) / (2.0f*r0);

            u32 i0_ = blend_color_with_background(c, i0);
            u32 i1_ = blend_color_with_background(c, i1);

            bool inner_i0 = distance <= r0_inner;
            bool outer_i0 = distance > r0_inner && distance < r0_outer;
            bool inner_i1 = distance >= r0_outer && distance <= r1_inner;
            bool outer_i1 = distance > r1_inner && distance < r1_outer;

            if (inner_i0) {
                continue;
            }

            if (outer_i0) {
                set_pixel(buffer, x, y, i0_);
                continue;
            }

            if (inner_i1) {
                set_pixel(buffer, x, y, c);
                continue;
            }

            if (outer_i1) {
                set_pixel(buffer, x, y, i1_);
                continue;
            }
        }
    }
}

static void draw_disk(buffer_t* buffer, float x, float y, float ri, float ro, u32 c, rectangle_t* bounding) {

    transform_screen_into_world_disk(&x, &y, &ri, &ro);

    s32 x0 = (s32) x;
    s32 y0 = (s32) y;
    s32 r0 = (s32) ri;
    s32 r1 = (s32) ro;

    if (bounding) {
        *bounding = (rectangle_t) {
            .x = x0 - r1,
            .y = y0 - r1,
            .w = r1 * 2,
            .h = r1 * 2,
        };
    }

    draw_disk_internal(buffer, x0, y0, r0, r1, c);
}

static void draw_text(buffer_t* buffer, float x, float y, const char* text, u32 color) {

    static unsigned char ttf_buffer[1<<20];
    static unsigned char temp_bitmap[512*512];
    static stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs
    static bool first_time = true;

    uint32_t stride = sizeof(uint8_t) * 512;

    if (first_time) {

        // @Incomplete: abstract this away.

        first_time = false;
        fread(ttf_buffer, 1, 1<<20, fopen("/usr/share/fonts/rsms-inter-fonts/Inter-Regular.ttf", "rb"));
        stbtt_BakeFontBitmap(ttf_buffer, 0, 32.0, temp_bitmap, 512, 512, 32, 96, cdata); // no guarantee this fits!

        // stbi_write_png("example.png", 512, 128, 1, temp_bitmap, sizeof(uint8_t) * 512);
    }

    transform_world_into_screen(&x, &y);

    while (*text) {
        if (*text >= 32) { // @Incomplete: check that we can actually draw this symbol, if not, draw something default...

            // @Incomplete: don't use baked quad, please. Although, we only need Engrish, so we might not need any actual packing algorithms.
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(cdata, 512, 512, *text-32, &x, &y, &q, 1); // 1 for opengl and 0 for d3d9, d3d10+

            f32 x0 = q.x0;
            f32 y0 = q.y0;
            f32 x1 = q.x1;
            f32 y1 = q.y1;

            f32 u0 = q.s0;
            f32 v0 = q.t0;
            f32 u1 = q.s1;
            f32 v1 = q.t1;

            draw_textured_box_internal(buffer, temp_bitmap, x0, y0, x1, y1, u0, v0, u1, v1, color);
        }
        text += 1;
    }
}

static void execute_command_buffer(client_state_t* state) {
    check_buffer_is_not_used_by_the_compositor(state);

    if (state->command_buffer.length == 0) {
        // Nothing to do...
        return;
    }

    wl_surface_attach(state->surface, state->buffer.buffer, 0, 0);

    for (int i = 0; i < state->command_buffer.length; i++) {
        command_t command = state->command_buffer.commands[i];

        rectangle_t invalidate_region = {};

        if (command.type == COMMAND_TYPE_DRAW_EVERYTHING) {

            // brown editor: #3f3f3f
            // brown highlight line editor: #4f4f4f
            // black windows: #111111
            // black highlight line windows: #1f1f1f
            // yellow status bar: #774f00
            //

            // draw scene

            { // background.
                // @Note: this is terribly inefficient... z buffer? @Note: although, we are invalidating the entire framebuffer, so we need to redraw it fully too.
                f32 x = 0.0f;
                f32 y = 0.0f;
                f32 w = 0.999f; // @Incomplete: handle 1.0f.
                f32 h = 0.999f; // @Incomplete: handle 1.0f.

                draw_box(&state->buffer, x, y, w, h, 0xff774f00, NULL);
            }

            { // editor window.
                f32 x = 0.00f;
                f32 y = 0.25f;
                f32 w = 0.45f;
                f32 h = 0.7f;

                draw_box(&state->buffer, x, y, w, h, 0xff3f3f3f, NULL);
            }

            { // disassembly window.
                f32 x = 0.0f;
                f32 y = 0.0f;
                f32 w = 0.45f;
                f32 h = 0.24f;

                draw_box(&state->buffer, x, y, w, h, 0xff3f3f3f, NULL);
            }

            { // watch window.
                f32 x = 0.46f;
                f32 y = 0.4f;
                f32 w = 0.53f;
                f32 h = 0.55f;

                draw_box(&state->buffer, x, y, w, h, 0xff111111, NULL);
            }

            { // call stack window.
                f32 x = 0.46f;
                f32 y = 0.0f;
                f32 w = 0.53f;
                f32 h = 0.39f;

                draw_box(&state->buffer, x, y, w, h, 0xff111111, NULL);
            }

            { // breakpoint on an editor window :)
                f32 x = 0.01f;
                f32 y = 0.4f;
                f32 r = 0.01f;

                draw_circle(&state->buffer, x, y, r, 0xffdb0f10, NULL);
            }

            f32 bar_x = 0.46f;
            f32 bar_y = 0.918f;
            { // watch window box
                {
                    f32 w = 0.075f;
                    f32 h = 0.03f;

                    draw_box(&state->buffer, bar_x, bar_y, w, h, 0xff22436b, NULL);
                }

                {
                    float x = bar_x + 0.005f;
                    float y = bar_y + 0.004f;
                    const char* text = "Watch";
                    draw_text(&state->buffer, x, y, text, 0xffffffff); // @Incomplete: query text size first and draw bounding box after it.
                }
            }

            bar_x += 0.078f;

            { // registers window box
                {
                    f32 w = 0.1f;
                    f32 h = 0.03f;

                    draw_box(&state->buffer, bar_x, bar_y, w, h, 0xff22436b, NULL);
                }

                {
                    float x = bar_x + 0.005f;
                    float y = bar_y + 0.004f;
                    const char* text = "Registers";
                    draw_text(&state->buffer, x, y, text, 0xffffffff); // @Incomplete: query text size first and draw bounding box after it.
                }
            }

            bar_x = 0.46f;
            bar_y = 0.358f;
            { // call stack window box
                {
                    f32 w = 0.11f;
                    f32 h = 0.03f;

                    draw_box(&state->buffer, bar_x, bar_y, w, h, 0xff22436b, NULL);
                }

                {
                    float x = bar_x + 0.005f;
                    float y = bar_y + 0.004f;
                    const char* text = "Call Stack";
                    draw_text(&state->buffer, x, y, text, 0xffffffff); // @Incomplete: query text size first and draw bounding box after it.
                }
            }

            bar_x += 0.115f;

            { // call stack window box
                {
                    f32 w = 0.12f;
                    f32 h = 0.03f;

                    draw_box(&state->buffer, bar_x, bar_y, w, h, 0xff22436b, NULL);
                }

                {
                    float x = bar_x + 0.005f;
                    float y = bar_y + 0.004f;
                    const char* text = "Breakpoints";
                    draw_text(&state->buffer, x, y, text, 0xffffffff); // @Incomplete: query text size first and draw bounding box after it.
                }
            }


            invalidate_region = (rectangle_t) {
                .x = 0,
                .y = 0,
                .w = state->buffer.width,
                .h = state->buffer.height,
            };

        } else if (command.type == COMMAND_TYPE_DRAW_BOX) {
            draw_box(&state->buffer, command.box.x, command.box.y, command.box.w, command.box.h, command.box.color, &invalidate_region);

        } else if (command.type == COMMAND_TYPE_DRAW_CIRCLE) {
            draw_circle(&state->buffer, command.circle.x, command.circle.y, command.circle.r, command.circle.color, &invalidate_region);

        } else if (command.type == COMMAND_TYPE_DRAW_DISK) {
            draw_disk(&state->buffer, command.disk.x, command.disk.y, command.disk.r0, command.disk.r1, command.disk.color, &invalidate_region);

        } else {
            assert(0);
        }

        s32 x = invalidate_region.x;
        s32 y = invalidate_region.y;
        s32 w = invalidate_region.w;
        s32 h = invalidate_region.h;

        wl_surface_damage_buffer(state->surface, x, y, w, h);
    }

    state->command_buffer.length = 0;
    memset(state->command_buffer.commands, 0, sizeof(state->command_buffer.commands));

    wl_surface_commit(state->surface);

    state->used_by_compositor = state->buffer;
    state->buffer             = empty;
}

static void try_execute_command_buffer(client_state_t* state) {
    if (buffer_is_ready_for_drawing(state)) {
        execute_command_buffer(state);
    }
}

static void add_command(command_buffer_t* buffer, command_t cmd) {
    if (buffer->length >= MAX_RENDERING_COMMANDS) {
        assert(0 && "Command buffer doesn't handle more than MAX_RENDERING_COMMANDS commands");
    }

    buffer->commands[buffer->length++] = cmd;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
    client_state_t* state = data;

    //
    // @Note: We can only reuse the shared buffer when it's no longer used by the compositor.
    //

    if (state->used_by_compositor.buffer == wl_buffer) {
        state->buffer = state->used_by_compositor;
        state->used_by_compositor = empty;

        execute_command_buffer(state);
    }

    if (state->request_destruction.buffer == wl_buffer) {
        wl_buffer_destroy(wl_buffer);

        int size = state->request_destruction.width * sizeof(uint32_t) * state->request_destruction.height;

        munmap(state->request_destruction.data, size);
        state->request_destruction = empty;
    }
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

static int allocate_shared_file(size_t size) {
    int fd = memfd_create("my-shell-shared", MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);

    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void reallocate_framebuffer(client_state_t* state, int width, int height) {

    if (state->buffer.buffer) {
        // Buffer is ours, can destruct inplace.

        int stride = state->buffer.width * sizeof(uint32_t);
        int size = stride * state->buffer.height;

        wl_buffer_destroy(state->buffer.buffer);
        munmap(state->buffer.data, size);
    } else {
        assert(state->used_by_compositor.buffer);
        state->request_destruction = state->used_by_compositor;
        state->buffer = empty;
    }

    state->width  = width;
    state->height = height;

    int stride = width * sizeof(uint32_t);
    int size = stride * height;

    int fd = allocate_shared_file(size);

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    auto buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    state->buffer.buffer      = buffer;
    state->used_by_compositor = empty;

    wl_buffer_add_listener(buffer, &buffer_listener, state);

    state->buffer = (buffer_t) {
        .buffer = buffer,
        .data   = data,
        .width  = width,
        .height = height,
    };
}

static buffer_t allocate_framebuffer(client_state_t *state) {
    int width  = state->width;
    int height = state->height;
    int stride = width * sizeof(uint32_t);
    int size = stride * height;

    int fd = allocate_shared_file(size);

    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    auto buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    state->used_by_compositor = empty;
    wl_shm_pool_destroy(pool);
    close(fd);

    // munmap(data, size);
    wl_buffer_add_listener(buffer, &buffer_listener, state);

    buffer_t result = {
        .buffer = buffer,
        .data   = data,
        .width  = width,
        .height = height,
    };
    return result;
}

void output_geometry(void *data, struct wl_output *wl_output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height, int32_t subpixel, const char *make, const char *model, int32_t transform) {
}

void output_mode(void *data, struct wl_output *wl_output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    client_state_t* state = data;

    // fprintf(stderr, "Display := (%d, %d)\n", width, height);

    state->display_width  = width;
    state->display_height = height;
}

void output_done(void *data, struct wl_output *wl_output) {
}

void output_scale(void *data, struct wl_output *wl_output, int32_t factor) {
}

void output_name(void *data, struct wl_output *wl_output, const char *name) {
}

void output_description(void *data, struct wl_output *wl_output, const char *description) {
}



static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

void pointer_enter(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    client_state_t* state = data;

    state->current_surface = surface;
    state->serial = serial;

    wp_cursor_shape_device_v1_set_shape(state->cursor_shape_device, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
}

void pointer_leave(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface) {
    client_state_t* state = data;

    state->current_surface = NULL;
}

void pointer_axis(void* data, struct wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {}


static enum xdg_toplevel_resize_edge find_interactive_edge(int width, int height, int x, int y) {
    if (y == 0)  return XDG_TOPLEVEL_RESIZE_EDGE_NONE;

    int margin = 15;
    int dm = 10;

    // @Note: Adding some pixels for easier controls at corners...
    margin += dm;

    bool top, bottom, left, right;
	top    = y < margin;
	bottom = y > (height - margin);
	left   = x < margin;
	right  = x > (width - margin);

    if (top && left) {
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    }

    if (top && right) {
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    }

    if (bottom && left) {
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    }

    if (bottom && right) {
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    }

    margin -= dm;
	top    = y < margin;
	bottom = y > (height - margin);
	left   = x < margin;
	right  = x > (width - margin);

    if (top) {
        return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    }

    if (bottom) {
        return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    }

    if (left) {
        return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    }

    if (right) {
        return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    }

    return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
}

#ifdef HAS_LIBDECOR
static enum libdecor_resize_edge xdg_edge_to_edge(enum xdg_toplevel_resize_edge edge)
{
	switch (edge) {
	case XDG_TOPLEVEL_RESIZE_EDGE_NONE:         return LIBDECOR_RESIZE_EDGE_NONE;
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP:          return LIBDECOR_RESIZE_EDGE_TOP;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:       return LIBDECOR_RESIZE_EDGE_BOTTOM;
	case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:         return LIBDECOR_RESIZE_EDGE_LEFT;
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:     return LIBDECOR_RESIZE_EDGE_TOP_LEFT;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:  return LIBDECOR_RESIZE_EDGE_BOTTOM_LEFT;
	case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:        return LIBDECOR_RESIZE_EDGE_RIGHT;
	case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:    return LIBDECOR_RESIZE_EDGE_TOP_RIGHT;
	case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT: return LIBDECOR_RESIZE_EDGE_BOTTOM_RIGHT;
    default: assert(0); break;
	}
    return LIBDECOR_RESIZE_EDGE_NONE;
}
#endif

static enum wp_cursor_shape_device_v1_shape edge_to_shape(enum xdg_toplevel_resize_edge edge) {
    switch (edge) {
        case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT:     return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE;
        case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT:    return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE;
        case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT:  return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE;
        case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT: return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE;
        case XDG_TOPLEVEL_RESIZE_EDGE_TOP:          return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE;
        case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM:       return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE;
        case XDG_TOPLEVEL_RESIZE_EDGE_LEFT:         return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE;
        case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT:        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE;
        default:                                    return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
    }
}

void pointer_motion(void* data, struct wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    client_state_t* state = data;


    if (state->cursor_shape_device && !using_libdecor()) {
        // @Incompete: this is dumb, rewrite these in floats.
        int32_t x = wl_fixed_to_int(surface_x);
        int32_t y = wl_fixed_to_int(surface_y);

        u32 width  = state->width;
        u32 height = state->height;

        auto edge = find_interactive_edge(width, height, x, y);
        auto shape = edge_to_shape(edge);
        wp_cursor_shape_device_v1_set_shape(state->cursor_shape_device, state->serial, shape);
    }

    state->cursor_x = wl_fixed_to_int(surface_x);
    state->cursor_y = wl_fixed_to_int(surface_y);

    if (is_libdecor_surface(state)) {
        return;
    }

    float rect_x = state->button.x;
    float rect_y = state->button.y;
    float rect_w = state->button.w;
    float rect_h = state->button.h;

    double x = wl_fixed_to_double(surface_x);
    double y = wl_fixed_to_double(surface_y);
    transform_screen_into_world(&x, &y);

    bool in_x = x > rect_x && x < rect_x + rect_w;
    bool in_y = y > rect_y && y < rect_y + rect_h;
    bool in_box = in_x && in_y;

    // fprintf(stderr, "Pointer: %d, %d\n", x, y);
    // fprintf(stderr, "Pointer: %f, %f\n", x, y);


    if (in_box) {
        if (state->button.hovered == false) {
            state->button.hovered = true;

            add_command(&state->command_buffer, (command_t) {
                .type = COMMAND_TYPE_DRAW_BOX,
                .box  = {
                    .x = rect_x,
                    .y = rect_y,
                    .w = rect_w,
                    .h = rect_h,
                    .color  = make_u32_from_color((color_t){ .r = 0.0f, .g = 1.0f, .b = 0.0f, .a = 1.0f }),
                },
            });
        }

    } else {
        if (state->button.hovered == true) {
            state->button.hovered = false;

            add_command(&state->command_buffer, (command_t) {
                .type  = COMMAND_TYPE_DRAW_BOX,
                .box   = {
                    .x = rect_x,
                    .y = rect_y,
                    .w = rect_w,
                    .h = rect_h,
                    .color = make_u32_from_color((color_t){ .r = 1.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f }),
                },
            });
        }
    }

#if 0
    add_command(&state->command_buffer, (command_t) {
        .type = COMMAND_TYPE_DRAW_CIRCLE,
        .circle = {
            .x = 0.5f,
            .y = 0.5f,
            .r = 0.3f,
            .color  = make_u32_from_color((color_t){ .r = 0.0f, .g = 0.0f, .b = 1.0f, .a = 1.0f }),
        },
    });
    try_execute_command_buffer(data);

    add_command(&state->command_buffer, (command_t) {
        .type = COMMAND_TYPE_DRAW_DISK,
        .disk = {
            .x = 0.1f,
            .y = 0.8f,
            .r0 = 0.05f,
            .r1 = 0.1f,
            .color  = make_u32_from_color((color_t){ .r = 0.0f, .g = 0.0f, .b = 1.0f, .a = 1.0f }),
        },
    });
#endif
    try_execute_command_buffer(data);
}

void pointer_button(void* data, struct wl_pointer* wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    client_state_t* client = data;

    if (is_libdecor_surface(client)) {
        return;
    }

    bool pressed  = state == WL_POINTER_BUTTON_STATE_PRESSED;
    bool released = state == WL_POINTER_BUTTON_STATE_RELEASED;

    // printf("Cursor (%u, %u)\n", client->cursor_x, client->cursor_y);

    if (!using_libdecor() && button == BTN_LEFT) {

        u32 x = client->cursor_x;
        u32 y = client->cursor_y;
        u32 width  = client->width;
        u32 height = client->height;

        auto edge = find_interactive_edge(width, height, x, y);
        if (edge != XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
            if (client->xdg_toplevel) {
                xdg_toplevel_resize(client->xdg_toplevel, client->seat, serial, edge);
            }
        }
    }

#if 0
    if (pressed) {
        if (button == BTN_LEFT) {
            puts("RIGHT MOUSE CLICKED!");
        } else if (button == BTN_RIGHT) {
            puts("RIGHT MOUSE CLICKED!");
        } else if (button == BTN_MIDDLE) {
            puts("MIDDLE MOUSE CLICKED!");
        }
    }
#endif
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

void touch_down(void *data, struct wl_touch *wl_touch, uint32_t serial, uint32_t time, struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
}

void touch_up(void *data, struct wl_touch *wl_touch, uint32_t serial, uint32_t time, int32_t id) {
}

void touch_motion(void *data, struct wl_touch *wl_touch, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y) {
}

void touch_frame(void *data, struct wl_touch *wl_touch) {
}

void touch_cancel(void *data, struct wl_touch *wl_touch) {
}

static const struct wl_touch_listener touch_listener = {
    .down = touch_down,
    .up = touch_up,
    .motion = touch_motion,
    .frame = touch_frame,
    .cancel = touch_cancel,
};


void seat_capabilities(void *data, struct wl_seat *wl_seat, uint32_t capabilities) {
    client_state_t* state = data;

    // wl_seat_capability   bitmask
    // WL_SEAT_CAPABILITY_POINTER  = 1,
	// WL_SEAT_CAPABILITY_KEYBOARD = 2,
	// WL_SEAT_CAPABILITY_TOUCH    = 4,

    if (capabilities & WL_SEAT_CAPABILITY_POINTER) { // pointer is gained
        state->pointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(state->pointer, &pointer_listener, data);

        state->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(state->cursor_shape_manager, state->pointer);

    } else { // pointer is lost.
        if (state->pointer) {
            wl_pointer_release(state->pointer);
            state->pointer = NULL;

        }

        if (state->cursor_shape_device) {
            wp_cursor_shape_device_v1_destroy(state->cursor_shape_device);
            state->cursor_shape_device = NULL;
        }
    }

#if 0
    if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
        state->touch = wl_seat_get_touch(wl_seat);
        wl_touch_add_listener(state->touch, &touch_listener, data);
    } else {
        if (state->touch) {
            wl_touch_release(state->touch);
            state->touch = NULL;
        }
    }
#endif
}

void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void surface_enter(void* data, struct wl_surface* surface, struct wl_output* output) {
}

static void surface_leave(void* data, struct wl_surface* surface, struct wl_output *output) {
}

static void surface_preferred_buffer_scale(void* data, struct wl_surface* wl_surface, int32_t factor) {
}

static void surface_preferred_buffer_transform(void* data, struct wl_surface* wl_surface, uint32_t transform) {
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_enter,
    .leave = surface_leave,
    .preferred_buffer_scale = surface_preferred_buffer_scale,
    .preferred_buffer_transform = surface_preferred_buffer_transform,
};

void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

void registry_global(void* data, struct wl_registry* wl_registry, uint32_t name, const char* interface, uint32_t version) {
    client_state_t* state = data;

    // TODO: figure out minimal versions of things that I want to use.

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, 6);
        state->surface    = wl_compositor_create_surface(state->compositor);
        wl_surface_add_listener(state->surface, &surface_listener, data);
        wl_surface_set_user_data(state->surface, data);

    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = wl_registry_bind(wl_registry, name, &wl_seat_interface, 4); // `4` because of all of the annoying pointer events that we don't need.
        wl_seat_add_listener(state->seat, &seat_listener, data);

    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(wl_registry, name, &wl_shm_interface, 1);
        // wl_shm_add_listener(state->seat, &seat_listener, data); // @Incomplete: listen to supported formats.

    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(wl_registry, name, &xdg_wm_base_interface, 6);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, data);

    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        state->output = wl_registry_bind(wl_registry, name, &wl_output_interface, 1);
        wl_output_add_listener(state->output, &output_listener, data);

    } else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
        state->cursor_shape_manager = wl_registry_bind(wl_registry, name, &wp_cursor_shape_manager_v1_interface, 1);

    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        state->xdg_decoration_manager = wl_registry_bind(wl_registry, name, &zxdg_decoration_manager_v1_interface, 1);
    }
}

void registry_global_remove(void* data, struct wl_registry* wl_registry, uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial) {
    client_state_t* state = data;

    xdg_surface_ack_configure(xdg_surface, serial);
#if 0
    if (state->request_resize) {
        state->ready_to_resize = true;
    }
#endif
}

static struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
    client_state_t* state = data;

    if (width != 0 && height != 0) {
        update_orthographic_projection(width, height);


        // @Incomplete: @SluggishResize: cap resizing at 60fps, some gaming mice can send thounsands of requests per second and that is going to kill the performance here.
        reallocate_framebuffer(state, width, height);

        add_command(&state->command_buffer, (command_t) {
            .type = COMMAND_TYPE_DRAW_EVERYTHING,
        });
        try_execute_command_buffer(state);
    }
}

void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    client_state_t* state = data;

    state->request_quit = true;
}

void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height) {
}

void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *xdg_toplevel, struct wl_array *capabilities) {
}


static struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
    .configure_bounds = xdg_toplevel_configure_bounds,
    .wm_capabilities = xdg_toplevel_wm_capabilities,
};


#if 0
void display_error(void *data, struct wl_display *wl_display, void *object_id, uint32_t code, const char *message) {
    client_state_t* state = data;

    if (state->compositor == object_id) {

    } else if (state->seat == object_id) {

    } else if (state->layer_shell == object_id) {
        puts("Error in layer shell: ");

    } else if (wl_display == object_id) {
        fprintf(stderr, "Error in display: ");
    }

    fprintf(stderr, "Error: %s\n", message);
}

void display_delete_id(void *data, struct wl_display *wl_display, uint32_t id) {}

static const struct wl_display_listener display_listener = {
    .error = display_error,
    .delete_id = display_delete_id,
};
#endif

#ifdef HAS_LIBDECOR

static void libdecor_error(struct libdecor* context, enum libdecor_error error, const char* message) {
	fprintf(stderr, "Caught error: %s\n", message);
}

static struct libdecor_interface libdecor_iface = {
	.error = libdecor_error,
};

static void libdecor_event_configure(struct libdecor_frame* frame, struct libdecor_configuration* configuration, void* data) {
	client_state_t *client = data;

	int width, height;
	if (!libdecor_configuration_get_content_size(configuration, frame, &width, &height)) {
		width = client->width;
		height = client->height;
	}

    // fprintf(stderr, "Libdecor (%d, %d)\n", width, height);

	client->width = width;
	client->height = height;

    libdecor_frame_set_max_content_size(frame, width, height);

	struct libdecor_state* state = libdecor_state_new(width, height);
	libdecor_frame_commit(frame, state, configuration);
	libdecor_state_free(state);

    if (width != 0 && height != 0) {
        reallocate_framebuffer(client, width, height);

        add_command(&state->command_buffer, (command_t) {
            .type = COMMAND_TYPE_DRAW_EVERYTHING,
        });
        try_execute_command_buffer(client);
    }
}

static void libdecor_event_close(struct libdecor_frame* frame, void* data) {
    client_state_t* state = data;

    state->request_quit = true;
}

static void libdecor_event_commit(struct libdecor_frame* frame, void* data) {
    client_state_t* state = data;

	wl_surface_commit(state->surface);
}

static void libdecor_event_dismiss_popup(struct libdecor_frame* frame, const char* seat_name, void* data) {
}

static struct libdecor_frame_interface libdecor_frame_iface = {
	.configure = libdecor_event_configure,
	.close = libdecor_event_close,
	.commit = libdecor_event_commit,
	.dismiss_popup = libdecor_event_dismiss_popup,
};
#endif

void init_wayland_window(struct wl_display* display, client_state_t* state) {
    state->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, state->surface);
    xdg_surface_add_listener(state->xdg_surface, &xdg_surface_listener, state);

    state->xdg_toplevel = xdg_surface_get_toplevel(state->xdg_surface);
    xdg_toplevel_add_listener(state->xdg_toplevel, &xdg_toplevel_listener, state);

    if (state->xdg_decoration_manager) {
        // @Incomplete: implement server side decoration on a compatible compositor...
        // auto toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(state->xdg_decoration_manager, xdg_toplevel);
    }
}

void init_libdecor_window(struct wl_display* display, client_state_t* state) {
#ifdef HAS_LIBDECOR
    state->context = libdecor_new(display, &libdecor_iface);
    state->frame = libdecor_decorate(state->context, state->surface, &libdecor_frame_iface, state);
    libdecor_frame_set_app_id(state->frame, "libdecor-demo");
    libdecor_frame_set_title(state->frame, "libdecor demo");
    libdecor_frame_map(state->frame);
    libdecor_frame_set_min_content_size(state->frame, 150, 100);
#endif
}

void init_scene(client_state_t* state) {
    state->button = (button_t) {
        .x = 0.0f,
        .y = 0.0f,
        .w = 0.2f,
        .h = 0.2f,
    };
}

int main() {
    gdb_start_instance();

#if 0
    auto handle = gdb_send_command((gdb_command_t) {
        .type = GDB_COMMAND_TYPE_LOAD_FILE,
        .string = sprint("build/debugger"),
        // .type = GDB_COMMAND_TYPE_PWD,
    });

    auto output = gdb_wait_command_result(handle);
    printf("Pwd := %.*s\n", fmt(output.cwd));
#endif


    client_state_t state = {};
    init_scene(&state);

    auto display  = wl_display_connect(NULL);  // wl_display_add_listener(display, &display_listener, &state);
    auto registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(display);

    if (state.xdg_decoration_manager) {
        init_wayland_window(display, &state);
    } else {
        load_libdecor(); // @Note: Fuck GNOME...
        if (using_libdecor()) {
            init_libdecor_window(display, &state);
        } else {
            unload_libdecor();
            init_wayland_window(display, &state);
        }
    }

    int initial_width  = 1400;
    int initial_height = 920;
    state.width  = initial_width;
    state.height = initial_height;

    update_orthographic_projection(initial_width, initial_height);

    wl_surface_commit(state.surface);


    state.buffer = allocate_framebuffer(&state);
    add_command(&state.command_buffer, (command_t) {
        .type = COMMAND_TYPE_DRAW_EVERYTHING,
    });
    try_execute_command_buffer(&state);


    int fd = wl_display_get_fd(display);
    while (true) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);

        while (wl_display_prepare_read(display) != 0) {
            wl_display_dispatch_pending(display);
        }

        if (state.request_quit) {
            break;
        }

        wl_display_flush(display);
        select(fd+1, &set, NULL, NULL, NULL);

        if (FD_ISSET(fd, &set)) {
            wl_display_read_events(display);
        }
    }

    gdb_send_command((gdb_command_t) {
        .type = GDB_COMMAND_TYPE_QUIT,
    });

    // TODO: this just doesn't work, use goto to jump here.
    wl_display_disconnect(display);

    gdb_wait_until_finished();
    return 0;
}

