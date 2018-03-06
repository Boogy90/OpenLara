#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <linux/joystick.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <pwd.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include "game.h"

#define WND_TITLE       "OpenLara"

// timing
unsigned int startTime;

int osGetTime() {
    timeval t;
    gettimeofday(&t, NULL);
    return int((t.tv_sec - startTime) * 1000 + t.tv_usec / 1000);
}

// sound
#define SND_FRAME_SIZE  4
#define SND_DATA_SIZE   (1024 * SND_FRAME_SIZE)

pa_simple *sndOut;
pthread_t sndThread;

Sound::Frame *sndData;

void* sndFill(void *arg) {
    while (1) {
        Sound::fill(sndData, SND_DATA_SIZE / SND_FRAME_SIZE);
        pa_simple_write(sndOut, sndData, SND_DATA_SIZE, NULL);
    }
    return NULL;
}

void sndInit() {
    static const pa_sample_spec spec = {
        .format   = PA_SAMPLE_S16LE,
        .rate     = 44100,
        .channels = 2
    };

    static const pa_buffer_attr attr = {
        .maxlength  = SND_DATA_SIZE * 4,
        .tlength    = 0xFFFFFFFF,
        .prebuf     = 0xFFFFFFFF,
        .minreq     = SND_DATA_SIZE,
        .fragsize   = 0xFFFFFFFF,
    };

    int error;
    if (!(sndOut = pa_simple_new(NULL, WND_TITLE, PA_STREAM_PLAYBACK, NULL, "game", &spec, NULL, &attr, &error))) {
        LOG("pa_simple_new() failed: %s\n", pa_strerror(error));
        sndData = NULL;
        return;
    }

    sndData = new Sound::Frame[SND_DATA_SIZE / SND_FRAME_SIZE];
    pthread_create(&sndThread, NULL, sndFill, NULL);
}

void sndFree() {
    if (sndOut) {
        pthread_cancel(sndThread);
    //    pa_simple_flush(sndOut, NULL);
    //    pa_simple_free(sndOut);
        delete[] sndData;
    }
}

