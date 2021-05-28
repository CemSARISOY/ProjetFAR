/**
 * \file serveur.c
 * \brief Programme du serveur
 * \author 
 * \version 0.1
 * \date 21 Mai 2021
 *
 * Programme du serveur
 *
 */




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
#include <signal.h>
#include <limits.h>
#include <ctype.h>
#include "files.h"
#define MSGSIZE 1024

int n; // Nombre de chaines max
int nChaines; // Nombre de chaines actuel

// Les deux descripteurs de socket principaux
int dSMsg;
int dSFile;


/**
 * @brief Stockage infos clients
 * 
 * Pour chaque client, on stock sa socket et son thread associé
 */
struct infoClients{
    int dS;
    pthread_t thread;
};

// Liste des threads fini (à join)
pthread_t* threadsFini;
int nbThreadsFini;

// Liste des threads
pthread_t* threadsExec;
int nbThreadsExec;

/**
 * @brief Stockage connexion client
 * 
 * Pour chaque client, on stock dans les informations d'une chaine sa socket et son pseudo
 */
struct client{
    int dS;
    char pseudo[20];
};

/**
 * @brief Informations d'une chaine
 * 
 * Structure stockant le nom de la chaine, une description associée, le nombre de clients max pouvant se connecter,
 * le nombre de clients actuellement connecté, ainsi que la liste des clients connectés
 */
struct chaine{
    char nomChaine[20];
    char description[75];
    int m;  //nombre max de clients qui peuvents se connecter dans une chaque chaine
    int nb; //nombre de clients deja connectés
    struct client *listeClients;
};

// Liste de chaines
struct chaine *listeChaines;

// Mutex pour les variables globales
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// Mutex pour les variables globales de thread uniquement
pthread_mutex_t mutexThreads = PTHREAD_MUTEX_INITIALIZER;

sem_t semaphore;
sem_t semaphoreFile;

/**
  * @brief procedure qui cherche la position du client sur le serveur (chaines) grâce à son pseudo,
  *         elle modifie i et j (si i == nbdechaines alors client non trouvé)
  *
  * @param pseudo Pseudo du client
  * @param i Entier à modifier pour indiquer la position de la chaine dans laquelle le client est
  * @param j Entier à modifier pour indiquer la position du client dans la chaine
*/
void findClientByPseudo(char* pseudo, int *i, int *j){
    int k=0;
    int l=0;
    int trouve = 0;
    while(k < nChaines && trouve==0){
        l=0;
        while(l<listeChaines[k].nb && trouve==0){
            if(strcmp(pseudo,listeChaines[k].listeClients[l].pseudo) == 0){
                trouve=1;
            }else{
                l++;
            }
        }
        if(trouve==0) k++;
    }
    *i = k;
    *j = l;
}

/**
  * @brief procedure qui cherche la position du client sur le serveur (chaines) grâce à sa socket,
  *     elle modifi i et j (si i == nbdechaines alors client non trouvé)
  *
  * @param dS Descripteur de la socket du client
  * @param i Entier à modifier pour indiquer la position de la chaine dans laquelle le client est
  * @param j Entier à modifier pour indiquer la position du client dans la chaine

*/
void findClientBySocket(int dS, int *i, int *j){
    int k=0; int l=0; int trouve = 0;
    while(k < nChaines && trouve==0){
        l=0;
        while(l<listeChaines[k].nb && trouve==0){
            if(listeChaines[k].listeClients[l].dS == dS){
                trouve=1;
            }else{
                l++;
            }
        }
        if(trouve==0) k++;
    }
    *i = k;
    *j = l;
}

/**
  * @brief fonction permettant la création de la socket serveur
  *
  * @param port Port sur lequel la socket sera créée
  * @return le descripteur de la socket
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

    if( bind(dS, (struct sockaddr*)&ad, sizeof(ad)) == -1 ){ //nommage socket
        perror("Erreur bind");
        exit(1);
    }
    //passer une socket en mode écoute
    if( listen(dS, 100) == -1){
        perror("Erreur listen");
        exit(1);
    }

    return dS;
}

/**
  * @brief procédure déconnectant un client et terminant le thread
  *
  * @param dsC Descripteur socket du client à déconnecter
  * @param force  Variable permettant si l'on doit forcer la déconnexion (c'est à dire sans toucher aux pseudos)
*/

void deconnexion(int dsC, int force, pthread_t thread){
    shutdown(dsC, 2);

    // Décalage des clients dans le tableau
    pthread_mutex_lock(&mutex);
    int k,j;

    findClientBySocket(dsC, &k, &j);

    for(int i = j; i < listeChaines[k].nb ; i++){
        listeChaines[k].listeClients[i].dS = listeChaines[k].listeClients[i+1].dS;
        //decalage des pseudo aussi

        // Si il s'est déconnecté avant d'entrer le pseudo, pas besoin de décaler les pseudos
        if(force != 1){
            strcpy(listeChaines[k].listeClients[i].pseudo,listeChaines[k].listeClients[i+1].pseudo);
        }
    }
    listeChaines[k].nb--;
    pthread_mutex_unlock(&mutex);

    pthread_mutex_lock(&mutexThreads);
    nbThreadsFini++;
    threadsFini = (pthread_t *)realloc(threadsFini, sizeof(pthread_t) * nbThreadsFini);
    threadsFini[nbThreadsFini - 1] = thread;
    pthread_mutex_unlock(&mutexThreads);

    sem_post(&semaphore); /*Operation V : se deconnecter */
    pthread_exit(0);

}

