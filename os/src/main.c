/* A02-OS shell: Home / Music / Now Playing / Recorder / Themes / Settings.
 * Keys: M = up, DOWN = down, NEXT or PLAY = open, PREV or BACK = back. */
#include <string.h>
#include "ui.h"
#include "keys.h"
#include "audio.h"
#include "player.h"
#include "ff.h"
#include "util.h"

#define MAXENT 96
#define NAMELEN 64

enum { S_HOME, S_MUSIC, S_REC, S_THEMES, S_SETTINGS };

static FATFS fs;
static char cwd[256] = "/";
static char names[MAXENT][NAMELEN];
static uint8_t isdir[MAXENT];
static int nent, sel, top, screen = S_HOME;
static uint32_t result[8];
static int volume = 0xa0, backlight = 7;
static char last_path[320];
static int have_last;

/* ---------- directory listing ---------- */

static int ent_less(int a, int b)
{
	if (isdir[a] != isdir[b])
		return isdir[a] > isdir[b];
	for (int i = 0;; i++) {
		char x = names[a][i], y = names[b][i];
		if (x >= 'a' && x <= 'z') x -= 32;
		if (y >= 'a' && y <= 'z') y -= 32;
		if (x != y)
			return x < y;
		if (!x)
			return 0;
	}
}

static void swap_ent(int a, int b)
{
	char t[NAMELEN];
	uint8_t d = isdir[a];
	memcpy(t, names[a], NAMELEN);
	memcpy(names[a], names[b], NAMELEN);
	memcpy(names[b], t, NAMELEN);
	isdir[a] = isdir[b];
	isdir[b] = d;
}

static void load_dir(void)
{
	DIR dir;
	FILINFO fno;
	nent = sel = top = 0;
	if (f_opendir(&dir, cwd) != FR_OK)
		return;
	while (nent < MAXENT && f_readdir(&dir, &fno) == FR_OK && fno.fname[0]) {
		if (fno.fname[0] == '.' || (fno.fattrib & (AM_HID | AM_SYS)))
			continue;
		int d = (fno.fattrib & AM_DIR) != 0;
		if (!d && !player_supported(fno.fname))
			continue;
		strncpy(names[nent], fno.fname, NAMELEN - 1);
		names[nent][NAMELEN - 1] = 0;
		isdir[nent] = (uint8_t)d;
		nent++;
	}
	f_closedir(&dir);
	for (int i = 1; i < nent; i++)
		for (int j = i; j > 0 && ent_less(j, j - 1); j--)
			swap_ent(j, j - 1);
}

static void path_join(char *out, const char *dir, const char *name)
{
	size_t n = strlen(dir);
	memcpy(out, dir, n);
	if (n && out[n - 1] != '/')
		out[n++] = '/';
	strcpy(out + n, name);
}

/* strip a leading "001 - " and the extension for display */
static void pretty_name(char *out, int cap, const char *name)
{
	const char *s = name;
	int i = 0;
	while (s[i] >= '0' && s[i] <= '9')
		i++;
	if (i && s[i] == ' ' && s[i + 1] == '-' && s[i + 2] == ' ')
		s += i + 3;
	int n = 0;
	for (; s[n] && n < cap - 1; n++)
		out[n] = s[n];
	out[n] = 0;
	char *dot = 0;
	for (int k = 0; out[k]; k++)
		if (out[k] == '.')
			dot = &out[k];
	if (dot)
		*dot = 0;
}

/* ---------- list screens ---------- */

struct item { char icon; const char *label; const char *meta; };

static const struct item home_items[] = {
	{ '&', "NOW PLAYING", "" }, { '#', "MUSIC", "" }, { '*', "RECORDER", "" },
	{ '%', "THEMES", "" }, { '=', "SETTINGS", "" },
};
#define NHOME 5
#define NSETTINGS 5

static int list_count(void)
{
	switch (screen) {
	case S_HOME: return NHOME;
	case S_MUSIC: return nent;
	case S_THEMES: return NTHEMES;
	case S_SETTINGS: return NSETTINGS;
	}
	return 0;
}

