#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <dirent.h>

int N;//nb Clients Max
int listeClients[100];
char pseudoClients[100][20];
int nbClients = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

sem_t semaphore;/*permet à tous les threads qui seront crées d'y accéder*/
sem_t semaphoreFile;

/* Fonction permettant la création de la socket serveur
    Pré-requis :
        port(int): Port sur lequel la socket sera crééeint sem_init(sem_t *semaphore, int pshared, unsigned int valeur);
    Résultat :
        Retourne le descripteur de la socket
 */
int creationSocket(int port){
    int dS = socket(PF_INET, SOCK_STREAM, 0);
    if(dS == -1){
        perror("Erreur socket");
        exit(1);
    }
    struct sockaddr_in ad;
    ad.sin_family = AF_INET;
    ad.sin_addr.s_addr = INADDR_ANY ;
    ad.sin_port = port;
    /*nommage socket */
    if( bind(dS, (struct sockaddr*)&ad, sizeof(ad)) == -1 ){
        perror("Erreur bind");
        exit(1);
    }
    /*passer une socket en mode écoute*/
    if( listen(dS, N) == -1){
        perror("Erreur listen");
        exit(1);
    }

    return dS;
}

void deconnexion(int dsC, int force){
    shutdown(dsC, 2);

    // Décalage des clients dans le tableau
    pthread_mutex_lock(&mutex);
    int id = 0;
    while(listeClients[id] != dsC) id++;
    for(int k = id; k < nbClients ; k++){
        listeClients[k] = listeClients[k + 1];
        /*decalage des pseudo aussi*/

        // Si il s'est déconnecté avant d'entrer le pseudo, pas besoin de décaler les pseudos
        if(force != 1){
            for(long j = 0; j < 20; j++ ){
              pseudoClients[k][j] = pseudoClients[k+1][j];
            }
        }
    }
    nbClients--;
    pthread_mutex_unlock(&mutex);
    sem_post(&semaphore); /*Operation V : se deconnecter */
    pthread_exit(0);

}

void choixPseudo(int dsC){
    char pseudoEmet[20];
    int test = 0;
    pthread_mutex_lock(&mutex);
    do{
        test=0;
        if(recv(dsC, pseudoEmet, sizeof(pseudoEmet), 0) > 0){
            /*cas pseudo vide*/
            if(strtok(pseudoEmet," ")==NULL){
                test=1;
            }else{
                /*cas pseudo existant*/
                for(long k=0;k<N;k++){
                    if(strcmp(pseudoClients[k],pseudoEmet)==0){
                      test =1;
                    }
                }
            }
            send(dsC, &test, sizeof(test), 0);
        }else{
            pthread_mutex_unlock(&mutex);
            deconnexion(dsC, 1);
        }
    }while(test==1);
    // Si le client s'est déconnecté avant d'entrer le pseudo, on passe
    for(long j = 0; j < 20; j++ ){
        pseudoClients[nbClients-1][j] = pseudoEmet[j];
    }
    pthread_mutex_unlock(&mutex);
    printf("Client connecté avec le pseudo : %s\n",pseudoEmet);
}

void relayageMessage(int dsC, char* pseudoDest, char* msg){
    pthread_mutex_lock(&mutex);
    if(strcmp(pseudoDest,"ALL") == 0){
        for(int j = 0; j < N ; j++ ){
            if(dsC != listeClients[j]){
                send(listeClients[j], msg, sizeof(msg), 0);
            }
        }
    }else{
        int existe = 0;
        int j=0;
        while(j<nbClients && existe != 1){
            printf("Pseudo : %s , %s \n",pseudoDest,pseudoClients[j]);
            if(strcmp(pseudoDest,pseudoClients[j]) == 0){
            existe=1;
            }else{
            j++;
            }
        }
        if(existe==0){
            printf("Votre ami n'existe pas \n");
        }else{
            send(listeClients[j], msg, sizeof(msg), 0);
        }

    }
    pthread_mutex_unlock(&mutex);
}

void *receptionFichier(void* s){
    int dsC = (long)s;
    char nomFic[100];
    recv(dsC, nomFic, sizeof(nomFic), 0);
    // créer le fichier avec le meme nom
    FILE *fp = fopen(nomFic, "w+"); 
    if(fp == NULL){
        perror("fichier");
        exit(1);
    }

    int tailleLigne;
    char ligne[100];
    while (1) {
        /*if(recv(dsC, &tailleLigne, sizeof(tailleLigne), 0) <= 0){
            break;
        };
        printf("taille : %d\n", tailleLigne);*/
        int t = recv(dsC, ligne, 100, 0);
        if (t < 0){
            perror("Erreur receive fichier");
            exit(1);
        }else if( t == 0){
            break;
        }
        fputs(ligne,fp);
    }
    fclose(fp);
    sem_post(&semaphoreFile);
    shutdown(dsC, 2);
    pthread_exit(0);
}

/*Fonction du thread créé pour chaque client, qui permet de transmettre le message envoyé par le client écouté par le thread à tous les autres clients
    Pré-requis :
        s : pointeur vers le descripteur de la socket permettant la communication avec le client écouté par le thread
    Résultat:
        La liste des clients est mise à jour et le thread se termine
*/


