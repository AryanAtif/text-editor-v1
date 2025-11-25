#include <unistd.h>
#include <termios.h>

void enter_raw_mode()
{
  struct termios raw;

  tcgetattr(STDIN_FILENO, &raw); //  read the terminal attributes into raw - aka - the struct termios
                                 //
  raw.c_lflag &= ~(ECHO);        // AND the ECHO bits with the inverted bits

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);      // set the new terminal attributes to raw - aka - the struct termios
}


int main() 
{
  
  enter_raw_mode();
  char c;
  
  while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q');
  
  return 0;
}
