/* some flags for getline*/
#define _BSD_SOURCE
#define _GNU_SOURCE

#include<unistd.h>
#include<termios.h>
#include<stdlib.h>
#include<stdio.h>
#include<errno.h>
#include<sys/ioctl.h>
#include<string.h>

#define Ctrl(k) ((k)&0x1F)
const char *TEXOR_VER = "0.0.1";
int SCROLL_UP = 0;
int SCROLL_DOWN = 0;
int SCROLL_LEFT = 0;;
int SCROLL_RIGHT = 0;

typedef struct editor_row{
    int size;
    char *str;
    int rsize;
    char *render;
}editor_row;

typedef struct EditorConfig{
    int x_pos; // column position
    int y_pos; // row position
    int screenRows;
    int  screenColumns;
    int num_rows;
    editor_row *erow;
    struct termios orig_mode;
}EditorConfig;

EditorConfig E;

typedef struct ap_str{
    char *ptr;
    int len;
}ap_str;

#define AS_INIT {NULL, 0}
#define TAB_SIZE 8

enum{
    ARROW_UP = 1000,
    ARROW_DOWN,
    ARROW_LEFT,
    ARROW_RIGHT,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KY,
    END_KY,
    DEL_KY,
};

int append_string_alloc(ap_str *as, const char *str, int len){
    char *new_ptr = realloc(as->ptr, as->len + len);
    if(new_ptr == NULL){
        return -1;
    }
    as->ptr = new_ptr;
    memcpy(as->ptr + as->len, str, len);
    as->len = as->len + len;
    return 0;
}

void append_string_free(ap_str *as){
    free(as->ptr);
    as->len = 0;
    return;
}

void handle_error(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[1;1H", 6);
    perror(s);
    exit(1);
}

void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_mode) == -1){ // TCSAFLUSH -> discard any left over characters in stdin and stdout 
        handle_error("tcsetattr");
    }
   // printf("Number of rows: %d and number of columns: %d", E.screenRows, E.screenColumns);
    return;
}

void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &E.orig_mode) == -1){
        handle_error("tcgetattr");
    }
    struct termios raw_mode = E.orig_mode;
    raw_mode.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    /*
     ECHO -> to turn off echo mode
     ICANON -> to turn of canonical mode
     ISIG -> to handle ctrl-c and ctrl-z
     IEXTEN -> to handle ctrl-v
     */
    raw_mode.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    /*
     IXON -> to handle ctrl-s and ctrl-q
     ICRNL -> to handle ctrl-m
     BRKINT, INPCK, ISTRIP -> miscellaneous flags
     */
    raw_mode.c_oflag &= ~(OPOST);
    /*
     OPOST -> turn of so \n will remain \n not treated as \r\n
     */
    raw_mode.c_cflag |= (CS8);
    /*
     CS8 -> turn off so the maximum size of character remains 8 bits 1 byte
     */
    raw_mode.c_cc[VMIN] = 0;
    raw_mode.c_cc[VTIME] = 1;
    /*
     VMIN -> minimum number of bytes taken as input before returning from read
     VTIME -> maximum amount of time taken by read to return
     VTIME is in 1/10 of a second
     */
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode) == -1){
        handle_error("tcsetattr");
    }
    atexit(disableRawMode);
    return;
}

int getCursorPosition(int *rows, int *cols){
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; // n command is used to query terminal to get the status information and 
                                                           // 6 is the argument to get the current cursor position
    char cur_pos[32];
    size_t i=0;
    while(i<sizeof(cur_pos)-1 && read(STDIN_FILENO, &cur_pos[i], 1) == 1){
        if(cur_pos[i] == 'R'){
            break;
        }
        i++;
    }
    cur_pos[i] = '\0';
    if(cur_pos[0] != '\x1b' || cur_pos[1] != '[') return -1;
    if(sscanf(&cur_pos[2] , "%d;%d", rows, cols) != 2) return -1;
    return 0;
}

