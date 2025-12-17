//==========================================================================================================
/**** The Header files ****/
//==========================================================================================================
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <cstdlib>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <stdexcept>


//==========================================================================================================
/**** Declarations ****/
//==========================================================================================================

#define CTRL_KEY(k) ((k) & 0x1f) // Define Ctrl+<anyKey> to be 00011111 (which behaves on terminal as ctrl + <anykey>)

#define VERSION  "1.0"
#define TAB_SIZE 8
#define QUIT_TIMES 3

enum cursor_movement
{
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_DOWN,
  ARROW_UP,
  ARROW_RIGHT,
  DEL_KEY,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY
};

//==========================================================================================================
/**** Forward Declarations ***/
//==========================================================================================================

void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);

//==========================================================================================================
/**** Data ****/
//==========================================================================================================

class editor_row
{
  public:
    int size;
    int r_size;
    char *chars;
    char *render;
};

class Editor_config
{
  public:
    int cursor_x, cursor_y; // the x and y coordinates of the cursor
    int row_offset;         
    int col_offset;          
    int screen_rows;
    int screen_cols;
    int num_rows;
    char* filename;
    char statusmsg[80];
    time_t statusmsg_time;
    editor_row *row;
    int changes;

    termios og_termios;   // an object of the class "termios"
};

Editor_config config; // global object for the editor config


//==========================================================================================================
/**** The Operations on the terminal ****/
//==========================================================================================================
void exit_raw_mode()
{
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &config.og_termios) == -1) 
  {
    std::cerr << "tcsetattr error: " << std::strerror(errno) << std::endl;
      // To clear the screen
    write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the terminal
    write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
    exit(1);
  }
}


void enter_raw_mode()
{
  //Read the terminal Attributes
  if( tcgetattr(STDIN_FILENO, &config.og_termios) == -1){throw std::runtime_error(std::string("tcsetattr error: ") + std::strerror(errno));}

  atexit(exit_raw_mode); // tell the compiler beforehand to execute exit_raw_mode() before quiting the program.

  termios raw = config.og_termios; // copy the original terminal attributes into a new termios object "raw", so that the original attributes don't get disturbed when the program calls exit_og_termios_mode before quitting the program.
  
  raw.c_iflag &= ~(ICRNL | IXON);                          // Turn off the Ctrl+S, Ctrl+Q
  raw.c_oflag &= ~(OPOST);                                 // Turn off Ctrl+V
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN) ;        // AND the ECHO bits with the inverted bits

  raw.c_cc[VMIN] = 0; // minimun characters to be read by read()
  raw.c_cc[VTIME] = 1; // 0.1 secs // The time read() waits to read a character      
                       
// set the new terminal attributes to raw - aka - the struct termios
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {throw std::runtime_error(std::string("tcsetattr error: ") + std::strerror(errno));}
}


