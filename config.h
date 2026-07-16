/* suckcam config.h — edit, then `make` */
#include <stdint.h>
#include <linux/input-event-codes.h>

static const char *video_device = "/dev/video0";
static const int   cap_width    = 640;
static const int   cap_height   = 480;

/* window = capture area + this much bottom margin for controls */
static const int   ctrl_height  = 56;

static const int   timer_seconds_default = 3;

/* save_dir: use NULL to mean $HOME/Pictures (resolved at runtime).
 * Set to an absolute path like "/tmp" to override. */
static const char *save_dir    = NULL;
static const char *save_subdir = "Pictures";   /* appended to $HOME when save_dir==NULL */
static const char *save_prefix = "shot";       /* shot_2026-07-16_...png */

/* ARGB, matches gruvbox */
static const uint32_t col_bg      = 0xFF1D2021; /* bg0 */
static const uint32_t col_btn     = 0xFF3C3836; /* bg1 */
static const uint32_t col_btn_hot = 0xFF458588; /* blue */
static const uint32_t col_btn_arm = 0xFFCC241D; /* red, e.g. timer armed */
static const uint32_t col_text    = 0xFFEBDBB2; /* fg1 */
static const uint32_t col_timer   = 0xFFFABD2F; /* yellow, countdown text */

/* button layout: id, label, x is auto-packed left-to-right in ctrl bar */
enum { BTN_SHOOT, BTN_TIMER, BTN_QUIT, BTN_COUNT };

static const char *btn_labels[BTN_COUNT] = {
	"SHOOT [spc]", "TIMER [ret]", "QUIT [q]",
};

/* keyboard bindings — raw Linux evdev keycodes (linux/input-event-codes.h).
 * These work without xkbcommon on any layout-independent basis. */
static const uint32_t key_shoot = KEY_SPACE;   /* space  → shoot */
static const uint32_t key_timer = KEY_ENTER;   /* return → toggle timer */
static const uint32_t key_quit  = KEY_Q;       /* q      → quit */