/**
  * @brief Fonction qui connecte le client à son propre compte
  *
  * @param dsC Descripteur socket du client entrant son pseudo
  * @return le pseudo du client
*/
char* connexionCompte(int dsC, pthread_t thread){
    char* pseudoEmet = malloc(20);
    char* mdpEmet = malloc(20);
    int test=1;
    char* nom;
    FILE * fp;
    int disconnected = 0;
    // Test de l'existence du compte
    do{
        fp = fopen("comptes.txt", "r");
        if(fp == NULL){
            fp = fopen("comptes.txt","w");
            fclose(fp);
        }else{
        char data[MSGSIZE];

            if(recv(dsC, pseudoEmet, 20, 0) > 0){

                if(recv(dsC, mdpEmet, 20, 0) > 0){
                    //chercher dans les lignes s'il y le pseudo , une fois trouver comparer le mot de passe
                    while(fgets(data, sizeof(data), fp)!= NULL && test==1){
                        char rec1[20];
                        strcpy(rec1,strtok(data,";"));
                        if(strcmp(rec1,pseudoEmet) == 0){
                            char rec2[20];
                            strcpy(rec2,strtok(NULL,";"));
                            if(strcmp(rec2,mdpEmet) == 0){
                                test=0;
                            }
                        }
                    }
                }else{
                    disconnected = 1;
                }
            }else{
                disconnected = 1;
            }
        }
        fclose(fp);

        if(test == 0){
            // Test si le compte est déjà connecté
            pthread_mutex_lock(&mutex);
            int i, j;
            findClientByPseudo(pseudoEmet, &i, &j);
            if(i != nChaines){
                test = 1;
            }
            pthread_mutex_unlock(&mutex);
        }
        if(disconnected == 0) send(dsC, &test, sizeof(test), 0); // Avertissement au client si il est connecté ou non

    }while(test == 1);
    if(disconnected == 1){
        free(pseudoEmet);
        free(mdpEmet);
        deconnexion(dsC, 1, thread);   
    }
    printf("\033[32mClient connecté avec le pseudo : %s\033[00m\n",pseudoEmet);
    
    // sauvegarde le client dans le home
    pthread_mutex_lock(&mutex);
    listeChaines[0].listeClients[listeChaines[0].nb].dS = dsC;
    strcpy(listeChaines[0].listeClients[listeChaines[0].nb].pseudo,pseudoEmet);
    listeChaines[0].nb++;
    pthread_mutex_unlock(&mutex);
    free(mdpEmet);
    return pseudoEmet;
}

/**
  *  @brief fonction testant si la chaine de caractère possède un espace
  *
  *  @param str la chaine de caracter à tester
  *  @return 1 si la chaine de caractère possède au moins un espace, 0 sinon
*/
int hasSpace(char* str){
    for(int i = 0; i<strlen(str) ; i++){
        if(isspace(str[i])) return 1;
    }
    return 0;
}


/**
  *  @brief fonction permettant à un client de s'inscrire
  *
  *  @param dsC Descripteur socket du client 
  *  @return le pseudo du client (stocké côté serveur si validé)
*/
char* inscription(int dsC, pthread_t thread){
    char* pseudoEmet = malloc(20);
    char* mdpEmet = malloc(20);
    int test = 0;
    char* nom;
    FILE * fp;
    int disconnected = 0;
    do{
        test = 0;
        fp = fopen("comptes.txt", "r");
        if(fp == NULL){
            fp = fopen("comptes.txt","w");
            fclose(fp);
        }else{
            char data[MSGSIZE];
            if(recv(dsC, pseudoEmet, 20, 0) > 0){
                if(pseudoEmet!=NULL && hasSpace(pseudoEmet) == 0){
                    if(recv(dsC, mdpEmet, 20, 0) > 0){
                        if(mdpEmet!=NULL && hasSpace(mdpEmet) == 0){
                            // Test de l'existence du compte dans les fichiers
                            while(fgets(data, sizeof(data), fp)!= NULL && test==0){
                                char rec1[20];
                                strcpy(rec1,strtok(data,";"));
                                if(strcmp(rec1,pseudoEmet) == 0){
                                    test=1;
                                }
                            }
                        }
                        else test=1;
                    }else{
                        disconnected = 1;
                    }
                }
                else test=1;
            }else{
                disconnected = 1;
            }
        }
        fclose(fp);
        
        if(disconnected == 0) send(dsC, &test, sizeof(test), 0);  // Avertissement au client si il est inscrit ou non

    }while(test == 1);
    if(disconnected == 1){
        free(pseudoEmet);
        free(mdpEmet);
        deconnexion(dsC, 1, thread);
    }
    // Création du compte
    fp = fopen("comptes.txt", "a");
    if(fp == NULL){
        perror("Erreur open compte");
    }else{
        char * ligne = malloc(45);
        sprintf(ligne, "%s;%s;\n", pseudoEmet, mdpEmet);
        fwrite(ligne, 1, strlen(ligne), fp);
        free(ligne);
    }
    fclose(fp);
    free(mdpEmet);

    // Connexion du nouveau compte
    pthread_mutex_lock(&mutex);
    listeChaines[0].listeClients[listeChaines[0].nb].dS = dsC;
    strcpy(listeChaines[0].listeClients[listeChaines[0].nb].pseudo,pseudoEmet);
    listeChaines[0].nb++;
    pthread_mutex_unlock(&mutex);
    printf("\033[32mClient connecté avec le pseudo : %s\033[00m\n",pseudoEmet);
    return pseudoEmet;
}

