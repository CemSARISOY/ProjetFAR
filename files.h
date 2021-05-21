/**
 * \file files.h
 * \brief Header files
 * \author 
 * \version 0.1
 * \date 21 Mai 2021
 *
 * Header contenant des fonctions utilisées par le serveur et le client
 *
 */



/** 
  * @brief Procédure récupérant un fichier à travers une socket, le fichier est créé s'il n'existe pas et est modifié 
  *
  * @param fp Le descripteur du fichier dans lequel écrire
  * @param dS descripteur de la socket dans laquelle récupérer les informations du fichier
*/
void getFile(FILE* fp, int dS){
    char ligne[1024];
    while (1) {
        bzero(ligne, sizeof(ligne));
        int t = recv(dS, ligne, sizeof(ligne), 0);
        if (t < 0){
            perror("Erreur receive fichier");
            exit(1);
        }else if( t == 0){
            break;
        }
        fwrite(ligne, 1, t, fp);
    }
}
/**
  * @brief procedure  envoyant un fichier à travers une socket
  *
  * @param fp descripteur du fichier dans lequel lire
  * @param dS descripteur de la socket dans laquelle envoyer les informations du fichier
  * @param j Entier à modifier pour indiquer la position du client dans la chaine
*/
void sendFile(FILE* fp, int dS){
    char data[1024];
    int nb = fread(data, 1, sizeof(data), fp);
    while(!feof(fp)){
        if (send(dS, data, nb, 0) == -1) {
            perror("Erreur send file");
            exit(1);
        }
        bzero(data, sizeof(data));
        nb = fread(data, 1, sizeof(data), fp);
    }
    if (send(dS, data, nb, 0) == -1) {
        perror("Erreur send file");
        exit(1);
    }
}