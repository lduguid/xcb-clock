#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* libX11-xcb is present at runtime; headers are optional. */
extern xcb_connection_t *XGetXCBConnection(Display *dpy);
extern void XSetEventQueueOwner(Display *dpy, int owner);
#ifndef XCBOwnsEventQueue
#define XCBOwnsEventQueue 1
#endif

#define WIN_W 420
#define WIN_H 480
#define TOGGLE_W 140
#define TOGGLE_H 36
#define KNOB_PAD 4
#define KNOB_W 64
#define FONT_W 9

static Display *dpy;
static xcb_connection_t *conn;
static xcb_window_t win;
static xcb_screen_t *screen;
static xcb_colormap_t cmap;
static GLXContext glctx;
static GLuint font_base;
static XFontStruct *xfont;
static int analog = 1;
static int running = 1;
static int win_w = WIN_W;
static int win_h = WIN_H;
static float rot_x;
static float rot_y;
static float vel_x = 23.1f;
static float vel_y = 42.0f;
static int held_left;
static int held_right;
static int held_up;
static int held_down;

static int toggle_x(void)
{
    return (win_w - TOGGLE_W) / 2;
}

static int toggle_y(void)
{
    return win_h - 60;
}

static void gl_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    glColor4ub(r, g, b, a);
}