/**
  * @brief fonction qui convertie une chaîne en majuscule
  *
  * @param str chaine à convertir
  * @return copie de la chaîne en majuscule 
*/
char *uppercase( char str[256] ){
	int i;
	char* copy = malloc(20);

	for( i = 0; i < strlen(str)+1; i++ ){
        copy[i] = toupper(str[i]);	
	}	
	return copy;
}


/**
  * @brief fonction qui compare 2 chaines de caractères peu importe des majuscules et minuscules
  *
  * @param str1 chaine de caractères 1 à comparer
  * @param str2 chaine de caractères 2 à comparer
  * @return un entier représentant le nombre de caractères différents
*/
int strcmpUpper(char* str1, char* str2){
    char* copy1 = uppercase(str1);
    char* copy2 = uppercase(str2);
    int cmp = strcmp(copy1, copy2);
    free(copy1);
    free(copy2);
    return cmp;
}

/**
  * @brief Procédure envoyant à un client, la liste des clients connectés au serveur
  *
  * @param dS descripteur de la socket du client demandant la liste
  * 
*/
void listeConnectes(int dS){
    char str[MSGSIZE];
    strcpy(str, "\033[32m-- Liste des clients connectés --\033[00m\n");
    for(int i = 0; i < nChaines ; i++){
        strcat(str, "Chaine : ");
        strcat(str, listeChaines[i].nomChaine);
        strcat(str, "\n");
        for(int j = 0 ; j < listeChaines[i].nb; j++){
            char toto[100];
            sprintf(toto, "%d :\033[33m %s\033[00m\n",j+1, listeChaines[i].listeClients[j].pseudo);
            strcat(str,toto);
        }
        send(dS, str, MSGSIZE, 0);
        bzero(str, MSGSIZE);
    }
}


/**
  * @brief procédure créant une chaine
  *
  * @param msg Informations de la chaine (format : nom nbMax description)
  * @param dS  Descripteur de la socket du créateur de la chaine pour lui envoyer un message de retour
*/
void creationChaine(char* msg, int dS){
    if(nChaines < n){
        char * copy = malloc(strlen(msg) + 1);
        copy = strcpy(copy, msg);
        copy[strlen(msg)] = '\0';
        char* nomChaine , *description;
        int nbMax;

        // Récupération des différentes informations de la chaine
        nomChaine = strtok (msg, " " );
        if(nomChaine != NULL){
            char* tok = strtok (NULL, " " );
            if(tok != NULL){
                nbMax = atoi(tok);
                description = strtok (NULL, " " );
                if(description != NULL){
                    char * desc = strstr(copy, description);
                    char * erreur = "\033[31mCette chaîne a déjà été créée\033[00m";
                    int i = 0;
                    int trouve = 0;
                    while(i<nChaines && trouve!=1){
                        if(strcmpUpper(nomChaine, listeChaines[i].nomChaine) == 0){
                            send(dS, erreur,MSGSIZE,0);
                            trouve=1;
                        }else{
                            i++;
                        }
                    }

                    if(trouve==0){
                    // Sauvegarde de la chaine sur le serveur
                    struct chaine nouvelleChaine;
                    strcpy(nouvelleChaine.nomChaine ,nomChaine);
                    strcpy(nouvelleChaine.description , desc);
                    nouvelleChaine.m = nbMax;
                    nouvelleChaine.nb = 0;
                    nouvelleChaine.listeClients=malloc(sizeof(struct client)*nouvelleChaine.m);

                    // Sauvegarde de la chaine dans un fichier
                    FILE *fp = fopen("Chaines.txt", "a+");
                    if(fp == NULL){
                        perror("fichier");
                        exit(1);
                    }else{
                        char ligne[MSGSIZE];
                        sprintf(ligne, "%s;%d;%s\n",nomChaine, nbMax, desc);
                        fputs(ligne, fp);
                    }
                    fclose(fp);
                    free(copy);

                    listeChaines[nChaines]=nouvelleChaine;
                    nChaines++;

                    char* created = "\033[32mChaine correctement créée\033[00m";
                    send(dS, created, MSGSIZE, 0);
                    }
                }
            }
        }

        
    }else{
        char* created = "\033[31mNombre de chaine max atteint\033[00m";
        send(dS, created, MSGSIZE, 0);
    }

}