// Input
InputKey keyToInputKey(int code) {
    int codes[] = {
        113, 114, 111, 116, 65, 23, 36, 9, 50, 37, 64,
        19, 10, 11, 12, 13, 14, 15, 16, 17, 18,
        38, 56, 54, 40, 26, 41, 42, 43, 31, 44, 45, 46, 58,
        57, 32, 33, 24, 27, 39, 28, 30, 55, 25, 53, 29, 52,
    };

    for (int i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
        if (codes[i] == code)
            return (InputKey)(ikLeft + i);
    return ikNone;
}

InputKey mouseToInputKey(int btn) {
    switch (btn) {
        case 1 : return ikMouseL;
        case 2 : return ikMouseM;
        case 3 : return ikMouseR;
    }
    return ikNone;
}

#define JOY_DEAD_ZONE_STICK      8192
#define JOY_DEAD_ZONE_TRIGGER    8192
#define JOY_MIN_UPDATE_FX_TIME   50.0f

struct JoyDevice {
    int   fd;     // device file descriptor
    int   fe;     // event file descriptor
    vec2  L, R;   // left/right stick axes values
    float vL, vR; // current value for left/right motor vibration
    float oL, oR; // last applied value
    int   time;   // time when we can send effect update
    ff_effect fx; // effect structure
} joyDevice[INPUT_JOY_COUNT];

bool osJoyReady(int index) {
    return joyDevice[index].fd != -1;
}

void osJoyVibrate(int index, float L, float R) {
    if (!osJoyReady(index)) return;
    joyDevice[index].vL = L;
    joyDevice[index].vR = R;
}

void joyInit() {
    LOG("init gamepads:\n");
    char name[128];
    for (int i = 0; i < INPUT_JOY_COUNT; i++) {
        JoyDevice &joy = joyDevice[i];
    // open device
        sprintf(name, "/dev/input/js%d", i);
        joy.fd = open(name, O_RDONLY | O_NONBLOCK);
        if (joy.fd == -1)
            continue;
    // skip init messages
        js_event event;
        while (read(joy.fd, &event, sizeof(event)) != -1 && (event.type & JS_EVENT_INIT));
    // get gamepad info
        int8 axes, buttons;
        ioctl(joy.fd, JSIOCGAXES,    &axes);
        ioctl(joy.fd, JSIOCGBUTTONS, &buttons);
        
        if (axes < 4 || buttons < 11) { // is it really a gamepad?
            close(joy.fd);
            joy.fd = -1;
            continue;
        }

        if (ioctl(joy.fd, JSIOCGNAME(sizeof(name)), name) < 0)
            strcpy(name, "Unknown");

        LOG("gamepad %d\n", i + 1);
        LOG(" name : %s\n", name);
        LOG(" btns : %d\n", int(buttons));
        LOG(" axes : %d\n", int(axes));
        
        joy.fe = -1;
        for (int j = 0; j < 99; j++) {
            sprintf(name, "/sys/class/input/js%d/device/event%d", i, j);
            DIR *dir = opendir(name);
            if (!dir) continue;
            closedir(dir);
            sprintf(name, "/dev/input/event%d", j);
            joy.fe = open(name, O_RDWR);
            break;
        }
        
        uint32 features[4];
        if (joy.fe > -1 && (ioctl(joy.fe, EVIOCGBIT(EV_FF, sizeof(features)), features) == -1 || !TEST_BIT(features, FF_RUMBLE))) {
            close(joy.fe);
            joy.fe = -1;
        }
        
        if (joy.fe > -1) {
            int n_effects;
            if (ioctl(joy.fe, EVIOCGEFFECTS, &n_effects) == -1) {
                perror("Ioctl query");
            }
            LOG(" vibration feature %d\n", n_effects);
            joy.fx.id           = -1;
            joy.fx.type         = FF_RUMBLE;
            joy.fx.replay.delay = 0;
            joy.vL = joy.oL = joy.vR = joy.oR = 0.0f;
            joy.time  = osGetTime();
        }
    }
}

void joyFree() {
    for (int i = 0; i < INPUT_JOY_COUNT; i++) {
        JoyDevice &joy = joyDevice[i];
        if (joy.fd == -1) continue;
        close(joy.fd);
        if (joy.fe == -1) continue;
    }
}

float joyAxisValue(int value) {
    if (value > -JOY_DEAD_ZONE_STICK && value < JOY_DEAD_ZONE_STICK)
        return 0.0f;
    return value / 32767.0f;
}

float joyTrigger(int value) {
    if (value + 32767 < JOY_DEAD_ZONE_TRIGGER)
        return 0.0f;
    return min(1.0f, (value + 32767) / 65536.0f);
}

vec2 joyDir(const vec2 &value) {
    float dist = min(1.0f, value.length());
    return value.normal() * dist;
}

void joyRumble(JoyDevice &joy) {
    if (joy.fe == -1) return;
 
    if (joy.oL == 0.0f && joy.vL == 0.0f && joy.oR == 0.0f && joy.vR == 0.0f)
        return;
 
    if (osGetTime() <= joy.time)
        return;
     
    input_event event;
    event.type = EV_FF;

    if (joy.vL != 0.0f || joy.vR != 0.0f) {
    // update effect
        joy.fx.u.rumble.strong_magnitude = int(joy.vL * 65535);
        joy.fx.u.rumble.weak_magnitude   = int(joy.vR * 65535);
        joy.fx.replay.length             = int(max(JOY_MIN_UPDATE_FX_TIME, 1000.0f / Core::stats.fps));
        
        if (ioctl(joy.fe, EVIOCSFF, &joy.fx) == -1) {
            LOG("! joy update fx\n");
        }

    // play effect
        event.value = 1;
        event.code  = joy.fx.id; 
        if (write(joy.fe, &event, sizeof(event)) == -1)
            LOG("! joy play fx\n");
    } else
        if (joy.oL != 0.0f || joy.oR != 0.0f) {
        // stop effect
            event.value = 0;
            event.code  = joy.fx.id; 
            if (write(joy.fe, &event, sizeof(event)) == -1)
                LOG("! joy stop fx\n");
        }
    
    joy.oL = joy.vL;
    joy.oR = joy.vR;
    
    joy.time = osGetTime() + joy.fx.replay.length;
}

void joyUpdate() {
    static const JoyKey keys[] = { jkA, jkB, jkX, jkY, jkLB, jkRB, jkSelect, jkStart, jkNone /*jkHome*/, jkL, jkR };

    for (int i = 0; i < INPUT_JOY_COUNT; i++) {
        JoyDevice &joy = joyDevice[i];
    
        if (joy.fd == -1)
            continue;

        joyRumble(joy);
        
        js_event event;
        while (read(joy.fd, &event, sizeof(event)) != -1) {
        // buttons
            if (event.type & JS_EVENT_BUTTON)
                Input::setJoyDown(i, event.number >= COUNT(keys) ? jkNone : keys[event.number], event.value == 1);
        // axes
            if (event.type & JS_EVENT_AXIS) {
            
                switch (event.number) {
                // Left stick
                    case ABS_X  : joy.L.x = joyAxisValue(event.value); break;
                    case ABS_Y  : joy.L.y = joyAxisValue(event.value); break;
                // Right stick
                    case ABS_RX : joy.R.x = joyAxisValue(event.value); break;
                    case ABS_RY : joy.R.y = joyAxisValue(event.value); break;
                // Left trigger
                    case ABS_Z  : Input::setJoyPos(i, jkLT, joyTrigger(event.value)); break;
                // Right trigger
                    case ABS_RZ : Input::setJoyPos(i, jkRT, joyTrigger(event.value)); break;
                // D-PAD
                    case ABS_HAT0X    :
                    case ABS_THROTTLE :
                        Input::setJoyDown(i, jkLeft,  event.value < -0x4000);
                        Input::setJoyDown(i, jkRight, event.value >  0x4000);
                        break;
                    case ABS_HAT0Y    :
                    case ABS_RUDDER   :
                        Input::setJoyDown(i, jkUp,    event.value < -0x4000);
                        Input::setJoyDown(i, jkDown,  event.value >  0x4000);
                        break;
                }
                
                Input::setJoyPos(i, jkL, joyDir(joy.L));
                Input::setJoyPos(i, jkR, joyDir(joy.R));
            }
        }
    }
}

void toggle_fullscreen(Display* dpy, Window win) {
    const size_t _NET_WM_STATE_TOGGLE=2;

    XEvent xev;
    Atom wm_state  =  XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom scr_full  =  XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);

    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = wm_state;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = _NET_WM_STATE_TOGGLE;
    xev.xclient.data.l[1] = scr_full;

    XSendEvent(dpy, DefaultRootWindow(dpy), False, SubstructureNotifyMask, &xev);
}

