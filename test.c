#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>

int get_last_tty() {
  FILE *fp;
  char path[1035];
  fp = popen("/bin/ls /dev/pts", "r");
  if (fp == NULL) {
    printf("Impossible d'exécuter la commande\n" );
    exit(1);
  }
  int i = INT_MIN;
  while (fgets(path, sizeof(path)-1, fp) != NULL) {
    if(strcmp(path,"ptmx")!=0){
      int tty = atoi(path);
      if(tty > i) i = tty;
    }
  }

  pclose(fp);
  return i;
}

FILE* new_tty() {
  pthread_mutex_t the_mutex;  
  pthread_mutex_init(&the_mutex,0);
  pthread_mutex_lock(&the_mutex);
  system("gnome-terminal"); sleep(1);
  char *tty_name = ttyname(STDIN_FILENO);
  int ltty = get_last_tty();
  char str[2];
  sprintf(str,"%d",ltty);
  int i;
  for(i = strlen(tty_name)-1; i >= 0; i--) {
    if(tty_name[i] == '/') break;
  }
  tty_name[i+1] = '\0';  
  strcat(tty_name,str);  
  FILE *fp = fopen(tty_name,"wb+");
  pthread_mutex_unlock(&the_mutex);
  pthread_mutex_destroy(&the_mutex);
  return fp;
}

int main(int argc, char *argv[]) {
  FILE* fp1 = new_tty();
  fprintf(fp1,"%s\n","Ce terminal sera utilisé uniquement pour l'affichage");

  // Demander à l'utilisateur quel fichier afficher
  DIR *dp;
  struct dirent *ep;     
  dp = opendir ("./");
  if (dp != NULL) {
    fprintf(fp1,"Voilà la liste de fichiers :\n");
    while (ep = readdir (dp)) {
      if(strcmp(ep->d_name,".")!=0 && strcmp(ep->d_name,"..")!=0) 
	fprintf(fp1,"%s\n",ep->d_name);
    }    
    (void) closedir (dp);
  }
  else {
    perror ("Ne peux pas ouvrir le répertoire");
  }
  printf("Indiquer le nom du fichier : ");
  char fileName[1023];
  fgets(fileName,sizeof(fileName),stdin);
  fileName[strlen(fileName)-1]='\0';
  FILE *fps = fopen(fileName, "r");
  if (fps == NULL){
    printf("Ne peux pas ouvrir le fichier suivant : %s",fileName);
  }
  else {
    char str[1000];    
    // Lire et afficher le contenu du fichier
    while (fgets(str, 1000, fps) != NULL) {
      fprintf(fp1,"%s",str);
    }
  }
  fclose(fps);	
  return 0;
}