void *transmettreMessage(void *s){

    char msg[100];
    int dsC=(long) s;

    choixPseudo(dsC);

    while(1){

        if(recv(dsC, msg, sizeof(msg), 0) <= 0) break;
        char msgCopy[sizeof(msg)];
        strcpy(msgCopy,msg);
        if(strstr(msgCopy," ")!=NULL){
            strcpy(msgCopy,strstr(msgCopy," "));
        }
        printf("MsgCOPY : %s \n",msgCopy);

        char * pseudoDest = strtok ( msg, " " );

        relayageMessage(dsC, pseudoDest, msgCopy);

    }
    deconnexion(dsC, 0);
}


/* Procédure permettant la connexion entre clients/server
    Pré-requis :
        dS(int): Descripteur de la socket sur laquelle attendre les connexions    des clients
*/
void acceptClients(int dS){
    struct sockaddr_in aC ;
    socklen_t lg = sizeof(struct sockaddr_in);
    pthread_t thread[N];

    // acceptation des clients
    long i = 0;
    while(1){
        /* opération P : pour se connecter*/
        sem_wait(&semaphore);

        long dsC= accept(dS, (struct sockaddr*) &aC,&lg) ;
        if(dsC == -1){
            perror("Erreur connexion client ");
            exit(1);
        }
        // envoi d'id à chaque client
        if(send(dsC, &i, sizeof(i), 0) <= 0){
            perror("Erreur send id");
        }

        pthread_mutex_lock(&mutex);
        listeClients[nbClients] = dsC;

        nbClients++;
        pthread_mutex_unlock(&mutex);
        /*Dès qu'un client se connecte on lance un thread */
        if( pthread_create(&thread[i], NULL, transmettreMessage, (void*)dsC) != 0) {
            perror("Erreur création thread");
        }

        printf("Client %ld connecté\n", i+1);
        i++;
    }
    for(int j = 0; j < N ; j++){
        pthread_join(thread[j], NULL);
    }
}

void *listerFichiers(void *s){
    int dS = (long) s;
    // Demander à l'utilisateur quel fichier afficher
    DIR *dp;
    struct dirent *ep;     
    dp = opendir ("./");
    if (dp != NULL) {
        while (ep = readdir (dp)) {
            if(strcmp(ep->d_name,".")!=0 && strcmp(ep->d_name,"..")!=0){
                printf("%s\n",ep->d_name);
                send(dS, &ep->d_name, 100, 0);
            }
        }    
        (void) closedir (dp);
    }
    else {
        perror ("Ne peux pas ouvrir le répertoire");
    }
    shutdown(dS, 2);
    pthread_exit(0);
    
}

void *transmettreFichier(void *s){

    int dS = (long)s;
    char nomFic[100];
    recv(dS, &nomFic, sizeof(nomFic), 0);
    printf("nomFchier :%s\n",nomFic);
    FILE * fp;
    fp = fopen(nomFic, "r");
    if(fp == NULL){
        perror("Fichier inexistant");
    }else{
        // Fgets  : Pour chaque ligne, envoyer la taille de la ligne qu'on va envoyer, puis la ligne
        char data[100];
        while(fgets(data, 100, fp)!= NULL){
            //int tailleLigne = strlen(data);
            //send(dS, &tailleLigne, sizeof(tailleLigne), 0); 
            if (send(dS, data, 100, 0) == -1) {
                perror("Erreur send file");
                exit(1);
            }
        }
    }
    fclose(fp);
    shutdown(dS, 2);
    pthread_exit(0);
}

void *acceptFile(void* s){
    int dS = (long)s;
    struct sockaddr_in aC ;
    socklen_t lg = sizeof(struct sockaddr_in);
    pthread_t thread[N];

    // acceptation des clients
    long i = 0;
    while(1){
        /* opération P : pour se connecter*/
        sem_wait(&semaphoreFile);

        long dsC= accept(dS, (struct sockaddr*) &aC,&lg) ;
        if(dsC == -1){
            perror("Erreur connexion client ");
            exit(1);
        }
        int id;
        recv(dsC, &id, sizeof(id), 0);
        
        if(id == 1){ // liste fichiers
            if( pthread_create(&thread[i], NULL, listerFichiers, (void*)dsC) != 0) {
                perror("Erreur création thread");
            }
        }else if(id == 2){ // envoi un fichier
            if( pthread_create(&thread[i], NULL, transmettreFichier, (void*)dsC) != 0) {
                perror("Erreur création thread");
            }
        }else{
            /*Dès qu'un client se connecte on lance un thread */
            if( pthread_create(&thread[i], NULL, receptionFichier, (void*)dsC) != 0) {
                perror("Erreur création thread");
            }
        }
        i++;
    }
    for(int j = 0; j < N ; j++){
        pthread_join(thread[j], NULL);
    }
}

int main(int argc, char *argv[]) {

    if(argc != 3){
        perror("Mauvais nombre d'arguments");
        exit(1);
    }
    N = atoi(argv[2]);
    // creation de la socket
    int dS = creationSocket(htons(atoi(argv[1])));
    long dSFile = creationSocket(htons(atoi(argv[1])+1));
    printf("-- Serveur lancé --\n");
    printf("Attente de clients...\n");

    /*initialisation du semaphore declaré en global*/
    sem_init(&semaphore, PTHREAD_PROCESS_SHARED, atoi(argv[2]));

    /**/
    sem_init(&semaphoreFile, PTHREAD_PROCESS_SHARED, atoi(argv[2]));
    
    // lancement du serveur
    pthread_t threadFile;
    pthread_create(&threadFile, NULL, acceptFile, (void *) dSFile);
    acceptClients(dS);
    ;
    pthread_join(threadFile, NULL);
    shutdown(dS, 2) ;
    return 0;
}
