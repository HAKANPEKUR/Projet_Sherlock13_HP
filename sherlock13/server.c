#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>

// Structure pour stocker les informations du client
struct _client
{
    char ipAddress[40];
    int port;
    char name[40];
    int active; // 1 si le joueur est actif, 0 s'il a raté une accusation
    int sockfd;
} tcpClients[4];

int nbClients;
int fsmServer; // State machine: 0=attente connexions, 1=jeu en cours, 2=fin de partie
int deck[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
int tableCartes[4][8];
char *nomcartes[] =
    {"Sebastian Moran", "irene Adler", "inspector Lestrade",
     "inspector Gregson", "inspector Baynes", "inspector Bradstreet",
     "inspector Hopkins", "Sherlock Holmes", "John Watson", "Mycroft Holmes",
     "Mrs. Hudson", "Mary Morstan", "James Moriarty"};
int joueurCourant;
int coupable;

void error(const char *msg)
{
    perror(msg);
    exit(1);
}

// Mélange le paquet de cartes
void melangerDeck()
{
    int i;
    int index1, index2, tmp;

    srand(time(NULL));

    for (i = 0; i < 1000; i++)
    {
        index1 = rand() % 13;
        index2 = rand() % 13;

        tmp = deck[index1];
        deck[index1] = deck[index2];
        deck[index2] = tmp;
    }
    coupable = deck[12];
    printf("Le coupable est: %s (%d)\n", nomcartes[coupable], coupable);
}

// Calcule les symboles pour chaque joueur basé sur les cartes distribuées
void createTable()
{
    // Le joueur 0 possede les cartes d'indice 0,1,2
    // Le joueur 1 possede les cartes d'indice 3,4,5
    // Le joueur 2 possede les cartes d'indice 6,7,8
    // Le joueur 3 possede les cartes d'indice 9,10,11
    // Le coupable est la carte d'indice 12
    int i, j, c;

    // Initialise tableCartes
    for (i = 0; i < 4; i++)
        for (j = 0; j < 8; j++)
            tableCartes[i][j] = 0;

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 3; j++)
        {
            c = deck[i * 3 + j];

            switch (c)
            {
            case 0: // Sebastian Moran
                tableCartes[i][7]++; // Crane
                tableCartes[i][2]++; // Poing
                break;
            case 1: // Irene Adler
                tableCartes[i][7]++; // Crane
                tableCartes[i][1]++; // Ampoule
                tableCartes[i][5]++; // Collier
                break;
            case 2: // Inspector Lestrade
                tableCartes[i][3]++; // Couronne
                tableCartes[i][6]++; // Oeil
                tableCartes[i][4]++; // Carnet
                break;
            case 3: // Inspector Gregson
                tableCartes[i][3]++; // Couronne
                tableCartes[i][2]++; // Poing
                tableCartes[i][4]++; // Carnet
                break;
            case 4: // Inspector Baynes
                tableCartes[i][3]++; // Couronne
                tableCartes[i][1]++; // Ampoule
                break;
            case 5: // Inspector Bradstreet
                tableCartes[i][3]++; // Couronne
                tableCartes[i][2]++; // Poing
                break;
            case 6: // Inspector Hopkins
                tableCartes[i][3]++; // Couronne
                tableCartes[i][0]++; // Pipe
                tableCartes[i][6]++; // Oeil
                break;
            case 7: // Sherlock Holmes
                tableCartes[i][0]++; // Pipe
                tableCartes[i][1]++; // Ampoule
                tableCartes[i][2]++; // Poing
                break;
            case 8: // John Watson
                tableCartes[i][0]++; // Pipe
                tableCartes[i][6]++; // Oeil
                tableCartes[i][2]++; // Poing
                break;
            case 9: // Mycroft Holmes
                tableCartes[i][0]++; // Pipe
                tableCartes[i][1]++; // Ampoule
                tableCartes[i][4]++; // Carnet
                break;
            case 10: // Mrs. Hudson
                tableCartes[i][0]++; // Pipe
                tableCartes[i][5]++; // Collier
                break;
            case 11: // Mary Morstan
                tableCartes[i][4]++; // Carnet
                tableCartes[i][5]++; // Collier
                break;
            case 12: // James Moriarty
                tableCartes[i][7]++; // Crane
                tableCartes[i][1]++; // Ampoule
                break;
            }
        }
    }
}

