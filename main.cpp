#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <iostream>
#include <ctype.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>


#define CTRL_KEY(k) ((k) & 0x1f) // Define Ctrl+<anyKey> to be 00011111 (which behaves on terminal as ctrl) + <anykey>

class termios og_termios;

void exit_raw_mode()
{
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios) == -1) 
  {
    std::cerr << "tcsetattr error: " << std::strerror(errno) << std::endl;
    exit(1);
  }
}


void enter_raw_mode()
{
  //Read the terminal Attributes
  if( tcgetattr(STDIN_FILENO, &og_termios) == -1){throw std::runtime_error(std::string("tcsetattr error: ") + std::strerror(errno));}

  atexit(exit_raw_mode); // tell the compiler beforehand to execute exit_raw_mode() before quiting the program.

  class termios raw = og_termios; // copy the original terminal attributes into a new termios variable "raw", so that the original attributes don't get disturbed when the program calls exit_og_termios_mode before quitting the program.
  
  raw.c_iflag &= ~(ICRNL | IXON);                          // Turn off the Ctrl+S, Ctrl+Q
  raw.c_oflag &= ~(OPOST);                                 // Turn off Ctrl+V
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN) ;        // AND the ECHO bits with the inverted bits

  raw.c_cc[VMIN] = 0; // minimun characters to be read by read()
  raw.c_cc[VTIME] = 1; // 0.1 secs // The time read() waits to read a character      
                       
// set the new terminal attributes to raw - aka - the struct termios
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {throw std::runtime_error(std::string("tcsetattr error: ") + std::strerror(errno));}
}

char editorReadKey() 
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

int main() 
{
  try 
  {
    enter_raw_mode();
    while (1) // To run infinitely until read() returns 0 (aka timeruns out)
    {
      char c = '\0';

        // The body of this loop (i.e, everything written below) returns the ascii value of all characters that have been pressed by the user, and if the user presses non-control characters, the characters also get printed.
        // The character of ascii values (0-31) are named control characters. they really don't have any symbol assigned to them.
      if ( c >= 0 && c <= 31)  
      {
        std::cout << int(c) << "\r\n";
      }
      else
      {
        std::cout << int(c) << " (" << c << ")" << "\r\n";
      }
      if (c == CTRL_KEY('q')) break; // quit the loop when read "Q" 
    }
  }
  
  catch(const std::exception &error)
  {
    std::cerr << error.what() << std::endl;
    exit(1);
  }


  return 0;
}