/**
  * @brief Procédure connectant le client à une chaine
  *
  * @param msg Nom de la chaine
  * @param dS Descripteur de la socket du client voulant se connecter à la chaine
*/
void connexionChaine(char *msg,int dS){
    int i=0,trouve=0,k=0,j=0;
    // Recherche de la chaine dans laquelle il souhaite se connecter
    while(i<nChaines && trouve!=1){
        if(strcmpUpper(msg,listeChaines[i].nomChaine) == 0){
            trouve=1;
        }else{
            i++;
        }
    }
    if(trouve==1){
        if(listeChaines[i].nb != listeChaines[i].m ){
            // Recherche de la position du client dans le serveur
            findClientBySocket(dS, &k, &j);

            // Déplacement du client
            listeChaines[i].listeClients[listeChaines[i].nb].dS = listeChaines[k].listeClients[j].dS;
            strcpy(listeChaines[i].listeClients[listeChaines[i].nb].pseudo,listeChaines[k].listeClients[j].pseudo);
            listeChaines[i].nb++;
            listeChaines[k].nb--;
            // Supression dans l'ancienne chaine
            for(int l = j; l < listeChaines[k].nb ; l++){
                listeChaines[k].listeClients[l].dS = listeChaines[k].listeClients[l+1].dS;
                strcpy(listeChaines[k].listeClients[l].pseudo,listeChaines[k].listeClients[l+1].pseudo);
            }


            char* connected = "Connecté à la chaine ";
            char* connecte = malloc(strlen(connected) + strlen(msg) + 2);
            strcpy(connecte, connected);
            strcat(connecte, msg);
            send(dS, connecte, MSGSIZE, 0);
            free(connecte);
        }else{
            char* connected = "\033[31mLa chaîne est complète !\033[00m";
            send(dS, connected, MSGSIZE, 0);
        }
    }else{
        char* connected = "\033[31mChaine inexistante\033[00m";
        send(dS, connected, MSGSIZE, 0);
    }
}

/**
  * @brief procédure listant toutes les chaines au client
  *
  * @param dS  Descripteur de la socket du client auquel envoyer la liste
*/
void listerChaine(int dS) {
    char* entete = "\033[32m-- Liste des chaines --\033[00m";
    send(dS, entete, MSGSIZE, 0);
    char* nom;
    for(int i=0;i<nChaines;i++){
        nom = malloc(strlen(listeChaines[i].nomChaine) + 4 + strlen(listeChaines[i].description));
        sprintf(nom, "%s : %s", listeChaines[i].nomChaine, listeChaines[i].description);
        int s=send(dS,nom , MSGSIZE, 0);
        printf("send : %d\n",s);
        free(nom);
    }
}

/**
  * @brief Procédure supprimant une chaine et qui déplace tous les clients de cette chaine au home
  *
  * @param msg Nom de la chaine à supprimer
  * @param dS Descripteur de la socket du client voulant supprimer la chaine pour lui envoyer un retour

*/
void deleteChaine(char *msg, int dS) {

    int i=1,trouve=0;
    // Recherche de la position de la chaine à supprimer
    while(i<nChaines && trouve!=1){
        if(strcmpUpper(msg, listeChaines[i].nomChaine) == 0){
            trouve=1;
        }else{
            i++;
        }
    }

    if(trouve==1){
        // Déplacement de tous les clients à l'home
        int nbConnecte = listeChaines[i].nb;
        for(int j=0; j<nbConnecte;j++){
            connexionChaine("Home", listeChaines[i].listeClients[0].dS);
        }

        // Suppression de la chaine
        nChaines--;
        for(int j=i;j<nChaines;j++){
            listeChaines[j]=listeChaines[j+1];
        }
        char* deleted = "\033[32mChaine correctement supprimée\033[00m";
        send(dS, deleted, MSGSIZE, 0);

        // Suppression depuis le fichier
        remove("Chaines.txt");
        FILE *fp = fopen("Chaines.txt", "a+");
        if(fp == NULL){
            perror("fichier");
            exit(1);
        }
        for(int i = 1; i < nChaines ; i++){
            char ligne[MSGSIZE];
            sprintf(ligne, "%s;%d;%s\n",listeChaines[i].nomChaine, listeChaines[i].m, listeChaines[i].description);
            fputs(ligne, fp);
        }
        fclose(fp);

    }else{
        char* deleted = "\033[31mLa chaine n'a pas été supprimée\033[00m";
        send(dS, deleted, MSGSIZE, 0);
    }


}