static void list_item(int i, struct item *it, char *metabuf, char *labelbuf)
{
	it->meta = "";
	switch (screen) {
	case S_HOME:
		*it = home_items[i];
		break;
	case S_MUSIC:
		it->icon = isdir[i] ? '+' : ' ';
		pretty_name(labelbuf, 40, names[i]);
		it->label = labelbuf;
		break;
	case S_THEMES:
		it->icon = i == theme_idx ? '*' : ' ';
		it->label = themes[i].name;
		break;
	case S_SETTINGS: {
		static const char *const lbl[NSETTINGS] = { "BACKLIGHT", "VOLUME", "EQUALIZER", "SLEEP TIMER", "ABOUT A02-OS" };
		it->icon = '-';
		it->label = lbl[i];
		if (i == 0) {
			num_str(metabuf, (uint32_t)backlight);
			it->meta = metabuf;
		} else if (i == 1) {
			num_str(metabuf, (uint32_t)((volume - 0x40) * 30 / (0xff - 0x40)));
			it->meta = metabuf;
		} else if (i == 2) {
			it->meta = "SOON";
		} else if (i == 3) {
			it->meta = "OFF";
		} else {
			it->meta = "V0.1";
		}
		break;
	}
	}
}

static void draw_row(int i)
{
	struct item it = { " "[0], "", "" };
	char meta[12], label[48];
	list_item(i, &it, meta, label);
	ui_row(i - top, it.icon, it.label, it.meta, i == sel);
}

static const char *screen_title(void)
{
	switch (screen) {
	case S_MUSIC: return strcmp(cwd, "/") ? strrchr_last(cwd) : "MUSIC";
	case S_THEMES: return "THEMES";
	case S_SETTINGS: return "SETTINGS";
	}
	return "A02-OS";
}

static void draw_screen(void)
{
	int n = list_count();
	ui_header(screen_title(), 0);
	ui_body_clear();
	if (screen == S_MUSIC && !n)
		lcd_text(8, ROW_Y0 + 4, "NO MUSIC HERE", 1, T->dim, T->bg);
	for (int i = top; i < n && i < top + ROWS; i++)
		draw_row(i);
	static const char *const hl[] = { "M/DN MOVE", "< BACK", "", "< BACK", "< BACK" };
	static const char *const hr[] = { "> OPEN", "> PLAY", "", "> APPLY", "> CHANGE" };
	ui_hints(hl[screen], hr[screen]);
}

/* ---------- recorder (placeholder until the ADC path is reverse engineered) ---------- */

static void draw_recorder(void)
{
	ui_header("RECORDER", 0);
	ui_body_clear();
	int cx = LCD_W / 2, cy = 52;
	lcd_rect(cx - 24, cy - 24, 48, 2, T->dim);
	lcd_rect(cx - 24, cy + 22, 48, 2, T->dim);
	lcd_rect(cx - 24, cy - 24, 2, 48, T->dim);
	lcd_rect(cx + 22, cy - 24, 2, 48, T->dim);
	lcd_rect(cx - 12, cy - 12, 24, 24, T->rec);
	lcd_text(cx - 24, 88, "0:00", 2, T->text, T->bg);
	lcd_text(10, 112, "LECTURE RECORDER", 1, T->dim, T->bg);
	lcd_text(10, 124, "COMING NEXT UPDATE", 1, T->accent, T->bg);
	ui_hints("< BACK", "");
}

/* ---------- now playing ---------- */

#define VIZ_X 6
#define VIZ_Y 20
#define VIZ_W (LCD_W - 12)
#define VIZ_H 50
#define BAR_W 5
#define BAR_GAP 3
#define BAR_MAX (VIZ_H - 8)

static const char *np_file;
static int np_bar[NBANDS], np_last_bar, np_last_sec, np_vol_popup, np_paused;

static void np_times(void)
{
	char a[12], b[12];
	int y = 131;
	time_str(a, player_secs);
	time_str(b, player_total_secs);
	lcd_rect(0, y, LCD_W, 9, T->bg);
	lcd_text(6, y, a, 1, T->dim, T->bg);
	lcd_text(LCD_W - 6 - 6 * (int)strlen(b), y, b, 1, T->dim, T->bg);
	const char *st = np_paused ? "PAUSED" : "";
	lcd_text(LCD_W / 2 - 3 * (int)strlen(st), y, st, 1, T->accent, T->bg);
}

static void np_title(void)
{
	char name[48];
	const char *title = player_title;
	if (!title[0]) {
		pretty_name(name, sizeof(name), np_file);
		title = name;
	}
	lcd_rect(0, 74, LCD_W, 48, T->bg);
	int len = (int)strlen(title);
	ui_text_fit(6, 76, title, 20, T->text, T->bg);
	if (len > 20)
		ui_text_fit(6, 86, title + 20, 20, T->text, T->bg);
	ui_text_fit(6, 100, player_artist[0] ? player_artist : "UNKNOWN ARTIST", 20, T->dim, T->bg);
}

