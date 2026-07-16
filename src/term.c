/* Terminal layer: kitty graphics presentation and key input.
 *
 * Presentation is delegated to the vendored kitty-framebuffer library,
 * which owns raw mode, the alternate screen, the graphics probe, the
 * presenter thread and the restore sequences.  This file keeps the
 * game-facing term_* API and the kitty-keyboard input glue.
 *
 * Composition order (per the kitty-framebuffer README): the framebuffer
 * session starts first so its graphics probe can read the terminal's
 * responses, then the keyboard layer starts with make_raw disabled
 * because the framebuffer already owns raw mode.  Shutdown reverses
 * that: the keyboard mode pop must happen inside the alternate screen
 * it was pushed on, so the keyboard stops before the framebuffer.
 */
#include "terminal_lander.h"
#include "kitty_framebuffer.h"
#include "kitty_keyboard_posix.h"

#include <errno.h>
#include <unistd.h>

static kittyfb_session frame_session;
static bool sessionActive = false;
static bool keyboardActive = false;
static kittykb_terminal keyboard;
static volatile int shutdownClaimed = 0;

bool term_init(int *outW, int *outH)
{
    kittyfb_options frameOptions;
    kittykb_terminal_options keyboardOptions;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;

    kittyfb_session_init(&frame_session);
    kittyfb_options_init(&frameOptions);
    /* the game's historical framebuffer bounds */
    frameOptions.min_width = 640;
    frameOptions.min_height = 400;
    frameOptions.max_width = 1600;
    frameOptions.max_height = 1000;
    /* the game never polls for resizes, so keep SIGWINCH untouched */
    frameOptions.install_winch_handler = false;
    if (kittyfb_start(&frame_session, STDIN_FILENO, STDOUT_FILENO,
                      &frameOptions) != 0)
        return false;

    kittykb_terminal_init(&keyboard);
    kittykb_terminal_options_init(&keyboardOptions);
    keyboardOptions.flags = KITTYKB_FLAGS_KEY_STATE;
    keyboardOptions.make_raw = false;
    keyboardOptions.make_nonblocking = false;
    if (kittykb_terminal_start(&keyboard, STDIN_FILENO, STDOUT_FILENO,
                               &keyboardOptions) != 0) {
        int error = errno;
        kittyfb_stop(&frame_session);
        errno = error;
        return false;
    }
    keyboardActive = true;
    sessionActive = true;
    shutdownClaimed = 0;
    *outW = kittyfb_width(&frame_session);
    *outH = kittyfb_height(&frame_session);
    return true;
}

/* one-shot guard shared by the normal and signal-handler exit paths */
static bool claim_shutdown(void)
{
    if (!sessionActive) return false;
    return !__sync_lock_test_and_set(&shutdownClaimed, 1);
}

void term_shutdown(void)
{
    if (!claim_shutdown()) return;
    if (keyboardActive) {
        (void)kittykb_terminal_stop(&keyboard);
        keyboardActive = false;
    }
    kittyfb_stop(&frame_session);
    sessionActive = false;
}

/* async-signal path: no locks, no pthread_join.  The keyboard mode pop
 * goes out first (behind an ST that closes any half-written graphics
 * escape) so it lands inside the alternate screen; the library call then
 * fences the presenter thread and restores everything else. */
void term_emergency_restore(void)
{
    static const char keyboardPop[] = "\x1b\\\x1b[<u";

    if (!claim_shutdown()) return;
    (void)write(STDOUT_FILENO, keyboardPop, sizeof keyboardPop - 1);
    kittyfb_emergency_restore(&frame_session);
}

void term_present(const uint8_t *rgba, int w, int h)
{
    if (!sessionActive) return;
    (void)kittyfb_present(&frame_session, rgba, w, h);
}

int term_read_input(void)
{
    if (!keyboardActive) {
        errno = EINVAL;
        return -1;
    }
    return kittykb_terminal_read(&keyboard);
}

bool term_next_key_event(kittykb_event *event)
{
    return keyboardActive && kittykb_input_next(&keyboard.input, event);
}

bool term_key_down(uint32_t key)
{
    return keyboardActive && kittykb_input_key_down(&keyboard.input, key);
}

bool term_has_release_events(void)
{
    return keyboardActive &&
           kittykb_input_has_release_events(&keyboard.input);
}
