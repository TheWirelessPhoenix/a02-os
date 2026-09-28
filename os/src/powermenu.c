/* POWER screen, opened by holding Back ~2 s anywhere (KEY_POWER). Cancel returns to the
 * caller, which redraws its own screen. Every confirmed action first finalizes an active
 * recording and silences audio, so no WAV is left without a valid header. */
#include <string.h>
#include "powermenu.h"
#include "power.h"
#include "ui.h"
#include "keys.h"
#include "audio.h"
#include "guard.h"
#include "recorder.h"

enum { PM_OFF, PM_STOCK, PM_RELOAD, PM_CANCEL, PM_N };
static const char *const pm_label[PM_N] = { "POWER OFF", "RESTART (STOCK)", "RELOAD VIA USB", "CANCEL" };
static const char pm_icon[PM_N] = { '-', '-', '-', ' ' };

static void center(const char *s, int y, int scale, uint16_t fg)
{
	int w = (int)strlen(s) * 6 * scale;
	lcd_text((LCD_W - w) / 2, y, s, scale, fg, T->bg);
}

static void message(const char *big, const char *l1, const char *l2)
{
	lcd_rect(0, 0, LCD_W, LCD_H, T->bg);
	center(big, 52, 2, T->accent);
	center(l1, 84, 1, T->text);
	center(l2, 98, 1, T->dim);
}

static void draw(int sel)
{
	ui_header("POWER", 0);
	ui_body_clear();
	for (int i = 0; i < PM_N; i++)
		ui_row(i, pm_icon[i], pm_label[i], "", i == sel);
	lcd_text(8, ROW_Y0 + PM_N * ROW_H + 10, rec_active() ? "REC WILL BE SAVED" : "", 1, T->dim, T->bg);
	ui_hints("< CANCEL", "> SELECT");
}

static void safe_stop(void)
{
	if (rec_active())
		rec_stop();
	audio_halt();
	guard_off();
}

/* Shrinking accent bar + backlight fade: ~0.8 s */
static void goodbye(void)
{
	message("GOODBYE", "", "");
	for (int w = LCD_W - 24; w >= 0; w -= 8) {
		lcd_rect(12, 76, LCD_W - 24, 3, T->bg);
		lcd_rect((LCD_W - w) / 2, 76, w, 3, T->accent);
		delay(40);
	}
	for (int b = 7; b >= 1; b--) {
		backlight_on((uint8_t)b);
		delay(40);
	}
}

static void do_power_off(void)
{
	safe_stop();
	goodbye();
	power_off();
	/* still running: the board is powered over USB and turns off when it is unplugged */
	backlight_on(3);
	message("USB POWER", "UNPLUG USB CABLE", "TO FINISH POWER OFF");
	center("PLAY: RELOAD (USB)", 140, 1, T->dim);
	for (;;) {
		if (keys_poll() == KEY_PLAY) {
			message("RELOAD", "USB RECOVERY MODE", "");
			delay(200);
			power_reboot_adfu();
		}
		delay(5);
	}
}

int powermenu(void)
{
	int sel = PM_OFF, prev = -1;
	for (;;) {
		if (sel != prev) {
			draw(sel);
			prev = sel;
		}
		int k = keys_poll();
		switch (k) {
		case KEY_MENU:
			sel = (sel + PM_N - 1) % PM_N;
			break;
		case KEY_DOWN:
			sel = (sel + 1) % PM_N;
			break;
		case KEY_BACK:
		case KEY_PREV:
			return 0;
		case KEY_PLAY:
		case KEY_NEXT:
			switch (sel) {
			case PM_OFF:
				do_power_off();
				break;
			case PM_STOCK:
				safe_stop();
				message("RESTART", "STARTING STOCK", "FIRMWARE");
				delay(400);
				power_reboot_stock();
			case PM_RELOAD:
				safe_stop();
				message("RELOAD", "USB RECOVERY MODE", "RUN: MAKE RUN");
				delay(400);
				power_reboot_adfu();
			default:
				return 0;
			}
			break;
		default:
			if (rec_active())
				rec_pump(); /* keep a running recording draining while the menu is open */
			else
				delay(2);
			break;
		}
	}
}
