
//==========================================================================================================
/**** The Header files ****/
//==========================================================================================================

#include <unistd.h>
#include <cstdlib>
#include <sys/ioctl.h>
#include <termios.h>
#include <iostream>
#include <cctype>
#include <cstring>
#include <cerrno>
#include <stdexcept>


//==========================================================================================================
/**** Declarations ****/
//==========================================================================================================

#define CTRL_KEY(k) ((k) & 0x1f) // Define Ctrl+<anyKey> to be 00011111 (which behaves on terminal as ctrl) + <anykey>

//==========================================================================================================
/**** Prototypes to be declared before their definition ***/
//==========================================================================================================

void editorRefreshScreen();

//==========================================================================================================
/**** The Operations on the terminal ****/
//==========================================================================================================

class Editor_config
{
  public:
    int screen_rows;
    int screen_cols;

    termios og_termios;   // an object of class "termios"
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

  termios raw = config.og_termios; // copy the original terminal attributes into a new termios variable "raw", so that the original attributes don't get disturbed when the program calls exit_og_termios_mode before quitting the program.
  
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
/**** Input Operations ****/
//==========================================================================================================

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
  }
}


//==========================================================================================================
/*** output ***/
//==========================================================================================================

void editorDrawRows()  // The rows of tildes
{
  int y;
  for (y = 0; y < 24; y++) {
    write(STDOUT_FILENO, "~\r\n", 3);
  }
}

void editorRefreshScreen() 
{
  write(STDOUT_FILENO, "\x1b[2J", 4); // Clears the terminal
  write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
  editorDrawRows();
  write(STDOUT_FILENO, "\x1b[H", 3);  // Moves the cursor at the top-left of the terminal
}

//==========================================================================================================
//==========================================================================================================

void initEditor() 
{
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
