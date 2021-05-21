/**
 * \file client.c
 * \brief Programme du client
 * \author 
 * \version 0.1
 * \date 21 Mai 2021
 *
 * Programme du client
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
#include <signal.h>
#include <dirent.h>
#include <limits.h>
#include "files.h"
#define MSGSIZE 1024

// Structures pour envoyer plusieurs variables aux threads
struct descripteurs{
    long dS;
    char* ip;
    int port;
};

struct transfert{
    long dS;
    char* nomFic;
};

/**
  * @brief Fonction permettant la création de socket et la communication au serveur
  *
  * @param adresse adresse ip du serveur
  * @param port port sur lequel se connecte le serveur
  * @return Retourne le descripteur de la socket
*/
int creationSocket(char* adresse, int port){
    /* Nommer la socket */
    int dS = socket(PF_INET, SOCK_STREAM, 0);
    if(dS == -1){
        perror("Erreur socket");
        exit(1);
    }
    struct sockaddr_in aS;
    aS.sin_family = AF_INET;
    int inet = inet_pton(AF_INET,adresse,&(aS.sin_addr));
    if ( inet == -1){
        perror("Erreur inet");
        exit(1);
    }else if( inet == 0){
        perror("Adresse non valide");
        exit(1);
    }

    /*récuperation du port donné en argument*/
    aS.sin_port = port;

    socklen_t lgA = sizeof(struct sockaddr_in) ;

    /* Demander une connexion */
    if( connect(dS, (struct sockaddr *) &aS, lgA) == -1){
        perror("Erreur connect");
        exit(1);
    }

    return dS;
}


/**
  * @brief Procédure executée par un thread pour envoyer un fichier au serveur
  *
  *@param s Pointeur vers une structure ayant pour informations : La socket attendant des fichiers ainsi que le nom du fichier à envoyer

*/
void *transfert(void *s){
    struct transfert *t = (struct transfert*) s;
    long dS = (*t).dS;
    char* nomFic = (*t).nomFic;

    FILE * fp;
    fp = fopen(nomFic, "r");
    if(fp == NULL){
        perror("Fichier inexistant");
    }else{
        send(dS, nomFic, MSGSIZE, 0);
        sendFile(fp, dS);
        puts("Fichier envoyé avec succès");
    }
    fclose(fp);
    shutdown(dS, 2);
    pthread_exit(0);
}

