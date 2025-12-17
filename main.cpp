//==========================================================================================================
/**** The Header files ****/
//==========================================================================================================

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

enum cursor_movement
{
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

//==========================================================================================================
/**** Data ****/
//==========================================================================================================

class editor_row
{
  public:
    int size;
    char *chars;
};

class Editor_config
{
  public:
    int cursor_x, cursor_y; // the x and y coordinates of the cursor
    int screen_rows;
    int screen_cols;
    int num_rows;
    editor_row row;

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
/**** File I/O ****/
//==========================================================================================================

void editorOpen(std::string& file_name) 
{
  std::ifstream file (file_name);

  if(!file.is_open())
  {
    throw std::runtime_error(std::string("File Read Error:") + std::strerror(errno));
  }

  std::string line;
  ssize_t linelen;
  size_t linecap = 0;

  std::getline(file, line);
  linelen = line.size();
  
  if (linelen != -1) 
  {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
    {
      linelen--;
    }
  
    config.row.size = linelen;

    config.row.chars = new char[linelen + 1];
    std::memcpy(config.row.chars, line.c_str(), linelen + 1);

    config.row.chars[linelen] = '\0';

    config.num_rows = 1;

  }

  file.close();
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
  switch (key)
  {
   case ARROW_LEFT:
      if (config.cursor_x != 0)
      {
        config.cursor_x--;
      }
      break;

    case ARROW_RIGHT:
      if (config.cursor_x != (config.screen_cols -1))
      {
        config.cursor_x++;
      }
      break;

    case ARROW_UP:
      if (config.cursor_y != 0)
      {
        config.cursor_y--;
      }
      break;

    case ARROW_DOWN:
      if (config.cursor_y != (config.screen_rows -1))
      {
        config.cursor_y++;
      }
      break;
  }
}

void editorProcessKeypress() // editorProcessKeypress() waits for a keypress, and then handles it.
{
  int c = editorReadKey();

  switch (c)
  {

    case CTRL_KEY('q'):
        // To clear the screen
      write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the terminal
      write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
      
      exit(0);
      break;

    // Home/End Key operations
    case HOME_KEY:
      config.cursor_x = 0;  // move the cursor at the beginning of the line
      break;

    case END_KEY:
      config.cursor_x = config.screen_cols - 1; // move the cursor at the end of the line
      break;


    // Page up/down operations 
    case PAGE_UP:
    case PAGE_DOWN:
      {
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
    if(y >= config.num_rows)
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
    }
    else 
    {
      int len = config.row.size;
      if (len > config.screen_cols) len = config.screen_cols;
      ab->append(config.row.chars); 
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
  config.num_rows = 0;

  if (getWindowSize(&config.screen_rows, &config.screen_cols) == -1) {throw std::runtime_error(std::string("Read error:") + std::strerror(errno));}
}

int main(int argc, char *argv[]) 
{
  try 
  {
    enter_raw_mode();
    initEditor();
    
    if (argc >= 2) 
    {
      std::string file_name (argv[1]);
      editorOpen(file_name);
    }
    
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