int getWindowSize(int *rows, int *cols){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0 || ws.ws_row == 0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        if(getCursorPosition(rows, cols) == -1) return -1;
    }
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
}

void editorUpdateRow(editor_row *erow){
    int num_tabs = 0;
    for(int i=0; i<erow->size; ++i){
        if(erow->str[i] == '\t'){
            num_tabs++;
        }
    }
    free(erow->render);
    erow->render = malloc(erow->size + num_tabs*(TAB_SIZE-1) + 1); // 1 tab will occupy 8 bytes currently each tab is occupying 1 byte so we are adding 7 bytes more to every tab
    int idx = 0;
    for(int i=0; i<erow->size; ++i){
        if(erow->str[i] == '\t'){
            erow->render[idx++] = ' ';
            while(idx % TAB_SIZE != 0) erow->render[idx++] = ' ';
        }
        else{
            erow->render[idx++] = erow->str[i];
        }
    }
    erow->render[idx] = '\0';
    erow->rsize = idx;
    return;
}

void editorAppendRow(char *str, int len){
    E.erow = realloc(E.erow, sizeof(editor_row) * (E.num_rows+1));
    E.erow[E.num_rows].size = len;
    E.erow[E.num_rows].str = malloc(len+1);
    memcpy(E.erow[E.num_rows].str, str, len);
    E.erow[E.num_rows].str[len] = '\0';
    E.erow[E.num_rows].rsize = 0;
    E.erow[E.num_rows].render = NULL;
    editorUpdateRow(&E.erow[E.num_rows]);
    return;
}

void editorOpen(char *file_path){
    FILE *fp = fopen(file_path, "r");
    if(fp != NULL){
        char *line_buf = NULL;
        size_t line_cap = 0;
        int line_len = 0;
        while((line_len = getline(&line_buf, &line_cap, fp)) != -1){
            while(line_len >0 && (line_buf[line_len-1] == '\r' || line_buf[line_len-1] == '\n')) line_len--;
            editorAppendRow(line_buf, line_len);
            E.num_rows = E.num_rows + 1;
        }
        free(line_buf);
        fclose(fp);
    }
    return;
}
/*
void str_rev(char *str, int len){
    for(int i=0; i<len/2; ++i){
        char temp = str[i];
        str[i] = str[len-i-1];
        str[len-i-1] = temp;
    }
    return;
}
*/
/*
void editorDrawLineNumber(ap_str *as){
    int idx = 0;
    int  len = 0;
    char str[6];
    for(int i=E.y_pos; i>1; --i){
        append_string_alloc(as, "\x1b[K", 3);
        memset(str, 0, 5);
        idx = i;
        len = 0;
        while(idx > 0 && len < 5){
            str[len] = (idx%10) + '0';
            idx = idx/10;
            len = len + 1;
        }
        str_rev(str, len);
        str[len] = '\0';
        append_string_alloc(as, str, len);
        append_string_alloc(as, "\r\n", 2);
    }
    append_string_alloc(as, "\x1b[K", 3);
    memset(str, 0, 5);
    idx = 1;
    len = 0;
    while(idx>0 && len<5){
        str[len] = (idx%10) + '0';
        idx = idx/10;
        len = len + 1;
    }
    str_rev(str, len);
    str[len] = '\0';
    append_string_alloc(as, str, len);
    append_string_alloc(as, "\r\n", 2);
    for(int i=2; i<=SCROLL_UP-(E.y_pos+1); ++i){
        append_string_alloc(as, "\x1b[K", 3);
        memset(str, 0, 5);
        idx = i;
        len = 0;
        while(idx > 0 && len < 5){
            str[len] = (idx%10) + '0';
            idx = idx/10;
            len = len + 1;
        }
        str_rev(str, len);
        str[len] = '\0';
        append_string_alloc(as, str, len);
        if(i<SCROLL_UP-(E.y_pos-1)){
            append_string_alloc(as, "\r\n", 2);
        }
    }
    return;
}
*/

