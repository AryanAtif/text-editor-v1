#include <unistd.h>
#include <stdlib.h>
#include <termios.h>

struct termios og_termios;

void exit_og_termios_mode()
{
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios);
}


void enter_og_termios_mode()
{
  tcgetattr(STDIN_FILENO, &og_termios); // read the original terminal attributes.
                                        
  atexit(exit_og_termios_mode); // tell the compiler beforehand to execute exit_og_termios_mode() before quiting the program.

  struct termios raw = og_termios; // copy the original terminal attributes into a new termios variable "raw", so that the original attributes don't get disturbed when the program calls exit_og_termios_mode before quitting the program.
  
  raw.c_lflag &= ~(ECHO);        // AND the ECHO bits with the inverted bits

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);      // set the new terminal attributes to raw - aka - the struct termios
}


int main() 
{
  
  enter_og_termios_mode();
  char c;
  
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
  
  return 0;
}
