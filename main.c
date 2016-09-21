#include "include/tty.h"
#include <assert.h>
#include "screen.h"
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <termios.h>
#ifndef TIOCGWINSZ
#include <sys/ioctl.h>
#endif

#define DEBUG 1
#define EC '~' 					/* Empty Character: indication empty line */


typedef struct line{
	int lineno;
	int len;
	char *content;
} line_t;

#define BUF_SIZE 1000				/* assume normally a file is less than 1000 line */
struct buffer_t{
	line_t lines[BUF_SIZE];
	struct buffer_t *nextbuf;		/* support file more than 1000 line */	
	int append_lineno;			/* next line number to append to buffer */
	int num_lines;				/* number of total lines */
};
char * filebuf;
char * filename;
struct buffer_t buffer;

/*
 * Please make sure buffer.lines[buffer.append_lineno] is reserved for insert
 * if insert new line in the middle of the buffer, pls call shift_line to 
 * shift the lines first , then update the buffer.append_lineno to the desired lineno
 */
void append_line(char *content)
{
	int idx	= buffer.append_lineno - 1;
	line_t *line = &(buffer.lines[idx]);	
	line->lineno = buffer.append_lineno;
	if (0){				/* TODO append_lineno > BUF_SIZE */
		
	}

	int len = strlen(content);
	line->content = (char *)malloc(len+1);
	if( line->content == NULL)
	{
		fprintf(stderr, "not enough memeory!!!");
		exit(1);
	}
	strncpy(line->content, content, len);
	line->content[len+1] = 0;		/* end of a string:0 */
	line->len = len;

}
void
shift_line(int lineno)
{
	int start_idx 	= buffer.num_lines - 1;
	int end_idx	= lineno - 1;
	
	if (buffer.num_lines >= BUF_SIZE)
	{}				/* TODO number of lines >= BUF_size */
	else
	{
		for (int i = start_idx; i >= end_idx; i--)
		{
			buffer.lines[i + 1] = buffer.lines[i];	
		}
		buffer.append_lineno = lineno;		/* temp update append_lineno to the line number to insert */
	}	
}
void
insert_line(int line_no, char *content)
{

	if (line_no == buffer.append_lineno)	/* append*/
	{
		append_line(content);
		buffer.append_lineno++;
		buffer.num_lines++;
	}
	else if (line_no > buffer.append_lineno) /* shoud never occur */
		assert(0);

	else 					/* (line_no < buffer.append_lineno)  insert */
	{
		int append_lineno = buffer.append_lineno;	/* reserve append_lineno */
		shift_line(line_no);
		append_line(content);
		buffer.append_lineno = ++append_lineno;
		buffer.num_lines++;
	}
}
void 
read_file(FILE* fp)
{
	int read;
	int line_no = 1;
	size_t n = 0;
	char *line = NULL;
	while((read = getline(&line, &n, fp) != -1)){   /* Why getline always return 1 ? */
		int len = strlen(line);
		if( line[len-2] == '\r')	/* CRLF sequence */
			line[len-2] = 0;

		line[len-1] = 0;		/* Overwrite '\n' */
		insert_line(line_no++, line);
	} 
	free(line);
	fclose(fp);
}
typedef struct location_t{
	int cx, cy; /* cursor location */
}location_t;
struct screen_t{
	int top_lineno;
	int btn_lineno;
	int cursor_lineno;			/*file line number under the cursor */
	location_t cur_l;
	location_t statusbar_l;
	char status_msg[80];
};
struct screen_t screen;
line_t * get_a_line(int lineno)
{
	int idx = lineno - 1;
	if( lineno < BUF_SIZE){
		return &buffer.lines[idx];
	}
	return NULL;		/*TODO add support for file larger the 1000 line */
}