void editorDrawText(ap_str *as){
    //char str_space[32];
    for(int i=SCROLL_DOWN; i<SCROLL_UP; ++i){
        //snprintf(str_space, sizeof(str_space), "\x1b[%d;%dH", i, 5);
        //append_string_alloc(as, str_space, strlen(str_space));
        append_string_alloc(as, "\x1b[K", 3);
        int len = E.erow[i].rsize;
        if(len >= SCROLL_RIGHT) len = SCROLL_RIGHT;
        if(len > SCROLL_LEFT){
            append_string_alloc(as, E.erow[i].render + SCROLL_LEFT, len-SCROLL_LEFT);
        }
        if(i < SCROLL_UP - 1){
            append_string_alloc(as, "\r\n", 2);
        }
    }
    /*
    int idx = SCROLL_UP - SCROLL_DOWN;
    for(int i=idx; i<E.screenRows; ++i){
        append_string_alloc(as, "\x1b[K", 3);
        if(i<E.screenRows-1){
            append_string_alloc(as, "~\r\n", 3);
        }
        else if(i == E.screenRows-1){
            append_string_alloc(as, "~", 1);
        }
    }
    */
    return;
}

void editorDrawName(ap_str *as){
    for(int i=0; i<E.screenRows; ++i){
        append_string_alloc(as, "\x1b[K", 3); // ersases the line

        // display the welcome message only no file is passed
        if(i == E.screenRows/3){
            char welcome_msg[80];
            int welcome_len = snprintf(welcome_msg, sizeof(welcome_msg), "Welcome to Texor version: %s\r\n", TEXOR_VER);
            if(welcome_len >= E.screenColumns) welcome_len = E.screenColumns;
            int padding = (E.screenColumns - welcome_len)/2;
            if(padding){
                append_string_alloc(as, "~", 1);
            }
            while(padding--) append_string_alloc(as, " ", 1);
            append_string_alloc(as, welcome_msg, welcome_len);
        }
        else if(i<E.screenRows-1){
            append_string_alloc(as, "~\r\n", 3);
        }
        else if(i == E.screenRows-1){
            append_string_alloc(as, "~", 1);
        }
    }
    return;
}

void editorRefreshScreen(){
    ap_str as = AS_INIT;
    append_string_alloc(&as, "\x1b[?25l", 6); // ?25l -> hiding the cursor
    append_string_alloc(&as, "\x1b[1;1H", 6); // moving the cursor to top position
    if(E.num_rows == 0){
        editorDrawName(&as);
    }
    else{
        /*editorDrawLineNumber(&as);
        append_string_alloc(&as, "\x1b[1;1H", 6);*/
        editorDrawText(&as);
    }
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", E.y_pos, E.x_pos);
    append_string_alloc(&as, pos_buf, strlen(pos_buf));
    append_string_alloc(&as, "\x1b[?25h", 6); // ?25h -> making the cursor visible again
    write(STDOUT_FILENO, as.ptr, as.len);
    append_string_free(&as);
    return;
}

