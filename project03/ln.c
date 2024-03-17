#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  if(argc != 4){
    printf(2, "Usage: ln [-h/-s] old new\n");
    exit();
  }
  
  // Hard link.
  if(!strcmp(argv[1], "-h")){
    if(link(argv[2], argv[3]) < 0)
      printf(2, "link %s %s: failed\n", argv[1], argv[2]);
  }
  // Symbloic link.
  else if(!strcmp(argv[1], "-s")){
    if(slink(argv[2], argv[3]) < 0)
      printf(2, "link %s %s: failed\n", argv[1], argv[2]);
  }
  else{
    printf(2, "Invalid option \'%s\'\n", argv[1]);
  }
  exit();
}
