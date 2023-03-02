
#include "devices/input.h"
#include "lib/kernel/console.h"
#include "lib/stdio.h"
#include "lib/string.h"
#include "threads/simple_shell.h"
// A simple shell that reads a command line and executes it.
static bool visable_char(char c){
  if(c >= ' ' && c <= '~') return true;
  return false;
}
void simple_shell(void){
    while (1) {
      printf("PKUOS>");
      char command_line[128];
      int len = 0;
      command_line[0] = '\0';
      for(;;){
        char c = input_getc();
        if(c == 127){//\b
          if(len > 0){
            len--;
            command_line[len] = '\0'; 
            putchar('\b');
            putchar(' ');
            putchar('\b');
          }
          continue;
        }

        if(c == '\r' || c=='\n'){
          putchar('\n');
          break;
        }

        if(visable_char(c) == false) continue;
        command_line[len++] = c;
        command_line[len] = '\0';
        putchar(c);
        //如果恰好就是
      }
      // putchar('over');
      // TODO: Parse and execute command_line
      // For example:
//When a newline is entered, it parses the input and checks if it is whoami. If it is whoami, print your student id. Afterward, the monitor will print the command prompt PKUOS> again in the next line and repeat.
      if (strcmp(command_line, "whoami") == 0){
        printf("2100012959\n");
        continue;
      }
      if (strcmp(command_line, "exit") == 0) {
        break; // Exit the loop
      }
      printf("Command not recognized: %s\n", command_line);
    }
}