void editorMoveCursor(int c){
    switch(c){
        case ARROW_UP:{
                     //up
                     if(E.y_pos == 1){
                         if(SCROLL_DOWN > 0){
                             SCROLL_DOWN--;                                                                                                                                                                                                                                  
                         }
                         if(SCROLL_UP - SCROLL_DOWN > E.screenRows){
                             SCROLL_UP--;
                         }
                     }
                     if(E.y_pos > 1){
                         E.y_pos--;
                     }
                     break;
                 }
        case ARROW_RIGHT:{
                     // right
                     if(E.x_pos == E.screenColumns){
                         if(SCROLL_RIGHT < E.erow[(SCROLL_DOWN + E.y_pos) - 1].rsize){
                             SCROLL_RIGHT++;
                         }
                        if(SCROLL_RIGHT - SCROLL_LEFT > E.screenColumns){
                             SCROLL_LEFT++;
                         }
                     }
                     if(E.x_pos < E.screenColumns && E.x_pos < E.erow[(SCROLL_DOWN + E.y_pos) - 1].rsize){
                         E.x_pos++;
                     }
                     break;
                 }
        case ARROW_DOWN:{
                     // down
                     if(E.y_pos == E.screenRows){
                         if(SCROLL_UP < E.num_rows){
                             SCROLL_UP++;
                         }
                         if(SCROLL_UP - SCROLL_DOWN > E.screenRows){
                             SCROLL_DOWN++;
                         }
                     }
                     if(E.y_pos < E.screenRows && E.y_pos < E.num_rows){
                         E.y_pos++;
                     }
                     break;
                 }
        case ARROW_LEFT:{
                     // left
                     if(E.x_pos == 1){
                         if(SCROLL_LEFT > 0){
                             SCROLL_LEFT--;
                         }
                         if(SCROLL_RIGHT - SCROLL_LEFT > E.screenColumns){
                             SCROLL_RIGHT--;
                         }
                     }
                     if(E.x_pos > 1){
                         E.x_pos--;
                     }
                     break;
                 }
    }
    return;
}

int editorReadKey(){
    ssize_t bytes_read;
    char c;
    while((bytes_read = read(STDIN_FILENO, &c, 1)) != 1){
        if(bytes_read == -1 && errno != EAGAIN){
            handle_error("read");
        }
    }
    if(c == '\x1b'){
        char seq[3];
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if(seq[2] == '~'){
                    switch(seq[1]){
                        case '5':{
                                     return PAGE_UP;
                                 }
                        case '6':{
                                     return PAGE_DOWN;
                                 }
                        case '1':{
                                     return HOME_KY;
                                 }
                        case '7':{
                                     return HOME_KY;
                                 }
                        case '4':{
                                     return END_KY;
                                 }
                        case '8':{
                                     return END_KY;
                                 }
                        case '3':{
                                     return DEL_KY;
                                 }
                        default:{
                                    return '\x1b';
                                }
                    }
                }
                else{
                    return '\x1b';
                }
            }
            else{
                switch(seq[1]){
                    case 'A':{
                                 return ARROW_UP;
                             }
                    case 'B':{
                                 return ARROW_DOWN;
                             }
                    case 'C':{
                                 return ARROW_RIGHT;
                             }
                    case 'D':{
                                 return ARROW_LEFT;
                             }
                    case 'H':{
                                 return HOME_KY;
                             }
                    case 'F':{
                                 return END_KY;
                             }
                    default:{
                                return '\x1b';
                            }
                }
            }
        }
        else if(seq[0] == 'O'){
            switch(seq[1]){
                case 'H':{
                             return HOME_KY;
                         }
                case 'F':{
                             return END_KY;
                         }
                default:{
                            return '\x1b';
                        }
            }
        }
        else{
            return '\x1b';
        }
    }
    else{
        return c;
    }
}