/**
  * @brief Procédure modifiant une chaine
  *
  * @param msg Informations de la chaine (format : chaineAModifier nouveauNom nouveauNbMax nouvelleDescription)
  * @param dS Descripteur de la socket du client voulant modifier la chaine pour lui envoyer un retour
*/
void modifierChaine(char* msg, int dS){
    char * copy = malloc(strlen(msg) + 1);
    strcpy(copy, msg);
    char* nomChaine , *description, *nouveauNomChaine;
    int nbMax;

    // Recherche de la position de la chaine à modifier
    nomChaine = strtok (msg, " " );
    int i = 1, trouve = 0;
    while(i<nChaines && trouve!=1){
        if(strcmpUpper(msg, listeChaines[i].nomChaine) == 0){
            trouve=1;
        }else{
            i++;
        }
    }
    if(trouve==1){
        // Récupération des différentes informations et modification
        nouveauNomChaine = strtok (NULL, " " );
        if(nouveauNomChaine != NULL){
            char* tok = strtok (NULL, " " );
            if(tok != NULL){
                nbMax = atoi(tok);
                description = strtok (NULL, " " );
                if(description != NULL){
                    char * desc = strstr(copy, description);

                    strcpy(listeChaines[i].nomChaine ,nouveauNomChaine);
                    strcpy(listeChaines[i].description , desc);
                    listeChaines[i].m = nbMax;

                    char* created = "\033[32mChaine correctement modifiée\033[00m";
                    send(dS, created, MSGSIZE, 0);

                    // Modification dans le fichier
                    remove("Chaines.txt");
                    FILE *fp = fopen("Chaines.txt", "a+");
                    if(fp == NULL){
                        perror("fichier");
                        exit(1);
                    }
                    for(int i = 1; i < nChaines ; i++){
                        char ligne[MSGSIZE];
                        sprintf(ligne, "%s;%d;%s\n",listeChaines[i].nomChaine, listeChaines[i].m, listeChaines[i].description);
                        fputs(ligne, fp);
                    }
                    fclose(fp);
                }
            }
        }
    }else{
        char* created = "\033[31mLa chaine à modifier n'a pas été trouvée\033[00m";
        send(dS, created, MSGSIZE, 0);
    }
    free(copy);

}

/**
  * @brief Procédure affichant la liste des commandes au client
  *
  * @param dS Descripteur de la socket du client souhaitant la liste des commandes
*/
void affichageManuelUtilisateur(int dS) {
    char* entete = "--- Liste des commandes disponibles ---";
    send(dS, entete, MSGSIZE, 0);
    char* nom;
    FILE * fp;
    fp = fopen("help.txt", "r");
    if(fp == NULL){
        perror("Fichier inexistant");
    }else{
        char data[MSGSIZE];
        sendFile(fp,dS);
        fclose(fp);
    }
}


/**
  * @brief fonction qui associe l'action saisie par le client à un entier
  *
  * @param action variable qui contient le choix du client
  * @param msg message qui accompagne l'action
  * @return un entier qui represente l'action choisie
*/
int traitement(char *action,char *msg){
    if(strcmp(action,"/msg") == 0){
        return 0;
    }else if(strcmp(action,"/chaine") == 0){
        char * copy = malloc(strlen(msg) + 1);
        copy = strcpy(copy, msg);
        strtok(copy, " ");
        char * action2 = strtok ( NULL, " " );
        if(strcmp(action2, "list")==0){
            return 10;
        }else if(strcmp(action2, "delete")==0){
            return 11;
        }else if(strcmp(action2, "modify")==0){
            return 12;
        }else if(strcmp(action2,"join") == 0){
            return 13;
        }else if(strcmp(action2, "create") == 0){
            return 14;
        }
        free(copy);
    }else if(strcmp(action,"/help") == 0){
        return 2;
    }else if(strcmp(action, "/list") == 0){
        return 4;
    }else{
        if(action[0] == '/'){
            return -1;
        }
        return 3; //si l'utilisateur envoie le message à tout le monde
    }
    return -1;
}
/**
  * @brief fonction qui traite et envoie les messages privés
  *
  * @param dsC Descripteur socket du client envoyant le message
  * @param msg message qui doit êtretraité
  * @param pseudo pseudo du client qui envoie le messa
*/
void messagesPrives(int dsC, char* msg, char* pseudo){
    char * copy = malloc(strlen(msg) + 1);
    strcpy(copy, msg);
    strtok(msg, " ");
    char *pseudoDest = strtok(NULL," ");
    char *premierMot = strtok(NULL," ");
    while(premierMot != NULL && strcmp(pseudoDest, premierMot) == 0){
        premierMot = strtok(NULL, " ");
    }
    if(premierMot != NULL){

        char *message = strstr(copy,premierMot);
        char *messageEntier = malloc(strlen(message) + 40);
        sprintf(messageEntier, "\033[33m%s\033[00m vous envoie : %s", pseudo, message);
        int existe = 0;

        int i, j;
        findClientByPseudo(pseudoDest, &i, &j);

        if(i == nChaines){
            char* inexistant = "\033[31mVotre ami n'existe pas\033[00m";
            send(dsC, inexistant, MSGSIZE, 0);
        }else{

            send(listeChaines[i].listeClients[j].dS, messageEntier, MSGSIZE, 0);
        }
        free(messageEntier);
    }
    free(copy);
}