int editorReadKey()  //editorReadKey()’s job is to wait for one keypress, and return it
{
  int nread; // the value returned by read()
  char c;    // The character entered by the user
             
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) 
  {
    if (nread == -1 && errno != EAGAIN) {throw std::runtime_error(std::string("Read error:") + std::strerror(errno));}
;
  }
  if (c == '\x1b') 
  {
    char seq[3];
    
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') 
    {      
      if (seq[1] >= '0' && seq[1] <= '9')       // check if the entered sequence represents the pageup/down key
      {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        
        if (seq[2] == '~') 
        {
          switch (seq[1]) 
          {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      }

      else
      {
        switch (seq[1])                      // if not the above, then check if the sequence represents an arrow key
        {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
      return '\x1b';
    }
    else if (seq[0] == 'O') 
    {
      switch (seq[1]) 
      {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }
  }  
    return c;
}

int getWindowSize(int *rows, int *cols) 
{
  winsize ws; // Declaration of an object "ws" of the struct "winsize"

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) 
  {
    return -1;
  }
  else
  {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

//==========================================================================================================
/**** Row Operations ****/
//==========================================================================================================

void editorUpdateRow(editor_row *row) 
{
  int tabs = 0;
  int j;

  for (j = 0; j < row->size; j++)
  { if (row->chars[j] == '\t') tabs++; } // read the number of tabs in the row

  free(row->render);
  row->render = (char *) malloc(row->size + tabs*(TAB_SIZE - 1) + 1);  // alloc the render the size of the row plus <no. of tabs>*<spaces for tab> plus \0
 
  int idx = 0;
  for (j = 0; j < row->size; j++) 
  {
    if (row->chars[j] == '\t') 
    {
      row->render[idx++] = ' ';
      while (idx % TAB_SIZE != 0) {row->render[idx++] = ' ';} // keep printing " " unless the idx doesn't become the multiple of 8 (aka 8 spaces)
    }
    else
    {
      row->render[idx++] = row->chars[j]; // copy the contents of row.chars into row.render
    }
  }
  row->render[idx] = '\0';
  row->r_size = idx;
}

void editor_insert_row(int at, std::string& s, size_t len)
{
  if (at < 0 || at > config.num_rows) {return;}

  config.row = (editor_row*) realloc(config.row, sizeof(editor_row) * (config.num_rows + 1));
  memmove(&config.row[at + 1], &config.row[at], sizeof(editor_row) * (config.num_rows - at)); 

  config.row[at].size = len;

  config.row[at].chars = new char[len + 1];
  std::memcpy(config.row[at].chars, s.c_str(), len + 1);

  config.row[at].chars[len] = '\0';

  config.row[at].r_size = 0;
  config.row[at].render = NULL;
  editorUpdateRow(&config.row[at]);
  
  config.num_rows++;

  config.changes++;

}

void editorRowInsertChar (editor_row *row, int at, int c)  // the row where to put char, at what index, the char to be inserted
{
  if (at < 0 || at > row->size) {at = row->size;} 

  row->chars = (char *)realloc(row->chars, row->size + 2);
  
  // to take the chars in the row from "at" and paste them to "at +1" || move the char from where the cursor is rn, one char ahead
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1); 

  row->size++; // increase the size of the row by one char
  row->chars[at] = c; // insert the char at the place of the cursor
  editorUpdateRow(row);
  
  config.changes++;
}

void editorRowDelChar(editor_row *row, int at) 
{
  if (at < 0 || at >= row->size) {return;}

  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);

  config.changes++;
}

//==========================================================================================================
/**** Editor Operations ****/
//==========================================================================================================

void editorInsertChar(int c) 
{
  if (config.cursor_y == config.num_rows)  // if the cursor's at the end of the line 
  {
    std::string null_str = "";
    editor_insert_row(config.num_rows, null_str, 0);
  }
  editorRowInsertChar(&config.row[config.cursor_y], config.cursor_x, c);
  config.cursor_x++;
}

void editorDelChar()
{
  if (config.cursor_y == config.num_rows) {return;}
  
  editor_row *row = &config.row[config.cursor_y];
  if (config.cursor_x > 0)
  {
    editorRowDelChar(row, config.cursor_x - 1);
    config.cursor_x--;
  }
}

//==========================================================================================================
/**** File I/O ****/
//==========================================================================================================

void editorOpen(std::string& filename) 
{  
  free(config.filename);
  config.filename = strdup(filename.c_str());  // duplicate the filename from the function argument to the config

  std::ifstream file (filename);

  if(!file.is_open())
  {
    throw std::runtime_error(std::string("File Read Error:") + std::strerror(errno));
  }

  std::string line;
  ssize_t linelen = 0 ;
  size_t linecap = 0;

  while (std::getline(file, line))
  {
    linelen = line.size();
  
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
    {
      linelen--;
    }
    editor_insert_row(config.num_rows, line, linelen);

  }

  file.close();

  config.changes = 0;
}


char *editorRowsToString(int *buffer_len)  // reads the row of the files and converts them into strings
{
  int total_len = 0;
  int j;
  for (j = 0; j < config.num_rows; j++)
  {
    total_len += config.row[j].size + 1;
  }

  *buffer_len = total_len;
  char *buf = (char *) malloc(total_len);
  char *p = buf;

  for (j = 0; j < config.num_rows; j++) 
  {
    memcpy(p, config.row[j].chars, config.row[j].size);
    p += config.row[j].size; 
    *p = '\n';
    p++;
  }
  return buf;
}


void editorSave()
{
  if (config.filename == NULL) return;

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(config.filename, O_RDWR | O_CREAT, 0644);  // open the <filename> with read/write perms, or create it, if it doesn't' exit
  if (fd != -1) 
  {
    if (ftruncate(fd, len) != -1) 
    {
      if (write(fd, buf, len) == len) 
      {
        close(fd);
        free(buf);

        config.changes = 0;

        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
  free(buf);
}


//==========================================================================================================
/**** AppendBuffer class (Custom String) ****/
//==========================================================================================================

class AppendBuffer {
private:
    std::string buffer;

public:
    void append(std::string_view s) {
        buffer.append(s);                   // calls the std::string::append function to append the string
    }
    
    const char* data() const { return buffer.c_str(); } // returns the C++-style string as a C-style string
    size_t length() const { return buffer.size(); }     // return the string size
    
};


//==========================================================================================================
/**** Input Operations ****/
//==========================================================================================================

void editorMoveCursor(int key) 
{
  editor_row *row = (config.cursor_y >= config.num_rows) ? NULL : &config.row[config.cursor_y];
  switch (key)
  {
    case ARROW_LEFT:
      if (config.cursor_x != 0)
      {
        config.cursor_x--;
      }
      else if (config.cursor_y > 0)  // when the right arrow is pressed at the far-left character of a row, move the cursor to the far-right of the row above 
      {
        config.cursor_y--;
        config.cursor_x = config.row[config.cursor_y].size;
      }
      break;

    case ARROW_RIGHT:
      if (row && config.cursor_x < row->size) 
      {       
        config.cursor_x++;
      }
      else if (row && config.cursor_x == row->size) // move the cursor to the beginning of the next line when right arrow is press at the end of the currect line 
      {
        config.cursor_y++;
        config.cursor_x = 0;
      }
      break;

    case ARROW_UP:
      if (config.cursor_y != 0)
      {
        config.cursor_y--;
      }
      break;

    case ARROW_DOWN:
      if (config.cursor_y < config.num_rows)  // if the current y position of the cursor is less than the num of line present in the file that's to be read, meaning that the body will be executed whenever there's still any line left in the file.
      {
        config.cursor_y++;
      }
      break;
  }

  // don't let the user move past the last character of each line
  row = (config.cursor_y >= config.num_rows) ? NULL : &config.row[config.cursor_y];
  int row_length = row ? row->size : 0; // if the "row" isn't null
  if (config.cursor_x > row_length) 
  {
    config.cursor_x = row_length;
  }
}

void editorProcessKeypress() // editorProcessKeypress() waits for a keypress, and then handles it.
{
  static int quit_times = QUIT_TIMES;

  int c = editorReadKey();

  switch (c)
  {
    case '\r':
      // todo;;;
      break;


    case CTRL_KEY('q'):
      if (config.changes && quit_times > 0) {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      } 
             // To clear the screen
      write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the terminal
      write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
      
      exit(0);
      break;

    case CTRL_KEY('s'):
      editorSave();
      break;

    // Home/End Key operations
    case HOME_KEY:
      config.cursor_x = 0;  // move the cursor at the beginning of the line
      break;

    case END_KEY:
      if (config.cursor_y < config.num_rows)
        config.cursor_x = config.row[config.cursor_y].size;
      config.cursor_x = config.screen_cols - 1; // move the cursor at the end of the line
      break;

    // backspace/del operations
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) {editorMoveCursor(ARROW_RIGHT);}
      editorDelChar();
      break;



    // Page up/down operations 
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) 
        {
          config.cursor_y = config.row_offset;
        } else if (c == PAGE_DOWN) 
        {
          config.cursor_y = config.row_offset + config.screen_rows - 1;
          if (config.cursor_y > config.num_rows) config.cursor_y = config.num_rows;
        }

        int times = config.screen_rows;
        while (times--)                              // While as long as we don't reach the top of the screen
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN); //Keep running the arrow_up key, if the character read was page up
      }
      break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
      editorMoveCursor(c);
      break;


    // for ctrl+l and an escape sequence
    case CTRL_KEY('l'):
    case '\x1b':
      break;

    // print characters like a normal texteditor ahhhhhhhhhhhh
    default:
      editorInsertChar(c);
      break;
  }

  quit_times = QUIT_TIMES;
}


//==========================================================================================================
/*** Output Operations ***/
//==========================================================================================================
void editorScroll() 
{
  if (config.cursor_y < config.row_offset)    // whenever the cursor is above the rowoffset
  {
    config.row_offset = config.cursor_y;      // set offset to the row where the cursor is right now (aka go back)
  }

  if (config.cursor_y >= config.row_offset + config.screen_rows)   // if the cursor is at the last visible line of the editor
  {
    config.row_offset = config.cursor_y - config.screen_rows + 1;  // set row offset to one plus the difference between the where the cursor is right now and the height of the screen
  }
  if (config.cursor_x < config.col_offset) 
  {
    config.col_offset = config.cursor_x;
  }
  if (config.cursor_x >= config.col_offset + config.screen_cols) 
  {
    config.col_offset = config.cursor_x - config.screen_cols + 1;
  }
}



void editorDrawRows(AppendBuffer *ab)  // The rows of tildes
{
  int y;

  for (y = 0; y < config.screen_rows; y++) 
  {
    int file_row  = y + config.row_offset;
    if(file_row >= config.num_rows)
    {

      if(config.num_rows == 0 && y == config.screen_rows / 3)  // when we've read NO lines and the "y" is exactly at the 1/3 of the terminal's height
      {
        char welcome [50];
        int welcome_length = snprintf(welcome, sizeof(welcome), "Text editor -- version %s", VERSION);


        if(welcome_length > config.screen_cols) { welcome_length = config.screen_cols; } // When the welcome message is too long for some screen.
     
        int padding = (config.screen_cols - welcome_length) / 2;
        if (padding)
        {
          ab->append("~");
          padding--;
        }
        while (padding--) { ab->append(" "); }

        ab->append(welcome);
      }
      else
      {
        ab->append("~");
      }
    }
    else 
    {
      int len = config.row[file_row].r_size - config.col_offset;
      if (len < 0) {len = 0;}
      if (len > config.screen_cols) len = config.screen_cols;
      ab->append(std::string_view(config.row[file_row].render + config.col_offset, len)); 
    }


    ab->append("\x1b[K"); // erarse each line before painting
    
      ab->append("\r\n");
  }
}

void editorDrawStatusBar(AppendBuffer *ab) 
{
  ab->append("\x1b[7m"); // invert the colors (from w on b to b on w)
                         
  char status[80], rstatus[80];

  // put the filename (if there's any) on the status bar and the number of line in that file && if the file has been mod'd or not
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", config.filename ? config.filename : "[No Name]", config.num_rows, config.changes ? "(modified)" : "");

  // show: the row where the cursor is rn>/<total num of rows>
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", config.cursor_y + 1, config.num_rows);

  if (len > config.screen_cols) {len = config.screen_cols;}
    
  ab->append(status);

  while (len < config.screen_cols)  
  {
    if (config.screen_cols - len == rlen) // do this while there is space for rstatus to be put out at the status bar 
    {
      ab->append(rstatus);
      break;
    }
    else 
    {
      ab->append(" ");
      len++;
    }
  }
  ab->append("\x1b[m");  // get the normal colors back
  ab->append("\r\n"); // add a new line (to print status messages)
}


void editorDrawMessageBar(AppendBuffer *ab) 
{
  ab->append("\x1b[K");

  int msglen = strlen(config.statusmsg);

  if (msglen > config.screen_cols) {msglen = config.screen_cols;}
  if (msglen && time(NULL) - config.statusmsg_time < 5)
    ab->append(config.statusmsg);
}

void editorRefreshScreen() 
{
  editorScroll();

  AppendBuffer ab;

  ab.append("\x1b[?25l"); // hides the cursor
  ab.append("\x1b[H");  // Moves the cursor at the top-left of the terminal
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);
  
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (config.cursor_y - config.row_offset) + 1, (config.cursor_x - config.row_offset) + 1);
  ab.append(buf);

  ab.append("\x1b[?25h"); // shows the cursor
  write(STDOUT_FILENO, ab.data(), ab.length());
}