// Affiche le contenu du deck et la table des symboles
void printDeck()
{
    int i, j;
    printf("Deck mélangé:\n");
    for (i = 0; i < 13; i++)
        printf("%d %s\n", deck[i], nomcartes[deck[i]]);

    printf("\nTable des symboles par joueur:\n");
    printf("       Pi Am Po Co Ca Cl Oe Cr\n");
    for (i = 0; i < 4; i++)
    {
        printf("Joueur %d: ", i);
        for (j = 0; j < 8; j++)
            printf("%2.2d ", tableCartes[i][j]);
        puts("");
    }
    printf("Carte Criminel: %s\n", nomcartes[coupable]);
}

// Affiche les informations des clients connectés
void printClients()
{
    int i;
    printf("Clients connectés (%d):\n", nbClients);
    for (i = 0; i < nbClients; i++)
        printf("%d: %s %5.5d %s (Actif: %d)\n", i, tcpClients[i].ipAddress,
               tcpClients[i].port,
               tcpClients[i].name, tcpClients[i].active);
}

// Trouve l'indice d'un client par son nom
int findClientByName(char *name)
{
    int i;
    for (i = 0; i < nbClients; i++)
        if (strcmp(tcpClients[i].name, name) == 0)
            return i;
    return -1; // Non trouvé
}

// Envoie un message à un client spécifique
void sendMessageToClient(char *clientip, int clientport, char *mess)
{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[256];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERREUR ouverture socket pour envoi message");
        return; // Ne pas quitter tout le serveur pour une erreur d'envoi
    }

    server = gethostbyname(clientip);
    if (server == NULL) {
        fprintf(stderr, "ERREUR, hote %s non trouvé\n", clientip);
        close(sockfd);
        return;
    }

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(clientport);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERREUR connexion pour envoi message");
        // Peut arriver si le client se déconnecte subitement
    } else {
        snprintf(buffer, sizeof(buffer),"%s\n", mess); // Utiliser snprintf pour sécurité
        if (write(sockfd, buffer, strlen(buffer)) < 0) {
            perror("ERREUR écriture socket pour envoi message");
        }
    }
    close(sockfd);
}

// Envoie un message à tous les clients connectés
void broadcastMessage(char *mess)
{
    int i;
    for (i = 0; i < nbClients; i++)
        sendMessageToClient(tcpClients[i].ipAddress,
                            tcpClients[i].port,
                            mess);
}

// Trouve le prochain joueur actif
int getNextActivePlayer(int current) {
    int nextPlayer;
    int activePlayers = 0;
    for(int i=0; i<nbClients; ++i) {
        if(tcpClients[i].active) activePlayers++;
    }
    // S'il ne reste qu'un joueur ou moins, le jeu est terminé (ou va se terminer)
    if (activePlayers <= 1) return -1;

    nextPlayer = (current + 1) % nbClients;
    // Boucle tant qu'on n'a pas trouvé un joueur actif (évite boucle infinie si tous inactifs)
    while (!tcpClients[nextPlayer].active && nextPlayer != current) {
        nextPlayer = (nextPlayer + 1) % nbClients;
    }
     // Si on a fait un tour complet sans trouver d'autre joueur actif, retourner -1
     if (!tcpClients[nextPlayer].active) return -1;

    return nextPlayer;
}