/**
  * @brief fonction qui traite et envoie les messages à tout le monde (ALL)
  *
  * @param dsC Descripteur socket du client envoyant le message
  * @param msg message qui doit être traité
  * @param pseudo pseudo du client qui envoie le message
*/
void broadcast(int dsC, char* msg, char* pseudo){
    char * messageEntier = malloc(strlen(msg) + 25);
    sprintf(messageEntier, "\033[33m%s \033[00m: %s", pseudo, msg);
    int k,j;
    findClientBySocket(dsC, &k, &j);

    for(int j = 0; j < listeChaines[k].nb ; j++ ){
        if(dsC != listeChaines[k].listeClients[j].dS){
            send(listeChaines[k].listeClients[j].dS, messageEntier, MSGSIZE, 0);
        }
    }
    free(messageEntier);
}

/**
  * @brief Procédure qui traite le message du client et lance la bonne action demandée
  *
  * @param dsC Descripteur socket du client envoyant le message
  * @param pseudo Pseudo de l'émetteur du message
  * @param action Action à effectuer (privé, chaine, ...)
  * @param msg Message à envoyer aux autres clients
*/
void relayageMessage(int dsC, char * pseudo, char* action, char* msg){
    pthread_mutex_lock(&mutex);
    char * chaine ;
    switch(traitement(action,msg)){
        case 0: // message privé
            messagesPrives(dsC, msg, pseudo);
            break;
        case 10: // liste des chaînes
            listerChaine(dsC);
            break;
        case 11: // Suppression chaîne
            chaine =  strtok (NULL, " " );
            if(chaine != NULL) deleteChaine(chaine, dsC);
            break;
        case 12: // Modification chaîne
            chaine =  strtok (NULL, " " );
            if(chaine != NULL) modifierChaine(strstr(msg, chaine), dsC);
            break;
        case 13: // Join chaîne
            chaine =  strtok (NULL, " " );
            if(chaine != NULL) connexionChaine(chaine,dsC);
            break;
        case 14: //Création chaîne
            chaine =  strtok (NULL, " " );
            if(chaine != NULL) creationChaine(strstr(msg, chaine), dsC);
            break;
        case 2: // help
            affichageManuelUtilisateur(dsC); 
            break;
        case 3:  //appel à la fonction broadcast pour ALL
            broadcast(dsC, msg, pseudo);
            break;
        case 4: // liste
            listeConnectes(dsC);
            break;
        default: //erreur
            chaine =  "\033[31mCette commande n'existe pas !\033[00m";
            send(dsC, chaine, MSGSIZE, 0);
            break;
    }
    pthread_mutex_unlock(&mutex);
}


/**
  * @brief Fonction du thread message créé pour chaque client, qui permet de transmettre le message envoyé par le client écouté par le thread aux destinataires
  *
  * @param s pointeur vers le descripteur de la socket permettant la communication avec le client écouté par le thread
  * @return La liste des clients est mise à jour et le thread se termine
*/
void *transmettreMessage(void *s){

    struct infoClients *info = (struct infoClients*) s;
    int dsC = (*info).dS;
    pthread_t thread = (*info).thread;

    pthread_mutex_lock(&mutexThreads);
    nbThreadsExec++;
    threadsExec = realloc(threadsExec, sizeof(pthread_t) * nbThreadsExec);
    threadsExec[nbThreadsExec-1] = thread;
    pthread_mutex_unlock(&mutexThreads);


    char msg[MSGSIZE];

    char * pseudo;
    int choix;
    recv(dsC, &choix, sizeof(choix), 0);

    if(choix == 1){
        pseudo = connexionCompte(dsC, thread);
    }else{
        pseudo = inscription(dsC, thread);
    }
    while(1){

        if(recv(dsC, msg, MSGSIZE, 0) <= 0) break;
        char msgCopy[sizeof(msg)];
        strcpy(msgCopy,msg);
        if(msgCopy[0] != ' '){
            char * action = strtok ( msg, " " );
            if(action != NULL) relayageMessage(dsC, pseudo,  action, msgCopy);
        }
        else{
            if(strstr(msgCopy," ")!=NULL){
                char * action = strtok ( msg, " " );
                if(action != NULL) relayageMessage(dsC, pseudo,  action, msgCopy);
            }
        }
    }
    //free(info);
    deconnexion(dsC, 0, thread);
}

/**
  * @brief Procédure permettant la connexion entre clients/serveur
  *
  * @param dS Descripteur de la socket sur laquelle attendre les connexions des clients
*/
void acceptClients(){

    struct sockaddr_in aC ;
    socklen_t lg = sizeof(struct sockaddr_in);
    threadsExec = malloc(0);
    threadsFini = malloc(0);
    // acceptation des clients
    long i = 0;
    while(1){
        /* opération P : pour se connecter*/
        sem_wait(&semaphore);

        int dsC= accept(dSMsg, (struct sockaddr*) &aC,&lg) ;
        if(dsC == -1){
            perror("Erreur connexion client ");
            exit(1);
        }

        struct infoClients *info;
        info = malloc(sizeof(struct infoClients));
        (*info).dS = dsC;
        /*Dès qu'un client se connecte on lance un thread */
        if( pthread_create(&(*info).thread, NULL, transmettreMessage, (void*)info) != 0) {
            perror("Erreur création thread");
        }

        pthread_mutex_lock(&mutexThreads);
        for(int j=0; j < nbThreadsFini; j++){
            pthread_join(threadsFini[j], NULL);
        }
        threadsFini = realloc(threadsFini, 0);
        nbThreadsFini = 0;
        pthread_mutex_unlock(&mutexThreads);

        printf("Client %ld connecté\n", i+1);
        i++;
    }
}