void move_cursor(int fd, int cy, int cx)
{	
	char c_seq[12];
	memset(c_seq, 0, 12);		/* init control sequence */
	snprintf(c_seq, 10,  "\33[%d;%dH", cy, cx);
	write(fd, c_seq, strlen(c_seq));	


}
void write_status(int fd, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(screen.status_msg, sizeof(screen.status_msg), fmt, ap);
	va_end(ap);
	move_cursor(fd, screen.statusbar_l.cy, screen.statusbar_l.cx);
#define CLR_LINE "\33[0K"
	write(fd, CLR_LINE, strlen(CLR_LINE));
	write(fd, screen.status_msg,  strlen(screen.status_msg));
	move_cursor(fd, screen.cur_l.cy, screen.cur_l.cx);
}
void restore_cursor()
{
	move_cursor(STDIN_FILENO, screen.cur_l.cx, screen.cur_l.cy);
}
#define MAX_LINE_CHAR 1000
void redraw_screen()
{
	char render_line[MAX_LINE_CHAR];
	for (int i = screen.top_lineno; i < screen.btn_lineno -1; i++){
		line_t *line = get_a_line(i);
		int len = 0;
		if( line->content != NULL)
		{
			len = strlen(line->content);		
			strncpy(render_line, line->content, len);
		}else
		{
			len = 1;
			render_line[0] = '~';	
		}
		render_line[len] = '\r';
		render_line[len+1] = '\n';
		render_line[len+2] = '\0';
		write(STDOUT_FILENO, render_line, len+3);
	}	
	restore_cursor();
}
struct winsize wind_sz;
char * 		wind_buf;
char **		wind_lines;
void init_window()
{
	if( ioctl(STDIN_FILENO, TIOCGWINSZ, (char *)&wind_sz) < 0 )
		exit(1);	
	printf("%d rows, %d cols\n", wind_sz.ws_row, wind_sz.ws_col);
	screen.top_lineno = 1;
	screen.btn_lineno = wind_sz.ws_row - 1;		/* reserve buttom line for status line */
	screen.cur_l.cx= screen.cur_l.cy = 0;
	screen.statusbar_l.cy = wind_sz.ws_row;
	screen.statusbar_l.cx = 0;	
		
	char clear_go_home[] = "\33[2J\33[H";
	write(STDOUT_FILENO, clear_go_home , strlen(clear_go_home));	
	//write(STDOUT_FILENO, filebuf, strlen(filebuf));
	redraw_screen();
	
}
enum KEY_ACTION{
        KEY_NULL = 0,       /* NULL */
        CTRL_C = 3,         /* Ctrl-c */
        CTRL_D = 4,         /* Ctrl-d */
        CTRL_F = 6,         /* Ctrl-f */
        CTRL_H = 8,         /* Ctrl-h */
        TAB = 9,            /* Tab */
        CTRL_L = 12,        /* Ctrl+l */
        ENTER = 13,         /* Enter */
        CTRL_Q = 17,        /* Ctrl-q */
        CTRL_S = 19,        /* Ctrl-s */
        CTRL_U = 21,        /* Ctrl-u */
        ESC = 27,           /* Escape */
        BACKSPACE =  127,   /* Backspace */
        /* The following are just soft codes, not really reported by the
         * terminal directly. */
        LEFT = 1000,
        RIGHT,
        UP,
        DOWN,
        DEL_KEY,
        HOME,
        END,
        PAGE_UP,
        PAGE_DOWN
};


char cr[] = "\r\n";
int read_escaped_key(int fd)
{
	int nread;
	char c, seq[3];
	while ((nread = read(STDIN_FILENO, &c, 1)) == 0);
	if (nread == -1 )
		exit(1);

	while(1)
	{
		switch(c){
		case ESC:
			if (read(fd, seq, 1) == 0) return ESC;
			if (read(fd, seq+1, 1) == 0) return ESC;
		
			/* ESC [ sequence */
			if (seq[0] == '['){
				if (seq[1] >= '0' && seq[1] <='9'){
					
				}else{
					switch(seq[1]){
					case 'A': return UP;
					case 'B': return DOWN;
					case 'C': return RIGHT;
					case 'D': return LEFT;
					case 'H': return HOME;
					case 'F': return END;
					}
				}
			}
		case ENTER:
			return ENTER;
		default:
			return c;
		}
				
	}
}
void cursor_down()
{
	if (screen.cursor_lineno == buffer.num_lines)
		return;
	else if (screen.cursor_lineno == screen.btn_lineno)
	{
		screen.btn_lineno++;
		screen.top_lineno--;		
		screen.cursor_lineno++;
		redraw_screen();
		return;
	}			
	screen.cursor_lineno++;
	screen.cur_l.cy++;		/* need to stop increase when reach the max lines of screen */
	/* TODO cursor position x need to change too */
	move_cursor(STDIN_FILENO, screen.cur_l.cy, screen.cur_l.cx);	
}
void do_input()
{
	
	int c = read_escaped_key(STDIN_FILENO);
	switch(c){
	case UP:
		write_status(STDIN_FILENO, "move up a line");
		break;
	case DOWN:
		write_status(STDIN_FILENO, "move down a line");
		cursor_down();
		break;
	case LEFT:
		write_status(STDIN_FILENO, "move left a cursor");
		break;
	case RIGHT:
		write_status(STDIN_FILENO, "move right a cursor");
		break;
	case ESC:
		exit(0);
	default:
		write_status(STDIN_FILENO, "get a character:%d", c);
	}
}
void
init_buffer()
{
	buffer.append_lineno	= 1;
	buffer.num_lines	= 0;
}
int
main(int argc, char *argv[])
{
	FILE *fp;
	if (atexit(tty_atexit) != 0){
		perror("argv[0]");
		exit(1);
	}
	if (argc < 2)
	{
		fprintf(stderr,"%s usage: %s <file>\n", argv[0], argv[0]);
		exit(1);
	}
	filename = argv[1];
	if ((fp = fopen(argv[1], "r")) == NULL) {		/* Only open file for reading here */
		perror(argv[0]);
		exit(1);
	}
	if (tty_raw(STDIN_FILENO) == -1)
	{
		perror(argv[0]);
		printf("error setting raw mode\n");
		exit(1);
	}	
	init_buffer();
	read_file(fp);
	init_window();
	while(1){
		//fresh_screen();
		do_input();
	}
}
