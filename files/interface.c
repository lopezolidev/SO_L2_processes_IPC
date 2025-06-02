#include <stdio.h>
#include <time.h>
#include <unistd.h> // Para sleep()

int main(int argc, char* argv[]){

int i;

for(i=0;i<20;i++){
printf("Time: %d\n",i);
sleep(1);
}

return 0;
}