/**
  * @brief Procedure executée par un thread pour recevoir et stocker un fichier depuis le client
  *
  * @param s Pointeur vers le descripteur de la socket sur laquelle récupérer le fichier
*/
void *receptionFichier(void* s){

    struct infoClients *info = (struct infoClients*) s;
    int dsC = (*info).dS;
    pthread_t thread = (*info).thread;

    pthread_mutex_lock(&mutexThreads);
    nbThreadsExec++;
    threadsExec = realloc(threadsExec, sizeof(pthread_t) * nbThreadsExec);
    threadsExec[nbThreadsExec-1] = thread;

    pthread_mutex_unlock(&mutexThreads);

    char nomFic[MSGSIZE];
    recv(dsC, nomFic, sizeof(nomFic), 0);
    // créer le fichier avec le meme nom
    FILE *fp = fopen(nomFic, "w+");
    if(fp == NULL){
        perror("fichier");
    }else{
        getFile(fp, dsC);
        fclose(fp);
    }
    sem_post(&semaphoreFile);
    shutdown(dsC, 2);

    pthread_mutex_lock(&mutexThreads);

    nbThreadsFini++;
    threadsFini = realloc(threadsFini, sizeof(pthread_t) * nbThreadsFini);
    threadsFini[nbThreadsFini - 1] = thread;

    pthread_mutex_unlock(&mutexThreads);

    pthread_exit(0);
}

/**
  * @brief Procédure executée par un thread pour envoyer la liste des fichiers a un client
  *
  * @param s Pointeur vers le descripteur de la socket sur laquelle envoyer la liste
*/

void *listerFichiers(void *s){
    struct infoClients *info = (struct infoClients*) s;
    int dS = (*info).dS;
    pthread_t thread = (*info).thread;

    pthread_mutex_lock(&mutexThreads);
    nbThreadsExec++;
    threadsExec = realloc(threadsExec, sizeof(pthread_t) * nbThreadsExec);
    threadsExec[nbThreadsExec-1] = thread;
    pthread_mutex_unlock(&mutexThreads);

    // Liste les fichiers
    DIR *dp;
    struct dirent *ep;
    dp = opendir ("./");
    if (dp != NULL) {
        while (ep = readdir (dp)) {
            if(strcmp(ep->d_name,".")!=0 && strcmp(ep->d_name,"..")!=0){
                send(dS, &ep->d_name, MSGSIZE, 0);
            }
        }
        (void) closedir (dp);
    }
    else {
        perror ("Ne peux pas ouvrir le répertoire");
    }
    shutdown(dS, 2);
    pthread_mutex_lock(&mutexThreads);

    nbThreadsFini++;
    threadsFini = realloc(threadsFini, sizeof(pthread_t) * nbThreadsFini);
    threadsFini[nbThreadsFini - 1] = thread;

    pthread_mutex_unlock(&mutexThreads);
    pthread_exit(0);

}

/**
  * @brief Procédure executée par un thread pour envoyer un fichier à un client
  *
  * @param s Pointeur vers le descripteur de la socket sur laquelle envoyer le fichier

*/

void *transmettreFichier(void *s){

    struct infoClients *info = (struct infoClients*) s;
    int dS = (*info).dS;
    pthread_t thread = (*info).thread;

    pthread_mutex_lock(&mutexThreads);
    nbThreadsExec++;
    threadsExec = realloc(threadsExec, sizeof(pthread_t) * nbThreadsExec);
    threadsExec[nbThreadsExec-1] = thread;

    pthread_mutex_unlock(&mutexThreads);

    char nomFic[MSGSIZE];
    recv(dS, &nomFic, sizeof(nomFic), 0);
    FILE * fp;
    do{
      fp = fopen(nomFic, "r");
    }while(fp == NULL);
    if(fp == NULL){
        perror("Fichier inexistant");
    }else{
        sendFile(fp, dS);
        fclose(fp);
    }

    shutdown(dS, 2);
    pthread_mutex_lock(&mutexThreads);

    nbThreadsFini++;
    threadsFini = realloc(threadsFini, sizeof(pthread_t) * nbThreadsFini);
    threadsFini[nbThreadsFini - 1] = thread;

    pthread_mutex_unlock(&mutexThreads);
    pthread_exit(0);
}