void WndProc(const XEvent &e,Display*dpy,Window wnd) {
    switch (e.type) {
        case ConfigureNotify :
            Core::width  = e.xconfigure.width;
            Core::height = e.xconfigure.height;
            break;
        case KeyPress   :
        case KeyRelease :
            if (e.type == KeyPress && (e.xkey.state & Mod1Mask) && e.xkey.keycode == 36) {
                toggle_fullscreen(dpy,wnd);
                break;
            }
            Input::setDown(keyToInputKey(e.xkey.keycode), e.type == KeyPress);
            break;
        case ButtonPress :
        case ButtonRelease : {
            InputKey key = mouseToInputKey(e.xbutton.button);
            Input::setPos(key, Input::mouse.pos);
            Input::setDown(key, e.type == ButtonPress);
            break;
        }
        case MotionNotify :
            Input::setPos(ikMouseL, vec2((float)e.xmotion.x, (float)e.xmotion.y));
            break;
    }
}

char Stream::cacheDir[255];
char Stream::contentDir[255];

int main(int argc, char **argv) {
    Stream::contentDir[0] = Stream::cacheDir[0] = 0;

    const char *home;
    if (!(home = getenv("HOME")))
        home = getpwuid(getuid())->pw_dir;
    strcat(Stream::cacheDir, home);
    strcat(Stream::cacheDir, "/.OpenLara/");
    
    struct stat st = {0};    
    if (stat(Stream::cacheDir, &st) == -1 && mkdir(Stream::cacheDir, 0777) == -1)
        Stream::cacheDir[0] = 0;

    static int XGLAttr[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_DEPTH_SIZE, 24,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        0
    };

    Display *dpy = XOpenDisplay(NULL);
    XVisualInfo *vis = glXChooseVisual(dpy, XDefaultScreen(dpy), XGLAttr);

    XSetWindowAttributes attr;
    attr.colormap = XCreateColormap(dpy, RootWindow(dpy, vis->screen), vis->visual, AllocNone);
    attr.border_pixel = 0;
    attr.event_mask = KeyPressMask | KeyReleaseMask | StructureNotifyMask |
                      ButtonPressMask | ButtonReleaseMask |
                      ButtonMotionMask | PointerMotionMask;

    Window wnd = XCreateWindow(dpy, RootWindow(dpy, vis->screen),
                               0, 0, 1280, 720, 0,
                               vis->depth, InputOutput, vis->visual,
                               CWColormap | CWBorderPixel | CWEventMask, &attr);
    XStoreName(dpy, wnd, WND_TITLE);

    GLXContext ctx = glXCreateContext(dpy, vis, NULL, true);
    glXMakeCurrent(dpy, wnd, ctx);
    XMapWindow(dpy, wnd);

    Atom WM_DELETE_WINDOW = XInternAtom(dpy, "WM_DELETE_WINDOW", 0);
    XSetWMProtocols(dpy, wnd, &WM_DELETE_WINDOW, 1);

    joyInit();

    timeval t;
    gettimeofday(&t, NULL);
    startTime = t.tv_sec;

    sndInit();
    Game::init(argc > 1 ? argv[1] : NULL);

    while (!Core::isQuit) {
        if (XPending(dpy)) {
            XEvent event;
            XNextEvent(dpy, &event);
            if (event.type == ClientMessage && *event.xclient.data.l == WM_DELETE_WINDOW)
                Core::quit();
            WndProc(event,dpy,wnd);
        } else {
            joyUpdate();
			bool updated = Game::update();
            if (updated) {
				Game::render();
                Core::waitVBlank();
				glXSwapBuffers(dpy, wnd);
			}
        }
    };

    joyFree();
    sndFree();
    Game::deinit();

    glXMakeCurrent(dpy, 0, 0);
    XCloseDisplay(dpy);
    return 0;
}