void editorProcessKey(){
    int c = editorReadKey();
    switch (c){
        case Ctrl('q'):{
                           write(STDOUT_FILENO, "\x1b[2J", 4);
                           write(STDOUT_FILENO, "\x1b[1;1H", 6);
                           exit(0);
                           break;
                     }
        case ARROW_UP:{
                     editorMoveCursor(c);
                     break;
                 }
        case ARROW_DOWN:{
                     editorMoveCursor(c);
                     break;
                 }
        case ARROW_RIGHT:{
                     editorMoveCursor(c);
                     break;
                 }
        case ARROW_LEFT:{
                     editorMoveCursor(c);
                     break;
                 }
        case PAGE_UP:{
                         int mov_times = E.screenRows;
                         while(mov_times--){
                             if(SCROLL_DOWN > 0){
                                 SCROLL_DOWN--;
                             }
                             if(SCROLL_UP - SCROLL_DOWN > E.screenRows){
                                 SCROLL_UP--;
                             }
                             if(E.y_pos > 1){
                                 editorMoveCursor(ARROW_UP);
                             }
                         }
                         break;
                     }
        case PAGE_DOWN:{
                           int mov_times = E.screenRows;
                           while(mov_times--){
                               if(SCROLL_UP < E.num_rows){
                                   SCROLL_UP++;
                               }
                               if(SCROLL_UP - SCROLL_DOWN > E.screenRows){
                                   SCROLL_DOWN++;
                               }
                               if(E.y_pos < E.screenRows){
                                   editorMoveCursor(ARROW_DOWN);
                               }
                           }
                           break;
                       }
        case HOME_KY:{
                         E.x_pos = 1;
                         SCROLL_RIGHT = E.screenColumns;
                         SCROLL_LEFT = 0;
                         break;
                     }
        case END_KY:{
                        E.x_pos = E.erow[(SCROLL_DOWN + E.y_pos) - 1].rsize;
                        SCROLL_RIGHT = E.erow[(SCROLL_DOWN + E.y_pos)-1].rsize > E.screenColumns ? E.erow[(SCROLL_DOWN + E.y_pos)-1].rsize : E.screenColumns;
                        SCROLL_LEFT = SCROLL_RIGHT - E.screenColumns > 0 ? SCROLL_RIGHT - E.screenColumns : 0;
                        break;
                    }

    }
    return;
}

void initEditor(){
    E.x_pos = 1;
    E.y_pos = 1;
    E.num_rows = 0;
    E.erow = NULL;
    int res = getWindowSize(&E.screenRows, &E.screenColumns);
    if(res == -1){
        handle_error("get terminal size:");
    }
    return;
}

int main(int argc, char *argv[]){
    enableRawMode();
    initEditor();
    if(argc == 2){
        editorOpen(argv[1]);
    }
    SCROLL_UP = E.num_rows <= E.screenRows ? E.num_rows : E.screenRows;
    SCROLL_RIGHT = E.screenColumns;
    while(1){
      editorRefreshScreen();
      editorProcessKey();
    }
    return 0;
    /*
     escape sequences that I learn so far
     "\x1b[J" -> it clears off the screen. Arguments can
     0 -> It is by default. It clears off the screen from the cursor to the end
     1 -> It clears off the screen from the starting to the cursor
     2 -> It clears off the entire screen

     "\x1[y;xH" -> It moves the cursor to the yth row and xth column but it don't take care of whether the cursor is going out off the screen or not
     "\x1b[K" -> It erases a line. Arguments can
     0 -> It is by default. It deletes the line from the cursor to the end
     1 -> It deletes the line from the beginning to the cursor
     2 -> It deletes the entire line

     "\x1b[?25l" -> It hides the cursor
     "\x1b[?25h" -> It makes the cursor visible again
     "\x1b[xC" -> It moves the cursor x units right
     "\x1b[yB" -> It moves the cursor y units down
     both B and C take care that cursor should not go out off the screen

     "\x1b[n" -> It is used to query the terminal about status information. If the argument is 6 then it is used to find the current position of the cursor

     "\x1b[A" -> ARROW UP
     "\x1b[B" -> ARROW DOWN
     "\x1b[C" -> ARROW RIGHT
     "\x1b[D" -> ARROW LEFT
     "\x1b[5~ -> PAGE UP
     "\x1b[6~ -> PAGE DOWN
     "\x1b[1~", "\x1b[7~", "|x1b[H", "\x1bOH" -> HOME KEY
     "\x1b[4~", "\x1b[8~", \x1b[F", "\x1bOF" -> END KEY
     "\x1b[3~", DELETE KEY
     */
}