/**
  * @brief Procédure executée par le thread attendant une connexion d'un client pour envoyer ou récupérer un fichier
  *
  * @param s Pointeur vers le descripteur de la socket sur laquelle attendre un fichier client
*/
void *acceptFile(void* s){
    struct infoClients *info = (struct infoClients*) s;
    int dS = (*info).dS;
    pthread_t thread = (*info).thread;
    free(info);
    struct sockaddr_in aC ;
    socklen_t lg = sizeof(struct sockaddr_in);

    long i = 0;
    while(1){
        /* opération P : pour se connecter*/
        sem_wait(&semaphoreFile);

        long dsC= accept(dSFile, (struct sockaddr*) &aC, &lg) ;
        if(dsC == -1){
            perror("Erreur connexion client ");
            exit(1);
        }
        // identifiant de l'action pour savoir quelle fonction executer
        int id;
        recv(dsC, &id, sizeof(id), 0);
        struct infoClients* info;
        info = malloc(sizeof(struct infoClients));
        (*info).dS = dsC;

        pthread_mutex_lock(&mutexThreads);
        nbThreadsExec++;
        threadsExec = realloc(threadsExec, sizeof(pthread_t) * nbThreadsExec);
        threadsExec[nbThreadsExec-1] = thread;

        pthread_mutex_unlock(&mutexThreads);

        if(id == 1){ // liste fichiers
            if( pthread_create(&(*info).thread, NULL, listerFichiers,(void*) info)) {
                perror("Erreur création thread liste fichier");
            }
        }else if(id == 2){ // envoi un fichier
            if( pthread_create(&(*info).thread, NULL, transmettreFichier, (void*) info)) {
                perror("Erreur création thread envoi fichier");
            }
        }else{
            /*Dès qu'un client se connecte on lance un thread */
            if( pthread_create(&(*info).thread, NULL, receptionFichier, (void*) info)) {
                perror("Erreur création thread reception fichier");
            }
        }
        i++;
    }
}


/**
  * @brief Procédure executé lorsqu'un signal de terminaison est reçu
  *
  * @param code Code du signal reçu
*/
void sigTerm( int code ){
    for(int i = 0 ; i < nbThreadsExec ; i++){
        pthread_cancel(threadsExec[i]);
    }
    for(int i = 0 ; i < nbThreadsExec ; i++){
        pthread_join(threadsExec[i], NULL);
    }
    // free

    free(threadsExec);
    free(threadsFini);
    for(int i = 0 ; i < n ; i ++){
        free(listeChaines[i].listeClients);
    }
    free(listeChaines);

    
    shutdown(dSMsg, 2) ;
    shutdown(dSFile, 2) ;

    exit(0);
}

int main(int argc, char *argv[]) {

    if(argc != 4){
        printf("\033[31mMauvais nombre d'arguments\033[00m\n");
        exit(1);
    }

    signal( SIGTERM, &sigTerm );
    signal( SIGINT, &sigTerm );

    //N c'est le nombre max de chaînes
    n = atoi(argv[2]);

    // creation de la socket
    dSMsg = creationSocket(htons(atoi(argv[1])));

    // Création chaîne par défaut
    listeChaines=malloc(sizeof(struct chaine)*n);

    struct chaine home;

    strcpy(home.nomChaine ,"Home");
    strcpy(home.description , "Welcome to the best chat channel");
    home.m = 100;
    home.nb = 0;
    home.listeClients=malloc(sizeof(struct client)*home.m);

    listeChaines[0]=home;
    nChaines=1;

    // lis le fichier des chaines
    FILE * fp;
    fp = fopen("Chaines.txt", "r");
    if(fp == NULL){
        fp = fopen("Chaines.txt","w");
        fclose(fp);
    }else{
        char * nomChaine;
        int m;
        char * desc;
        char data[MSGSIZE];
        while(fgets(data, sizeof(data), fp)!= NULL && nChaines<n){
            nomChaine = strtok(data,";");
            m = atoi(strtok(NULL, ";"));
            desc = strtok(NULL, ";");
            desc[strlen(desc) - 1] = '\0';
            struct chaine recChaine;
            strcpy(recChaine.nomChaine ,nomChaine);
            strcpy(recChaine.description , desc);
            recChaine.m = m;
            recChaine.nb = 0;
            recChaine.listeClients=malloc(sizeof(struct client)*recChaine.m);

            listeChaines[nChaines]=recChaine;
            nChaines++;

        }
        fclose(fp);
    }


    dSFile = creationSocket(htons(atoi(argv[1])+1));
    printf("\033[34m-- Serveur lancé --\033[00m\n");
    printf("\033[33mAttente de clients...\033[00m\n");

    /*initialisation du semaphore declaré en global*/
    sem_init(&semaphore, PTHREAD_PROCESS_SHARED, atoi(argv[3]));

    /*initialisation du semaphore pour le transfert de fichier*/
    sem_init(&semaphoreFile, PTHREAD_PROCESS_SHARED, atoi(argv[3]));

    // lancement du serveur
    struct infoClients* info;
    info = malloc(sizeof(struct infoClients));
    (*info).dS = dSFile;

    if( pthread_create(&(*info).thread, NULL, acceptFile, (void *) info) != 0){
        perror("Erreur création thread file");
    }
    acceptClients();

    pthread_join((*info).thread, NULL);
    shutdown(dSMsg, 2) ;
    shutdown(dSFile, 2) ;
    return 0;
}