/**
  * @brief Procédure executée par un thread pour demander la liste des fichiers au serveur et l'afficher
  *
  * param s Pointeur vers le descripteur de la socket pour les fichiers

*/
void *demandeListeFic(void *s){
    long dS = (long)s;
    char rec[MSGSIZE];

    puts("\nListe des fichiers :");
    while(recv(dS, rec, sizeof(rec), 0) > 0){
        puts(rec);
    }

    shutdown(dS, 2);
    pthread_exit(0);
}
/**
  * @brief procédure executée par un thread pour recevoir un fichier depuis le serveur
  *
  * @param s Pointeur vers une structure ayant pour informations : La socket attendant des fichiers ainsi que le nom du fichier à envoyer
*/
void *telecharger(void *s){
    struct transfert *t = (struct transfert*) s;
    long dS = (*t).dS;
    char* nomFic = (*t).nomFic;
    char rec[MSGSIZE];

    send(dS, nomFic, sizeof(rec), 0);

    // créer le fichier avec le meme nom
    FILE *fp = fopen(nomFic, "w+");
    if(fp == NULL){
        perror("fichier");
        exit(1);
    }else{
        getFile(fp, dS);

        puts("Fichier téléchargé avec succès");
    }
    fclose(fp);

    shutdown(dS, 2);
    pthread_exit(0);
}
/**
  * @brief procédure du thread permettant l'émission d'un message vers le serveur
  *
  * @param s pointeur vers le descripteur de la socket permettant la communication avec le serveurr
*/
void *emission(void *s){
    struct descripteurs *desc = (struct descripteurs*) s;
    int dS = (long)(*desc).dS;
    char* ip = (*desc).ip;
    int port = (*desc).port;
    char env[MSGSIZE];
    char pseudo[20];
    env[0] = '\0';
    while(strcmp(env, "FIN") != 0){
        fgets(env,MSGSIZE,stdin);
        char *pos = strchr(env, '\n');
        *pos = '\0';
        if(strcmp(env, "FIN") != 0 && strcmp(env, "") != 0){
            // Strtok pour récupérer si file
            char envCopy[MSGSIZE];
            strcpy(envCopy,env);
            char * strToken = strtok ( envCopy, " " );
            // FILE nomfichier -> Envoi au serveur
            // FILE list -> Liste des fichiers sur le serveur
            // FILE get nomfichier -> Télécharge depuis le serveur
            if(strcmp(strToken,"/file")==0){

                strToken = strtok (NULL, " " ); // on récupère ce qu'il y a après FILE

                int id;
                struct transfert *t;
                long dSFile = creationSocket(ip, htons(port+1));
                t = malloc(sizeof(struct transfert));
                (*t).dS = dSFile;
                pthread_t threadFile;
                if(strcmp(strToken,"list")==0){ //si le client demande une liste des fichiers

                    id=1; // Permet au serveur de savoir qu'on veut une liste de fichiers
                    send(dSFile, &id, sizeof(id), 0);

                    pthread_create(&threadFile, NULL, demandeListeFic, (void *) dSFile);

                }else if(strcmp(strToken,"get")==0){
                    strToken = strtok (NULL, " " ); //nomFichier à telecharger
                    (*t).nomFic = malloc(sizeof(strToken));
                    strcpy((*t).nomFic, strToken);

                    id=2; // Permet au serveur de savoir qu'on veut un fichier
                    send(dSFile, &id, sizeof(id), 0);

                    pthread_create(&threadFile, NULL, telecharger, (void *) t );
                }
                else{
                    (*t).nomFic = malloc(sizeof(strToken));
                    strcpy((*t).nomFic, strToken);
                    // lire le fichier


                    id=0; // Permet au serveur de savoir qu'on envoie un fichier
                    send(dSFile, &id, sizeof(id), 0);

                    pthread_create(&threadFile, NULL, transfert, (void *) t );
                }
                pthread_join(threadFile, NULL);
                free(t);
            }
            // sinon comportement par défaut
            else{
                if(send(dS, env, sizeof(env), 0) == -1) break ;
            }

        }
    }
    /* Fin de la communication avec le serveur */
    printf("Fermeture de la socket\n");
    shutdown(dS,2);
    pthread_exit(0);

}

/**
  * @brief Procédure du thread permettant à la réception d'un message du serveur
  *
  * @param s pointeur vers le descripteur de la socket permettant la communication avec le serveur
*/
void *reception(void *s){
    int dS = (long) s;

    char rec[MSGSIZE];
    while ( recv(dS, rec, MSGSIZE, 0) > 0){

        puts(rec);
        bzero(rec,sizeof(rec));
    }
    pthread_exit(0);

}

/**
  * @brief Procédure demandant au client un pseudo et le faisant vérifier au serveur
  *
  * @param dS Descripteur de la socket du serveur
*/
void saisiPseudo(int dS){
    int reponse = 1;
    char pseudo[20];
    while(reponse == 1){
        printf("Veuillez saisir votre pseudo :\n");
        fgets(pseudo,20,stdin);
        char *pos = strchr(pseudo, '\n');
        *pos = '\0';
        if(send(dS, pseudo, sizeof(pseudo), 0) <= 0){
            /*envoi du pseudo au serveur*/
            perror("Erreur send pseudo");
            exit(1);
        }
        if(recv(dS, &reponse, sizeof(reponse), 0) <= 0 ){
            perror("Erreur recv pseudo");
            exit(1);
        }

        if(reponse == 1) printf("Pseudo refusé par le serveur\n");

    }
    printf("Welcome %s\n", pseudo);
}

int main(int argc, char *argv[]) {
	/*vérification bon nombre d'arg*/
    if(argc != 3){
        perror("Mauvais nombre d'arguments");
        exit(1);
    }

    /*récuperation création socket*/
    long dS = creationSocket(argv[1], htons(atoi(argv[2])));
    printf("-- Connexion établie avec le serveur --\n");

    struct descripteurs *desc;
    desc = malloc(sizeof(struct descripteurs));
    (*desc).dS = dS;
    (*desc).ip = argv[1];
    (*desc).port = atoi(argv[2]);

    saisiPseudo(dS);

    // Creation des threads
    pthread_t thread[2];

    pthread_create(&thread[0], NULL, emission, (void*)desc);
    pthread_create(&thread[1], NULL, reception, (void*)dS);

    // Attente de la fin des threads
    pthread_join(thread[0], NULL);
    pthread_join(thread[1], NULL);
    free(desc);


    printf("Fin de la communication\n");

}
