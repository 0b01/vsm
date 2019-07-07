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
#include <uchar.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "text.h"
#include "text-motions.h"
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
	if (vsm.info[0])
		ui_draw_string(0, vsm.height-1, vsm.info, UI_STYLE_INFO);
	ui_blit();
}

void vsm_info(const char *msg, ...) {
	ui_draw_line(0, vsm.height-1, ' ', UI_STYLE_INFO);
	va_list args;
	va_start(args, msg);
	vsnprintf(vsm.info, sizeof(vsm.info), msg, args);
	va_end (args);
}

void vsm_init() {
	size_t styles_size = UI_STYLE_MAX * sizeof(CellStyle);
	CellStyle* styles = malloc(styles_size);
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

	vsm.view.text = text_load("src/vsm.c");
	vsm.view.off_y = 1;
	vsm.view.pos = 0;
	vsm.view.tabwidth = 4;
}

void ui_exit() {
	endwin();
}

// bool view_coord_get(View *view, size_t pos, Line **retline, int *retrow, int *retcol) {
// 	int row = 0, col = 0;
// 	size_t cur = view->start;
// 	Line *line = view->topline;

// 	if (pos < view->start || pos > view->end) {
// 		if (retline) *retline = NULL;
// 		if (retrow) *retrow = -1;
// 		if (retcol) *retcol = -1;
// 		return false;
// 	}

// 	while (line && line != view->lastline && cur < pos) {
// 		if (cur + line->len > pos)
// 			break;
// 		cur += line->len;
// 		line = line->next;
// 		row++;
// 	}

// 	if (line) {
// 		int max_col = MIN(view->width, line->width);
// 		while (cur < pos && col < max_col) {
// 			cur += line->cells[col].len;
// 			/* skip over columns occupied by the same character */
// 			while (++col < max_col && line->cells[col].len == 0);
// 		}
// 	} else {
// 		line = view->bottomline;
// 		row = view->height - 1;
// 	}

// 	if (retline) *retline = line;
// 	if (retrow) *retrow = row;
// 	if (retcol) *retcol = col;
// 	return true;
// }

void vsm_draw() {
	size_t linepos = text_pos_by_lineno(vsm.view.text, vsm.view.off_y);
	/* read a screenful of text considering each character as 4-byte UTF character*/
	const size_t size = vsm.width * vsm.height * 4;
	/* current buffer to work with */
	char text[size+1];
	/* remaining bytes to process in buffer */
	size_t rem = text_bytes_get(vsm.view.text, linepos, size, text);
	/* NUL terminate text section */
	text[rem] = '\0';

	char* hd = text;

	int x = 0;
	int y = 0;

	Cell (*cells)[vsm.width] = (void*)vsm.cells;
	const size_t cell_size = sizeof(cells[0][0].data)-1;
	for (const char *next = hd; *hd && x < vsm.width && y < vsm.height - 1; hd = next, linepos++) {
		CellStyle style = vsm.styles[UI_STYLE_LEXER_MAX];
		if (linepos == vsm.view.pos) {
			style = vsm.styles[UI_STYLE_CURSOR];
		}
		do next++; while (!ISUTF8(*next));
		size_t len = next - hd;
		if (!len)
			break;
		len = MIN(len, cell_size);
		if (*hd == '\n') {
			// draw a space
			strncpy(cells[y][x].data, " ", len);
			cells[y][x].style = style;
			x++;
			// then draw newline
			strncpy(cells[y][x].data, hd, len);
			cells[y][x].data[len] = '\0';
			cells[y][x].style = style;
			y++;
			x=0;
			continue;
		}
		if (*hd == '\t') {
			int width = vsm.view.tabwidth - (x % vsm.view.tabwidth);
			for (int i = 0; i < width; i++) {
				strncpy(cells[y][x].data, hd, len);
				cells[y][x++].style = style;
			}
			continue;
		}

		strncpy(cells[y][x].data, hd, len);
		cells[y][x].data[len] = '\0';
		cells[y][x].style = style;
		x++;
	}

	// size_t pos = text_mark_get(vsm.view.text, vsm.view.pos);
	// if (!view_coord_get(view, pos, &s->line, &s->row, &s->col) &&
	// 	s == view->selection) {
	// 	s->line = view->topline;
	// 	s->row = 0;
	// 	s->col = 0;
	// }
}

void ui_clear() {
	erase();
}

void view_line_down(View *view) {
	int width = text_line_width_get(view->text, view->pos) + 1;
	size_t next = text_line_next(view->text, view->pos);
	view->pos = text_line_width_set(view->text, next, width);
}
void view_line_up(View *view) {
	int width = text_line_width_get(view->text, view->pos) + 1;
	size_t prev = text_line_prev(view->text, view->pos);
	view->pos = text_line_width_set(view->text, prev, width);
}
void view_char_prev(View *view) {
	view->pos = text_char_prev(view->text, view->pos);
}
void view_char_next(View *view) {
	view->pos = text_char_next(view->text, view->pos);
}
void view_longword_end_next(View *view) {
	view->pos = text_customword_end_next(view->text, view->pos, isspace);
}
void view_e(View* view) {
	view->pos = text_customword_end_next(view->text, view->pos, is_word_boundary);
}
void view_E(View* view) {
	view->pos = text_customword_end_next(view->text, view->pos, isspace);
}
void view_b(View* view) {
	view->pos = text_customword_start_prev(view->text, view->pos, is_word_boundary);
}
void view_B(View* view) {
	view->pos = text_customword_start_prev(view->text, view->pos, isspace);
}
void view_w(View* view) {
	view->pos = text_customword_start_next(view->text, view->pos, is_word_boundary);
}
void view_W(View* view) {
	view->pos = text_customword_start_next(view->text, view->pos, isspace);
}
void view_line_begin(View* view) {
	view->pos = text_line_begin(view->text, view->pos);
}
void view_line_end(View* view) {
	view->pos = text_line_end(view->text, view->pos);
}
void view_para_prev(View* view) {
	view->pos = text_paragraph_prev(view->text, view->pos);
}
void view_para_next(View* view) {
	view->pos = text_paragraph_next(view->text, view->pos);
}


int main() {
	vsm_init();
	ui_init();
	ui_resize();

	char ch;
	for(;;) {
		ui_clear();
		memset(vsm.cells, 0, vsm.cells_size);
		vsm_info("example %d", vsm.view.off_y);
		vsm_draw();
		ui_draw();
		ch = getch();
		switch (ch) {
			case 'j':
				view_line_down(&vsm.view);
				break;
			case 'k':
				view_line_up(&vsm.view);
				break;
			case 'h':
				view_char_prev(&vsm.view);
				break;
			case 'l':
				view_char_next(&vsm.view);
				break;
			case 'e':
				view_e(&vsm.view);
				break;
			case 'E':
				view_E(&vsm.view);
				break;
			case 'b':
				view_b(&vsm.view);
				break;
			case 'B':
				view_B(&vsm.view);
				break;
			case 'w':
				view_w(&vsm.view);
				break;
			case 'W':
				view_W(&vsm.view);
				break;
			case '0':
				view_line_begin(&vsm.view);
				break;
			case '$':
				view_line_end(&vsm.view);
				break;
			case '{':
				view_para_prev(&vsm.view);
				break;
			case '}':
				view_para_next(&vsm.view);
				break;
			case 'q':
				goto exit;
		}
	}
exit:
	ui_exit();
}
