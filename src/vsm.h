#define MAX_WIDTH 1024
#define MAX_HEIGHT 1024
#include <termios.h>

typedef uint8_t CellAttr;

// typedef struct {
// 	uint8_t r, g, b;
// 	uint8_t index;
// } CellColor;

typedef struct {
	CellAttr attr;
	short fg, bg;
} CellStyle;

typedef struct {
	char data[16];      /* utf8 encoded character displayed in this cell (might be more than
	                       one Unicode codepoint. might also not be the same as in the
	                       underlying text, for example tabs get expanded */
	size_t len;         /* number of bytes the character displayed in this cell uses, for
	                       characters which use more than 1 column to display, their length
	                       is stored in the leftmost cell whereas all following cells
	                       occupied by the same character have a length of 0. */
	int width;          /* display width i.e. number of columns occupied by this character */
	CellStyle style;    /* colors and attributes used to display this cell */
} Cell;

typedef struct Vsm {
	int height;
	int width;
    char info[MAX_WIDTH];
	struct termios orig_termios;

	size_t styles_size;
	CellStyle *styles;

	size_t cells_size;
	Cell* cells;
} Vsm;


enum UiStyle {
	UI_STYLE_LEXER_MAX = 64,
	UI_STYLE_DEFAULT,
	UI_STYLE_CURSOR,
	UI_STYLE_CURSOR_PRIMARY,
	UI_STYLE_CURSOR_LINE,
	UI_STYLE_SELECTION,
	UI_STYLE_LINENUMBER,
	UI_STYLE_LINENUMBER_CURSOR,
	UI_STYLE_COLOR_COLUMN,
	UI_STYLE_STATUS,
	UI_STYLE_STATUS_FOCUSED,
	UI_STYLE_SEPARATOR,
	UI_STYLE_INFO,
	UI_STYLE_EOF,
	UI_STYLE_MAX,
};

#define CELL_COLOR_BLACK   COLOR_BLACK
#define CELL_COLOR_RED     COLOR_RED
#define CELL_COLOR_GREEN   COLOR_GREEN
#define CELL_COLOR_YELLOW  COLOR_YELLOW
#define CELL_COLOR_BLUE    COLOR_BLUE
#define CELL_COLOR_MAGENTA COLOR_MAGENTA
#define CELL_COLOR_CYAN    COLOR_CYAN
#define CELL_COLOR_WHITE   COLOR_WHITE
#define CELL_COLOR_DEFAULT (-1)
#define MAX_COLOR_PAIRS MIN(COLOR_PAIRS, 256)

#define CELL_ATTR_NORMAL    A_NORMAL
#define CELL_ATTR_UNDERLINE A_UNDERLINE
#define CELL_ATTR_REVERSE   A_REVERSE
#define CELL_ATTR_BLINK     A_BLINK
#define CELL_ATTR_BOLD      A_BOLD
#define CELL_ATTR_ITALIC    A_ITALIC