static void np_start(uint32_t hz, uint32_t ch, uint32_t kbps)
{
	(void)hz;
	(void)ch;
	(void)kbps;
	ui_header("NOW PLAYING", 1);
	ui_body_clear();
	lcd_rect(VIZ_X, VIZ_Y, VIZ_W, VIZ_H, T->surface);
	np_title();
	lcd_rect(6, 125, LCD_W - 12, 3, T->surface);
	for (int i = 0; i < NBANDS; i++)
		np_bar[i] = 0;
	np_last_bar = 0;
	np_last_sec = -1;
	np_vol_popup = 0;
	np_paused = 0;
	np_times();
	ui_hints("<< >> TRACK", "M/DN VOL");
}

static void np_progress(uint32_t pos, uint32_t total)
{
	int w = total ? (int)((uint64_t)pos * (LCD_W - 12) / total) : 0;
	if (w > np_last_bar) {
		lcd_rect(6 + np_last_bar, 125, w - np_last_bar, 3, T->accent);
		np_last_bar = w;
	}
	if ((int)player_secs != np_last_sec) {
		np_last_sec = (int)player_secs;
		np_times();
	}
}

static void np_levels(const uint8_t *lv, int n)
{
	static int tick;
	int x0 = VIZ_X + (VIZ_W - (NBANDS * BAR_W + (NBANDS - 1) * BAR_GAP)) / 2;
	int base = VIZ_Y + VIZ_H - 4;

	if (np_vol_popup && --np_vol_popup == 0)
		np_title();
	if (++tick & 1)
		return; /* draw every other frame to save CPU */
	for (int i = 0; i < n && i < NBANDS; i++) {
		int h = lv[i] * BAR_MAX / 255;
		if (h < np_bar[i] - 3)
			h = np_bar[i] - 3; /* fall slowly */
		if (h == np_bar[i])
			continue;
		int x = x0 + i * (BAR_W + BAR_GAP);
		uint16_t c = (i % 4 == 0) ? T->accent : T->dim;
		if (h > np_bar[i])
			lcd_rect(x, base - h, BAR_W, h - np_bar[i], c);
		else
			lcd_rect(x, base - np_bar[i], BAR_W, np_bar[i] - h, T->surface);
		np_bar[i] = h;
	}
}

static void np_pause(int paused)
{
	np_paused = paused;
	np_times();
}

static void np_key(int k)
{
	char b[16];
	if (k == KEY_MENU)
		volume += 7;
	if (k == KEY_DOWN)
		volume -= 7;
	if (volume > 0xff)
		volume = 0xff;
	if (volume < 0x40)
		volume = 0x40;
	audio_volume((uint32_t)volume);
	int v = (volume - 0x40) * 30 / (0xff - 0x40);
	lcd_rect(14, 76, LCD_W - 28, 30, T->surface);
	lcd_rect(14, 76, LCD_W - 28, 1, T->accent);
	lcd_rect(14, 105, LCD_W - 28, 1, T->accent);
	strcpy(b, "VOLUME ");
	num_str(b + 7, (uint32_t)v);
	lcd_text(20, 80, b, 1, T->dim, T->surface);
	lcd_rect(20, 93, LCD_W - 40, 5, T->bg);
	lcd_rect(20, 93, (LCD_W - 40) * v / 30, 5, T->accent);
	np_vol_popup = 60; /* ~1.5 s of frames */
}

static struct player_ui np_ui = { np_start, np_progress, np_pause, np_key, np_levels };

static void play_from(int idx)
{
	static char path[320];
	while (idx >= 0 && idx < nent) {
		if (isdir[idx]) {
			idx++;
			continue;
		}
		path_join(path, cwd, names[idx]);
		strcpy(last_path, path);
		have_last = 1;
		np_file = names[idx];
		sel = idx;
		int r = player_play(path, &np_ui);
		if (r == PLAYER_BACK || r == PLAYER_ERROR)
			break;
		if (r == PLAYER_PREV) {
			do
				idx--;
			while (idx >= 0 && isdir[idx]);
			if (idx < 0)
				break;
		} else {
			idx++;
		}
	}
	if (sel < top)
		top = sel;
	if (sel >= top + ROWS)
		top = sel - ROWS + 1;
	draw_screen();
}

/* ---------- navigation ---------- */