int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno;
    socklen_t clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    int i, j;

    char com;
    char clientIpAddress[256], clientName[256];
    int clientPort;
    int id;
    char reply[256];
    int playerId, objectId, targetPlayerId, suspectId; // Pour parser les messages

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    // Permet de réutiliser l'adresse immédiatement après l'arrêt du serveur
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    bzero((char *)&serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");

    listen(sockfd, 5); // Accepte jusqu'à 5 connexions en attente (4 joueurs + marge)
    clilen = sizeof(cli_addr);

    printf("Serveur Sherlock 13 démarré sur le port %d\n", portno);

    // Initialisation
    melangerDeck();
    createTable();
    printDeck(); // Afficher l'état initial pour le débogage
    joueurCourant = 0; // Le premier joueur est l'indice 0
    nbClients = 0;
    fsmServer = 0; // Attente des connexions

    for (i = 0; i < 4; i++)
    {
        strcpy(tcpClients[i].ipAddress, "localhost"); // Default
        tcpClients[i].port = -1;
        strcpy(tcpClients[i].name, "-");
        tcpClients[i].active = 0; // Inactif au début
        tcpClients[i].sockfd = -1;
    }

    // Boucle principale du serveur
    while (fsmServer != 2) // Boucle tant que le jeu n'est pas terminé
    {
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0) {
             if (fsmServer != 2) error("ERROR on accept"); // Ne pas crasher si on arrête le serveur
             else break; // Sortir proprement si fsmServer est à 2
        }

        bzero(buffer, 256);
        n = read(newsockfd, buffer, 255);
        if (n < 0) {
            if (fsmServer != 2) error("ERROR reading from socket");
            else { close(newsockfd); break; }
        }
        if (n == 0) {
             printf("Client disconnected before sending data.\n");
             close(newsockfd);
             continue; // Attend la prochaine connexion/message
        }

        // Supprimer le newline potentiel à la fin du buffer
        buffer[strcspn(buffer, "\n")] = 0;

        printf("Paquet reçu de %s:%d\nDonnées: [%s]\n\n",
               inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port), buffer);

        // --- État 0: Attente des connexions ---
        if (fsmServer == 0)
        {
            if (buffer[0] == 'C')
            {
                if (nbClients < 4) {
                    sscanf(buffer, "%c %s %d %s", &com, clientIpAddress, &clientPort, clientName);
                    printf("COMMANDE='C' ipAddress=%s port=%d name=%s\n", clientIpAddress, clientPort, clientName);

                    // Vérifier si le nom est déjà pris
                    if (findClientByName(clientName) != -1) {
                        printf("Erreur: Nom '%s' déjà pris.\n", clientName);
                    } else {
                        // Enregistrer le nouveau client
                        strcpy(tcpClients[nbClients].ipAddress, clientIpAddress);
                        tcpClients[nbClients].port = clientPort;
                        strcpy(tcpClients[nbClients].name, clientName);
                        tcpClients[nbClients].active = 1; // Le joueur est actif au début
                        tcpClients[nbClients].sockfd = newsockfd;
                        nbClients++;

                        printClients();

                        // Trouver l'ID du joueur qui vient de se connecter
                        id = findClientByName(clientName);
                        printf("id=%d\n", id);

                        // Lui envoyer un message personnel pour lui communiquer son ID
                        sprintf(reply, "I %d", id);
                        sendMessageToClient(tcpClients[id].ipAddress, tcpClients[id].port, reply);

                        // Envoyer un message broadcast pour communiquer à tout le monde la liste des joueurs
                        sprintf(reply, "L %s %s %s %s", tcpClients[0].name, tcpClients[1].name, tcpClients[2].name, tcpClients[3].name);
                        broadcastMessage(reply);

                        // Si le nombre de joueurs atteint 4, lancer le jeu
                        if (nbClients == 4)
                        {
                            printf("4 joueurs connectés. Début de la partie !\n");
                            // Envoyer les cartes et les tables de symboles à chaque joueur
                            for (id = 0; id < 4; id++) {
                                // Envoyer les 3 cartes du joueur (indices du deck)
                                sprintf(reply, "D %d %d %d", deck[id * 3 + 0], deck[id * 3 + 1], deck[id * 3 + 2]);
                                sendMessageToClient(tcpClients[id].ipAddress, tcpClients[id].port, reply);

                                // Envoyer la ligne correspondante de tableCartes (les totaux de symboles du joueur)
                                for(j=0; j<8; j++) {
                                     // Envoyer V <id_joueur_concerné> <id_symbole> <valeur>
                                    sprintf(reply, "V %d %d %d", id, j, tableCartes[id][j]);
                                     sendMessageToClient(tcpClients[id].ipAddress, tcpClients[id].port, reply);
                                }
                            }

                            // Envoyer à tout le monde qui est le joueur courant (joueur 0)
                            joueurCourant = 0; // Assurer que c'est bien 0
                            sprintf(reply, "M %d", joueurCourant);
                            broadcastMessage(reply);

                            fsmServer = 1; // Passer à l'état "jeu en cours"
                            printf("Transition vers l'état 1 (Jeu en cours). C'est au tour de %s\n", tcpClients[joueurCourant].name);
                        }
                    }
                } else {
                    printf("Connexion refusée: Nombre maximum de joueurs (4) atteint.\n");
                }
            } else {
                 printf("Commande inconnue reçue dans l'état 0: %c\n", buffer[0]);
            }
        }
        // --- État 1: Jeu en cours ---
        else if (fsmServer == 1)
        {
             // Variable senderId supprimée car non utilisée
            switch (buffer[0])
            {
            case 'G': // Accusation (Guess) - G <playerId> <suspectId>
                if (sscanf(buffer, "G %d %d", &playerId, &suspectId) == 2) {
                    printf("Commande 'G': Joueur %d (%s) accuse %s (%d)\n", playerId, tcpClients[playerId].name, nomcartes[suspectId], suspectId);
                    if (playerId == joueurCourant && tcpClients[playerId].active) {
                        if (suspectId == coupable) {
                            // Bonne accusation ! Fin de la partie
                            printf("Accusation CORRECTE ! Le joueur %d (%s) a gagné !\n", playerId, tcpClients[playerId].name);
                            sprintf(reply, "W %d %s %d", playerId, nomcartes[coupable], coupable); // W <winnerId> <coupableName> <coupableId>
                            broadcastMessage(reply);
                            fsmServer = 2; // Fin de partie
                        } else {
                            // Mauvaise accusation
                            printf("Accusation INCORRECTE ! Le joueur %d (%s) est éliminé.\n", playerId, tcpClients[playerId].name);
                            tcpClients[playerId].active = 0; // Désactiver le joueur
                            sprintf(reply, "F %d", coupable); // F <coupableId> (Failed)
                            sendMessageToClient(tcpClients[playerId].ipAddress, tcpClients[playerId].port, reply);

                            // Vérifier s'il reste un seul joueur actif
                            int activeCount = 0;
                            int lastPlayerId = -1;
                            for(i=0; i<nbClients; ++i) {
                                if (tcpClients[i].active) {
                                    activeCount++;
                                    lastPlayerId = i;
                                }
                            }

                            if (activeCount <= 1) {
                                if (activeCount == 1) {
                                     printf("Il ne reste plus qu'un joueur actif (%s). Fin de la partie !\n", tcpClients[lastPlayerId].name);
                                     sprintf(reply, "W %d %s %d", lastPlayerId, nomcartes[coupable], coupable); // Le dernier joueur gagne
                                     broadcastMessage(reply);
                                } else {
                                     // Cas où tout le monde a échoué en même temps ? Improbable.
                                     printf("Tous les joueurs sont inactifs. Personne ne gagne.\n");
                                      sprintf(reply, "W -1 %s %d", nomcartes[coupable], coupable); // Indiquer match nul ou coupable
                                      broadcastMessage(reply);
                                }
                                fsmServer = 2;
                            } else {
                                // Passer au joueur suivant
                                joueurCourant = getNextActivePlayer(joueurCourant);
                                if (joueurCourant != -1) {
                                    printf("C'est maintenant au tour de %s (%d)\n", tcpClients[joueurCourant].name, joueurCourant);
                                    sprintf(reply, "M %d", joueurCourant);
                                    broadcastMessage(reply);
                                } else {
                                    // Ne devrait pas arriver ici si activeCount > 1
                                     printf("Erreur: Aucun joueur actif suivant trouvé.\n");
                                     fsmServer = 2; // Terminer par sécurité
                                }
                            }
                        }
                    } else {
                         printf("Action 'G' reçue mais ce n'est pas le tour du joueur %d ou il est inactif.\n", playerId);
                    }
                } else {
                     printf("Format de message 'G' invalide: %s\n", buffer);
                }
                break;

            case 'O': // Enquête 1 (Objet) - O <playerId> <objetId>
                 if (sscanf(buffer, "O %d %d", &playerId, &objectId) == 2) {
                     printf("Commande 'O': Joueur %d (%s) demande qui a l'objet %d\n", playerId, tcpClients[playerId].name, objectId);
                     if (playerId == joueurCourant && tcpClients[playerId].active) {
                         int found = 0;
                         // Construire une réponse pour le joueur enquêteur
                         // Format R <type_enquete> [<joueurId_ayant>...]
                         // On envoie V <joueur_qui_a> <objetId> 100 (pour signifier "a l'objet")

                         for (i = 0; i < nbClients; i++) {
                             if (i != playerId && tcpClients[i].active) { // Ne vérifie pas le joueur lui-même ni les inactifs
                                 if (tableCartes[i][objectId] > 0) {
                                     printf("  -> Le joueur %d (%s) a l'objet %d.\n", i, tcpClients[i].name, objectId);
                                     sprintf(reply, "V %d %d %d", i, objectId, 100); // Utilise 100 comme indicateur "possède"
                                     sendMessageToClient(tcpClients[playerId].ipAddress, tcpClients[playerId].port, reply);
                                     found++; // Compter combien l'ont
                                 }
                             }
                         }

                         // Passer au joueur suivant
                         joueurCourant = getNextActivePlayer(joueurCourant);
                         if (joueurCourant != -1) {
                             printf("C'est maintenant au tour de %s (%d)\n", tcpClients[joueurCourant].name, joueurCourant);
                             sprintf(reply, "M %d", joueurCourant);
                             broadcastMessage(reply);
                         } else {
                             printf("Erreur/Fin: Aucun joueur actif suivant trouvé après enquête O.\n");
                             fsmServer = 2; // Terminer par sécurité ou gérer la victoire du dernier
                         }
                     } else {
                          printf("Action 'O' reçue mais ce n'est pas le tour du joueur %d ou il est inactif.\n", playerId);
                     }
                 } else {
                     printf("Format de message 'O' invalide: %s\n", buffer);
                 }
                break;

            case 'S': // Enquête 2 (Spécifique) - S <playerId> <targetPlayerId> <objetId>
                 if (sscanf(buffer, "S %d %d %d", &playerId, &targetPlayerId, &objectId) == 3) {
                     printf("Commande 'S': Joueur %d (%s) demande au joueur %d (%s) combien il a de l'objet %d\n",
                            playerId, tcpClients[playerId].name, targetPlayerId, tcpClients[targetPlayerId].name, objectId);

                    if (playerId == joueurCourant && tcpClients[playerId].active) {
                        // On vérifie que la cible est valide (même si inactive, elle doit répondre)
                        if (targetPlayerId >= 0 && targetPlayerId < nbClients ) {
                            int count = tableCartes[targetPlayerId][objectId];
                            printf("  -> Le joueur %d a %d exemplaire(s) de l'objet %d.\n", targetPlayerId, count, objectId);
                            // Envoyer la réponse seulement au joueur qui a demandé
                            sprintf(reply, "V %d %d %d", targetPlayerId, objectId, count);
                            sendMessageToClient(tcpClients[playerId].ipAddress, tcpClients[playerId].port, reply);

                             // Passer au joueur suivant
                             joueurCourant = getNextActivePlayer(joueurCourant);
                             if (joueurCourant != -1) {
                                printf("C'est maintenant au tour de %s (%d)\n", tcpClients[joueurCourant].name, joueurCourant);
                                sprintf(reply, "M %d", joueurCourant);
                                broadcastMessage(reply);
                             } else {
                                 printf("Erreur/Fin: Aucun joueur actif suivant trouvé après enquête S.\n");
                                 fsmServer = 2; // Terminer par sécurité ou gérer la victoire du dernier
                             }

                        } else {
                            printf("  -> Joueur cible (%d) invalide.\n", targetPlayerId);
                             joueurCourant = getNextActivePlayer(joueurCourant);
                             if (joueurCourant != -1) {
                                printf("C'est maintenant au tour de %s (%d)\n", tcpClients[joueurCourant].name, joueurCourant);
                                sprintf(reply, "M %d", joueurCourant);
                                broadcastMessage(reply);
                             } else { fsmServer = 2;}

                        }
                    } else {
                         printf("Action 'S' reçue mais ce n'est pas le tour du joueur %d ou il est inactif.\n", playerId);
                    }
                 } else {
                     printf("Format de message 'S' invalide: %s\n", buffer);
                 }
                break;

            default:
                 printf("Commande inconnue reçue dans l'état 1: %c\n", buffer[0]);
                break;
            }
        }

        // Fermer le socket de connexion temporaire après traitement du message
        if (newsockfd >= 0) close(newsockfd);

    }

    printf("Fin de la partie ou arrêt du serveur.\n");
    // Fermer la socket d'écoute principale
    if (sockfd >= 0) close(sockfd);

    return 0;
}