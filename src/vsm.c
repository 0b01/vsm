/**
 *  vsm text editor
 *  07/02/2019
 */
#include <ncurses.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "text.h"
#include "vsm.h"
#include "util.h"

Vsm vsm;

bool ui_init() {
	initscr();			/* Start curses mode 		*/
	raw();				/* Line buffering disabled	*/
	start_color();
	use_default_colors();
	cbreak();
	noecho();
	nonl();
	keypad(stdscr, TRUE);
	meta(stdscr, TRUE);
	curs_set(0);
	return true;
}

void ui_resize() {
	struct winsize ws;
	int width = 80, height = 24;

	if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) != -1) {
		width = ws.ws_col;
		height = ws.ws_row;
	}

	width = MAX(width, 1);
	width = MIN(width, MAX_WIDTH);
	height = MAX(height, 1);
	height = MIN(height, MAX_HEIGHT);

	wresize(stdscr, height, width);

	size_t size = width*height*sizeof(Cell);
	if (size > vsm.cells_size) {
		Cell *cells = realloc(vsm.cells, size);
		if (!cells)
			return;
		memset((char*)cells+vsm.cells_size, 0, size - vsm.cells_size);
		vsm.cells_size = size;
		vsm.cells = cells;
	}
	vsm.width = width;
	vsm.height = height;
}

static void ui_draw_line(int x, int y, char c, enum UiStyle style_id) {
	if (x < 0 || x >= vsm.width || y < 0 || y >= vsm.height)
		return;
	CellStyle style = vsm.styles[style_id];
	Cell (*cells)[vsm.width] = (void*)vsm.cells;
	while (x < vsm.width) {
		cells[y][x].data[0] = c;
		cells[y][x].data[1] = '\0';
		cells[y][x].style = style;
		x++;
	}
}

static void ui_draw_string(int x, int y, const char *str, enum UiStyle style_id) {
	// debug("draw-string: [%d][%d]\n", y, x);
	if (x < 0 || x >= vsm.width || y < 0 || y >= vsm.height)
		return;
	CellStyle style = vsm.styles[style_id];
	// FIXME: does not handle double width characters etc, share code with view.c?
	Cell (*cells)[vsm.width] = (void*)vsm.cells;
	const size_t cell_size = sizeof(cells[0][0].data)-1;
	for (const char *next = str; *str && x < vsm.width; str = next) {
		do next++; while (!ISUTF8(*next));
		size_t len = next - str;
		if (!len)
			break;
		len = MIN(len, cell_size);
		strncpy(cells[y][x].data, str, len);
		cells[y][x].data[len] = '\0';
		cells[y][x].style = style;
		x++;
	}
}

static inline unsigned int color_pair_hash(short fg, short bg) {
	if (fg == -1)
		fg = COLORS;
	if (bg == -1)
		bg = COLORS + 1;
	return fg * (COLORS + 2) + bg;
}

static short color_pair_get(short fg, short bg) {
	static bool has_default_colors;
	static short *color2palette, default_fg, default_bg;
	static short color_pairs_max, color_pair_current;

	if (!color2palette) {
		pair_content(0, &default_fg, &default_bg);
		if (default_fg == -1)
			default_fg = CELL_COLOR_WHITE;
		if (default_bg == -1)
			default_bg = CELL_COLOR_BLACK;
		has_default_colors = (use_default_colors() == OK);
		color_pairs_max = MIN(MAX_COLOR_PAIRS, SHRT_MAX);
		if (COLORS)
			color2palette = calloc((COLORS + 2) * (COLORS + 2), sizeof(short));
	}

	if (fg >= COLORS)
		fg = default_fg;
	if (bg >= COLORS)
		bg = default_bg;

	if (!has_default_colors) {
		if (fg == -1)
			fg = default_fg;
		if (bg == -1)
			bg = default_bg;
	}

	if (!color2palette || (fg == -1 && bg == -1))
		return 0;

	unsigned int index = color_pair_hash(fg, bg);
	if (color2palette[index] == 0) {
		short oldfg, oldbg;
		if (++color_pair_current >= color_pairs_max)
			color_pair_current = 1;
		pair_content(color_pair_current, &oldfg, &oldbg);
		unsigned int old_index = color_pair_hash(oldfg, oldbg);
		if (init_pair(color_pair_current, fg, bg) == OK) {
			color2palette[old_index] = 0;
			color2palette[index] = color_pair_current;
		}
	}

	return color2palette[index];
}

static inline attr_t style_to_attr(CellStyle *style) {
	return style->attr | COLOR_PAIR(color_pair_get(style->fg, style->bg));
}

static void ui_blit() {
	int w = vsm.width, h = vsm.height;
	Cell *cell = vsm.cells;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			attrset(style_to_attr(&cell->style));
			mvaddstr(y, x, cell->data);
			cell++;
		}
	}
	wnoutrefresh(stdscr);
	doupdate();
}

static void ui_draw() {
	// ui_arrange(vsm, vsm.layout);
	if (vsm.info[0])
		ui_draw_string(0, vsm.height-1, vsm.info, UI_STYLE_INFO);
	ui_blit();
}

void ui_info(const char *msg, ...) {
	ui_draw_line(0, vsm.height-1, ' ', UI_STYLE_INFO);
	va_list args;
	va_start(args, msg);
	vsnprintf(vsm.info, sizeof(vsm.info), msg, args);
	va_end (args);
}

void vsm_init() {
	size_t styles_size = UI_STYLE_MAX * sizeof(CellStyle);
	CellStyle* styles = calloc(1, styles_size);
	for (int i = 0; i < UI_STYLE_MAX; i++) {
		styles[i] = (CellStyle) {
			.fg = CELL_COLOR_DEFAULT,
			.bg = CELL_COLOR_DEFAULT,
			.attr = CELL_ATTR_NORMAL,
		};
	}
	styles[UI_STYLE_CURSOR].attr |= CELL_ATTR_REVERSE;
	styles[UI_STYLE_CURSOR_PRIMARY].attr |= CELL_ATTR_REVERSE|CELL_ATTR_BLINK;
	styles[UI_STYLE_SELECTION].attr |= CELL_ATTR_REVERSE;
	styles[UI_STYLE_COLOR_COLUMN].attr |= CELL_ATTR_REVERSE;
	styles[UI_STYLE_STATUS].attr |= CELL_ATTR_REVERSE;
	styles[UI_STYLE_STATUS_FOCUSED].attr |= CELL_ATTR_REVERSE|CELL_ATTR_BOLD;
	styles[UI_STYLE_INFO].attr |= CELL_ATTR_BOLD;
	vsm.styles = styles;
}

void ui_exit() {
	endwin();
}


int main() {
	vsm_init();
	ui_init();
	ui_resize();
	ui_info("example");
	ui_draw();
	getch();
	ui_exit();
}
