//==========================================================================================================
/**** The Header files ****/
//==========================================================================================================

#include <unistd.h>
#include <cstdlib>
#include <sys/ioctl.h>
#include <termios.h>
#include <iostream>
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

enum cursor_movement
{
  ARROW_LEFT = 'a';
  ARROW_DOWN = 's';
  ARROW_UP = 'w';
  ARROW_RIGHT = 'd';
};

//==========================================================================================================
/**** Forward Declarations ***/
//==========================================================================================================

void editorRefreshScreen();

//==========================================================================================================
/**** The Operations on the terminal ****/
//==========================================================================================================

class Editor_config
{
  public:
    int cursor_x, cursor_y; // the x and y coordinates of the cursor
    int screen_rows;
    int screen_cols;

    termios og_termios;   // an object of the class "termios"
};

Editor_config config; // global object for the editor config

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


char editorReadKey()  //editorReadKey()’s job is to wait for one keypress, and return it
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
      switch (seq[1]) 
      {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
      }
    }
    return '\x1b';
  } 
  else
  {
    return c;
  }
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

void editorMoveCursor(char key) {
  switch (key) {
    case ARROW_LEFT:
      config.cursor_x--;
      break;
    case ARROW_RIGHT:
      config.cursor_x++;
      break;
    case ARROW_UP:
      config.cursor_y--;
      break;
    case ARROW_DOWN:
      config.cursor_y++;
      break;
  }
}

void editorProcessKeypress() // editorProcessKeypress() waits for a keypress, and then handles it.
{
  char c = editorReadKey();
  switch (c) {
    case CTRL_KEY('q'):
        // To clear the screen
      write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the terminal
      write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
      
      exit(0);
      break;
    
    case 'w':
    case 's':
    case 'a':
    case 'd':
      editorMoveCursor(c);
      break;
  }
}


//==========================================================================================================
/*** Output Operations ***/
//==========================================================================================================

void editorDrawRows(AppendBuffer *ab)  // The rows of tildes
{
  int y;
  for (y = 0; y < config.screen_rows; y++) 
  {
    if(y == config.screen_rows / 3)  // when the "y" is exactly at the 1/3 of the terminal's height
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

    ab->append("\x1b[K"); // erarse each line before painting
    
    if (y < config.screen_rows - 1) 
    {
      ab->append("\r\n");
    }
  }
}

void editorRefreshScreen() 
{
  AppendBuffer ab;

  ab.append("\x1b[?25l"); // hides the cursor
  ab.append("\x1b[H");  // Moves the cursor at the top-left of the terminal
  editorDrawRows(&ab);
  
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", config.cursor_y + 1, config.cursor_x + 1);
  ab.append(buf);

  ab.append("\x1b[?25h"); // shows the cursor
  write(STDOUT_FILENO, ab.data(), ab.length());
}

//==========================================================================================================
// terminal init
//==========================================================================================================

void initEditor() 
{
  // set the x and y coordinates to 0,0
  config.cursor_x = 0; 
  config.cursor_y = 0;

  if (getWindowSize(&config.screen_rows, &config.screen_cols) == -1) {throw std::runtime_error(std::string("Read error:") + std::strerror(errno));}
}

int main() 
{
  try 
  {
    enter_raw_mode();
    initEditor();
    
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