static void fill_rect(float x, float y, float w, float h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    gl_color(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void draw_line(float x1, float y1, float x2, float y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                      float width)
{
    glLineWidth(width);
    gl_color(r, g, b, a);
    glBegin(GL_LINES);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
}

static void fill_circle(float cx, float cy, float rad, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const int segs = 72;
    gl_color(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segs; i++) {
        float ang = (float)(i * 2.0 * M_PI / segs);
        glVertex2f(cx + cosf(ang) * rad, cy + sinf(ang) * rad);
    }
    glEnd();
}

static void stroke_circle(float cx, float cy, float rad, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                          float width)
{
    const int segs = 72;
    glLineWidth(width);
    gl_color(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segs; i++) {
        float ang = (float)(i * 2.0 * M_PI / segs);
        glVertex2f(cx + cosf(ang) * rad, cy + sinf(ang) * rad);
    }
    glEnd();
}

static void draw_text(int x, int y, const char *s, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    gl_color(r, g, b, a);
    glRasterPos2i(x, y);
    glListBase(font_base);
    glCallLists((GLsizei)strlen(s), GL_UNSIGNED_BYTE, s);
}

static void draw_text_readable(int x, int y, const char *s, uint8_t r, uint8_t g, uint8_t b)
{
    draw_text(x + 1, y + 1, s, 8, 8, 10, 220);
    draw_text(x, y, s, r, g, b, 255);
}

static void draw_hand(float cx, float cy, float angle, float len, float width, uint8_t r, uint8_t g,
                      uint8_t b)
{
    float x2 = cx + cosf(angle) * len;
    float y2 = cy + sinf(angle) * len;
    draw_line(cx, cy, x2, y2, 8, 8, 10, 200, width + 2.5f);
    draw_line(cx, cy, x2, y2, r, g, b, 255, width);
}

static void draw_toggle(void)
{
    int x = toggle_x();
    int y = toggle_y();
    fill_rect((float)x, (float)y, TOGGLE_W, TOGGLE_H, 40, 44, 52, 210);
    int knob_x = analog ? x + KNOB_PAD : x + TOGGLE_W - KNOB_W - KNOB_PAD;
    fill_rect((float)knob_x, (float)(y + KNOB_PAD), KNOB_W, TOGGLE_H - KNOB_PAD * 2, 80, 160, 220, 240);
    const char *label = analog ? "Analog" : "Digital";
    draw_text_readable(x + 18, y + 24, label, 235, 235, 240);
}

static void draw_analog(struct tm *tm, double frac_sec)
{
    const float cx = win_w / 2.0f;
    const float cy = win_h * 0.42f;
    const float r = fminf((float)win_w, (float)win_h) * 0.32f;

    /* Glass face: cube shows through, hands stay readable. */
    fill_circle(cx, cy, r, 12, 14, 20, 88);
    stroke_circle(cx, cy, r, 210, 216, 228, 230, 2.5f);
    stroke_circle(cx, cy, r - 3.0f, 160, 170, 190, 140, 1.0f);

    for (int i = 0; i < 60; i++) {
        float a = (float)((i * 6.0 - 90.0) * M_PI / 180.0);
        float inner = (i % 5 == 0) ? r - 18.0f : r - 10.0f;
        float outer = r - 6.0f;
        if (i % 5 == 0)
            draw_line(cx + cosf(a) * inner, cy + sinf(a) * inner, cx + cosf(a) * outer,
                      cy + sinf(a) * outer, 240, 240, 245, 255, 3.0f);
        else
            draw_line(cx + cosf(a) * inner, cy + sinf(a) * inner, cx + cosf(a) * outer,
                      cy + sinf(a) * outer, 200, 205, 215, 180, 1.0f);
    }

    static const char *nums[] = {"12", "3", "6", "9"};
    static const float nang[] = {-90.0f, 0.0f, 90.0f, 180.0f};
    for (int i = 0; i < 4; i++) {
        float a = (float)(nang[i] * M_PI / 180.0);
        float tx = cx + cosf(a) * (r - 36.0f) - (float)(strlen(nums[i]) * FONT_W) * 0.5f;
        float ty = cy + sinf(a) * (r - 36.0f) + 5.0f;
        draw_text_readable((int)tx, (int)ty, nums[i], 235, 235, 240);
    }

    double sec = tm->tm_sec + frac_sec;
    double min = tm->tm_min + sec / 60.0;
    double hour = (tm->tm_hour % 12) + min / 60.0;
    float ha = (float)((hour * 30.0 - 90.0) * M_PI / 180.0);
    float ma = (float)((min * 6.0 - 90.0) * M_PI / 180.0);
    float sa = (float)((sec * 6.0 - 90.0) * M_PI / 180.0);

    draw_hand(cx, cy, ha, r * 0.50f, 6.0f, 235, 235, 240);
    draw_hand(cx, cy, ma, r * 0.72f, 4.0f, 180, 200, 230);
    draw_hand(cx, cy, sa, r * 0.82f, 2.0f, 220, 80, 80);
    fill_circle(cx, cy, 6.0f, 8, 8, 10, 200);
    fill_circle(cx, cy, 4.5f, 235, 235, 240, 255);
}

static void draw_digital(struct tm *tm)
{
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm);

    float panel_w = (float)win_w - 80.0f;
    float panel_h = 160.0f;
    float px = 40.0f;
    float py = (float)win_h * 0.28f;
    fill_rect(px, py, panel_w, panel_h, 12, 14, 20, 96);

    int tw = (int)strlen(buf) * FONT_W;
    draw_text_readable((win_w - tw) / 2, (int)(py + 70), buf, 140, 235, 195);

    strftime(buf, sizeof(buf), "%A %d %b %Y", tm);
    tw = (int)strlen(buf) * FONT_W;
    draw_text_readable((win_w - tw) / 2, (int)(py + 110), buf, 200, 208, 220);
}

static void cube_face(float nx, float ny, float nz, const float v[12], float r, float g, float b)
{
    glColor3f(r, g, b);
    glNormal3f(nx, ny, nz);
    glBegin(GL_QUADS);
    glVertex3f(v[0], v[1], v[2]);
    glVertex3f(v[3], v[4], v[5]);
    glVertex3f(v[6], v[7], v[8]);
    glVertex3f(v[9], v[10], v[11]);
    glEnd();
}

static void wrap_deg(float *a)
{
    *a = fmodf(*a, 360.0f);
    if (*a < 0.0f)
        *a += 360.0f;
}

static int arrow_axis(KeySym ks, xcb_keycode_t code, int *left, int *right, int *up, int *down)
{
    if (ks == XK_Left || ks == XK_KP_Left || code == 113) {
        *left = 1;
        return 1;
    }
    if (ks == XK_Right || ks == XK_KP_Right || code == 114) {
        *right = 1;
        return 1;
    }
    if (ks == XK_Up || ks == XK_KP_Up || code == 111) {
        *up = 1;
        return 1;
    }
    if (ks == XK_Down || ks == XK_KP_Down || code == 116) {
        *down = 1;
        return 1;
    }
    return 0;
}

static void set_arrow(xcb_key_press_event_t *ev, int down)
{
    int left = 0, right = 0, up = 0, dn = 0;
    KeySym ks = XkbKeycodeToKeysym(dpy, ev->detail, 0, 0);
    if (!arrow_axis(ks, ev->detail, &left, &right, &up, &dn))
        return;
    if (left)
        held_left = down;
    if (right)
        held_right = down;
    if (up)
        held_up = down;
    if (dn)
        held_down = down;
}

static void draw_cube(void)
{

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);

    GLfloat ambient[] = {0.18f, 0.18f, 0.22f, 1.0f};
    GLfloat diffuse[] = {0.95f, 0.95f, 0.95f, 1.0f};
    GLfloat pos[] = {2.4f, 3.2f, 4.0f, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(42.0, (double)win_w / (double)win_h, 0.1, 20.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.12f, -4.6f);
    glRotatef(rot_x, 1.0f, 0.15f, 0.0f);
    glRotatef(rot_y, 0.2f, 1.0f, 0.0f);

    const float s = 1.05f;
    float f[] = {-s, -s, s, s, -s, s, s, s, s, -s, s, s};
    float bk[] = {s, -s, -s, -s, -s, -s, -s, s, -s, s, s, -s};
    float l[] = {-s, -s, -s, -s, -s, s, -s, s, s, -s, s, -s};
    float r[] = {s, -s, s, s, -s, -s, s, s, -s, s, s, s};
    float tface[] = {-s, s, s, s, s, s, s, s, -s, -s, s, -s};
    float btm[] = {-s, -s, -s, s, -s, -s, s, -s, s, -s, -s, s};

    cube_face(0, 0, 1, f, 0.92f, 0.28f, 0.32f);
    cube_face(0, 0, -1, bk, 0.95f, 0.62f, 0.18f);
    cube_face(-1, 0, 0, l, 0.22f, 0.72f, 0.42f);
    cube_face(1, 0, 0, r, 0.25f, 0.48f, 0.92f);
    cube_face(0, 1, 0, tface, 0.95f, 0.88f, 0.22f);
    cube_face(0, -1, 0, btm, 0.72f, 0.28f, 0.82f);

    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

static void begin_overlay(void)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLdouble)win_w, (GLdouble)win_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void redraw(void)
{
    struct timespec rt;
    clock_gettime(CLOCK_REALTIME, &rt);
    time_t now = rt.tv_sec;
    struct tm tm;
    localtime_r(&now, &tm);
    double frac = rt.tv_nsec / 1000000000.0;

    glViewport(0, 0, win_w, win_h);
    glClearColor(18.0f / 255.0f, 20.0f / 255.0f, 24.0f / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    draw_cube();
    begin_overlay();

    fill_rect(0, 0, (float)win_w, 52, 10, 12, 16, 70);
    draw_text_readable((win_w - (int)strlen("XCB Clock") * FONT_W) / 2, 34, "XCB Clock", 220, 225, 235);

    if (analog)
        draw_analog(&tm, frac);
    else
        draw_digital(&tm);

    draw_toggle();
    draw_text_readable(12, win_h - 14, "Arrows rotate", 180, 186, 198);
    glXSwapBuffers(dpy, win);
}

static int hit_toggle(int x, int y)
{
    int tx = toggle_x();
    int ty = toggle_y();
    return x >= tx && x < tx + TOGGLE_W && y >= ty && y < ty + TOGGLE_H;
}

static int load_font(void)
{
    const char *names[] = {"9x15bold", "9x15", "fixed", NULL};
    for (int i = 0; names[i]; i++) {
        xfont = XLoadQueryFont(dpy, names[i]);
        if (xfont)
            break;
    }
    if (!xfont)
        return 0;

    font_base = glGenLists(256);
    glXUseXFont(xfont->fid, 0, 256, font_base);
    return 1;
}

static XVisualInfo *choose_visual(void)
{
    int msaa[] = {
        GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, 4, None,
    };
    XVisualInfo *vi = glXChooseVisual(dpy, DefaultScreen(dpy), msaa);
    if (vi)
        return vi;

    int basic[] = {
        GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8, GLX_DEPTH_SIZE, 24, None,
    };
    return glXChooseVisual(dpy, DefaultScreen(dpy), basic);
}

int main(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open X display\n");
        return 1;
    }

    conn = XGetXCBConnection(dpy);
    if (!conn || xcb_connection_has_error(conn)) {
        fprintf(stderr, "cannot get XCB connection\n");
        return 1;
    }
    XSetEventQueueOwner(dpy, XCBOwnsEventQueue);

    XVisualInfo *vi = choose_visual();
    if (!vi) {
        fprintf(stderr, "no suitable GLX visual\n");
        return 1;
    }

    screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    win = xcb_generate_id(conn);
    cmap = xcb_generate_id(conn);
    xcb_create_colormap(conn, XCB_COLORMAP_ALLOC_NONE, cmap, screen->root, (xcb_visualid_t)vi->visualid);

    uint32_t mask = XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
    uint32_t values[] = {
        0,
        XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS |
            XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_STRUCTURE_NOTIFY,
        cmap,
    };
    xcb_create_window(conn, (uint8_t)vi->depth, win, screen->root, 100, 100, WIN_W, WIN_H, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, (xcb_visualid_t)vi->visualid, mask, values);

    const char *title = "XCB Clock";
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        (uint32_t)strlen(title), title);

    xcb_intern_atom_cookie_t proto_c = xcb_intern_atom(conn, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_cookie_t del_c = xcb_intern_atom(conn, 0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t *proto = xcb_intern_atom_reply(conn, proto_c, NULL);
    xcb_intern_atom_reply_t *del = xcb_intern_atom_reply(conn, del_c, NULL);
    if (proto && del)
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, proto->atom, XCB_ATOM_ATOM, 32, 1, &del->atom);

    xcb_map_window(conn, win);
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
    xcb_flush(conn);
    XkbSetDetectableAutoRepeat(dpy, True, NULL);

    glctx = glXCreateContext(dpy, vi, NULL, True);
    XFree(vi);
    if (!glctx) {
        fprintf(stderr, "cannot create GLX context\n");
        return 1;
    }
    if (!glXMakeCurrent(dpy, win, glctx)) {
        fprintf(stderr, "cannot make GLX context current\n");
        return 1;
    }

    if (!load_font()) {
        fprintf(stderr, "cannot load X font for GL text\n");
        return 1;
    }

    int fd = xcb_get_file_descriptor(conn);
    struct timespec prev;
    clock_gettime(CLOCK_MONOTONIC, &prev);

    while (running) {
        struct timespec now_mono;
        clock_gettime(CLOCK_MONOTONIC, &now_mono);
        double dt = (now_mono.tv_sec - prev.tv_sec) +
                    (now_mono.tv_nsec - prev.tv_nsec) / 1000000000.0;
        prev = now_mono;
        rot_x += (vel_x + (held_up - held_down) * 120.0f) * (float)dt;
        rot_y += (vel_y + (held_right - held_left) * 120.0f) * (float)dt;
        wrap_deg(&rot_x);
        wrap_deg(&rot_y);
        redraw();

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv = {0, 16000};
        int n = select(fd + 1, &fds, NULL, NULL, &tv);
        if (n <= 0)
            continue;

        xcb_generic_event_t *ev;
        while ((ev = xcb_poll_for_event(conn))) {
            switch (ev->response_type & ~0x80) {
            case XCB_EXPOSE:
                break;
            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t *cfg = (xcb_configure_notify_event_t *)ev;
                if (cfg->width > 0 && cfg->height > 0) {
                    win_w = cfg->width;
                    win_h = cfg->height;
                }
                break;
            }
            case XCB_MAP_NOTIFY:
                xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
                break;
            case XCB_BUTTON_PRESS: {
                xcb_button_press_event_t *bp = (xcb_button_press_event_t *)ev;
                xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, win, XCB_CURRENT_TIME);
                if (hit_toggle(bp->event_x, bp->event_y))
                    analog = !analog;
                break;
            }
            case XCB_KEY_PRESS:
                set_arrow((xcb_key_press_event_t *)ev, 1);
                break;
            case XCB_KEY_RELEASE:
                set_arrow((xcb_key_release_event_t *)ev, 0);
                break;
            case XCB_CLIENT_MESSAGE: {
                xcb_client_message_event_t *cm = (xcb_client_message_event_t *)ev;
                if (del && cm->data.data32[0] == del->atom)
                    running = 0;
                break;
            }
            case XCB_DESTROY_NOTIFY:
                running = 0;
                break;
            default:
                break;
            }
            free(ev);
        }
    }

    free(proto);
    free(del);
    glXMakeCurrent(dpy, None, NULL);
    glXDestroyContext(dpy, glctx);
    if (xfont)
        XFreeFont(dpy, xfont);
    XCloseDisplay(dpy);
    return 0;
}