static void open_music(void)
{
	DIR d;
	if (f_opendir(&d, "/Music") == FR_OK) {
		f_closedir(&d);
		strcpy(cwd, "/Music");
	} else {
		strcpy(cwd, "/");
	}
	screen = S_MUSIC;
	load_dir();
	draw_screen();
}

static void go_parent(void)
{
	char *p = strrchr_last_slash(cwd);
	if (p == cwd)
		p[1] = 0;
	else if (p)
		*p = 0;
}

static void go_home(int select)
{
	screen = S_HOME;
	sel = select;
	top = 0;
	draw_screen();
}

static void activate(void)
{
	switch (screen) {
	case S_HOME:
		if (sel == 0) {
			if (have_last) {
				np_file = strrchr_last(last_path);
				player_play(last_path, &np_ui);
			}
			draw_screen();
		} else if (sel == 1) {
			open_music();
		} else if (sel == 2) {
			screen = S_REC;
			draw_recorder();
		} else {
			screen = sel == 3 ? S_THEMES : S_SETTINGS;
			sel = screen == S_THEMES ? theme_idx : 0;
			top = 0;
			draw_screen();
		}
		break;
	case S_MUSIC:
		if (!nent)
			break;
		if (isdir[sel]) {
			static char np[256];
			path_join(np, cwd, names[sel]);
			strcpy(cwd, np);
			load_dir();
			draw_screen();
		} else {
			play_from(sel);
		}
		break;
	case S_THEMES:
		ui_set_theme(sel);
		draw_screen();
		break;
	case S_SETTINGS:
		if (sel == 0) {
			backlight = backlight >= 10 ? 2 : backlight + 1;
			backlight_on((uint8_t)backlight);
		} else if (sel == 1) {
			volume = volume >= 0xf0 ? 0x40 : volume + 0x18;
			audio_volume((uint32_t)volume);
		}
		draw_row(sel);
		break;
	}
}

static void back(void)
{
	switch (screen) {
	case S_MUSIC:
		if (strcmp(cwd, "/Music") && strcmp(cwd, "/")) {
			go_parent();
			load_dir();
			draw_screen();
		} else {
			go_home(1);
		}
		break;
	case S_REC:
		go_home(2);
		break;
	case S_THEMES:
		go_home(3);
		break;
	case S_SETTINGS:
		go_home(4);
		break;
	}
}

void *main(void)
{
	result[0] = 0xa02a02a0;
	lcd_init();
	ui_set_theme(0);
	lcd_rect(0, 0, LCD_W, LCD_H, T->bg);
	backlight_on((uint8_t)backlight);
	lcd_text(10, 64, "A02-OS", 3, T->accent, T->bg);
	lcd_text(40, 92, "LOADING", 1, T->dim, T->bg);
	keys_init();
	audio_init();
	audio_volume((uint32_t)volume);
	cpu_clock_mhz(96);

	FRESULT fr = f_mount(&fs, "", 1);
	result[1] = fr;
	if (fr != FR_OK) {
		lcd_text(22, 120, "INSERT SD CARD", 1, T->rec, T->bg);
		goto out;
	}
	go_home(0);

	/* press BACK 3 times on the home screen to exit to the USB loader */
	int back_count = 0;
	for (;;) {
		wdt_feed();
		int k = keys_poll();
		int n = list_count(), old = sel;
		if (k == KEY_NONE) {
			delay(1);
			continue;
		}
		if (screen == S_REC) {
			if (k == KEY_BACK || k == KEY_PREV)
				back();
			continue;
		}
		if (k != KEY_BACK)
			back_count = 0;
		switch (k) {
		case KEY_MENU:
			if (sel > 0)
				sel--;
			break;
		case KEY_DOWN:
			if (sel < n - 1)
				sel++;
			break;
		case KEY_NEXT:
		case KEY_PLAY:
			activate();
			continue;
		case KEY_PREV:
		case KEY_BACK:
			if (screen == S_HOME) {
				if (k == KEY_BACK && ++back_count >= 3)
					goto out;
				continue;
			}
			back();
			continue;
		}
		if (sel != old) {
			if (sel < top || sel >= top + ROWS) {
				top = sel < top ? sel : sel - ROWS + 1;
				draw_screen();
			} else {
				draw_row(old);
				draw_row(sel);
			}
		}
	}
out:
	lcd_rect(0, 0, LCD_W, LCD_H, T->bg);
	lcd_text(28, 72, "BYE", 3, T->dim, T->bg);
	static uint32_t ret[2];
	ret[0] = (uint32_t)result;
	ret[1] = sizeof(result);
	return ret;
}
