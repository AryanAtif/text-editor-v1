#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <iostream>
#include <ctype.h>


class termios og_termios;

void exit_raw_mode()
{ tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios);
}


void enter_raw_mode()
{
  tcgetattr(STDIN_FILENO, &og_termios); // read the original terminal attributes.
                                        
  atexit(exit_raw_mode); // tell the compiler beforehand to execute exit_raw_mode() before quiting the program.

  class termios raw = og_termios; // copy the original terminal attributes into a new termios variable "raw", so that the original attributes don't get disturbed when the program calls exit_og_termios_mode before quitting the program.
  
  raw.c_iflag &= ~(ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN) ;        // AND the ECHO bits with the inverted bits

  raw.c_cc[VMIN] = 0; // minimun characters to be read by read()
  raw.c_cc[VTIME] = 1; // 0.1 secs // The time read() waits to read a character      
                       
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);      // set the new terminal attributes to raw - aka - the struct termios
}


int main() 
{
  
  enter_raw_mode();
  while (1) // To run infinitely until read() returns 0 (aka timeruns out)
  {
    char c = '\0';
    
    read(STDIN_FILENO, &c, 1);
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
    if (c == 'q') break; // quit the loop when read "Q" 
  }
  return 0;
}