void editorSetStatusMessage(const char *fmt, ...) 
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(config.statusmsg, sizeof(config.statusmsg), fmt, ap);
  va_end(ap);
  config.statusmsg_time = time(NULL);
}
//==========================================================================================================
// terminal init
//==========================================================================================================

void initEditor() 
{
  // set the x and y coordinates to 0,0
  config.cursor_x = 0; 
  config.cursor_y = 0;
  config.row_offset = 0; // so that we start off at the first row 
  config.col_offset = 0; // so that we start off at the first col 
  config.num_rows = 0;
  config.row = NULL;
  config.filename = NULL;
  config.statusmsg[0] = '\0';
  config.statusmsg_time = 0;
  config.changes = 0;

  if (getWindowSize(&config.screen_rows, &config.screen_cols) == -1) {throw std::runtime_error(std::string("Read error:") + std::strerror(errno));}
  config.screen_rows -= 2;
}

int main(int argc, char *argv[]) 
{
  try 
  {
    enter_raw_mode();
    initEditor();
    if (argc >= 2) 
    {
      std::string filename (argv[1]);
      editorOpen(filename);
    }
    
    editorSetStatusMessage("Controls: Ctrl-S = Save | Ctrl-Q = quit");
    
    while (1) // To run infinitely until read() returns 0 (aka timeruns out)
    {
      editorRefreshScreen();
      editorProcessKeypress(); 
    }
  }
  catch(const std::exception &error)
  {
      // To clear the screen
    write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the terminal
    write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
    
    std::cerr << error.what() << std::endl;
    
    exit(1);
  }

  return 0;
}
