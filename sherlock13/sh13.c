#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

// --- Constantes pour la limitation des chaînes dans snprintf ---
#define MAX_NAME_LEN 64
#define MAX_IP_LEN 40
#define MAX_COUPABLE_NAME_LEN 100

pthread_t thread_serveur_tcp_id;
char gbuffer[256];
char gServerIpAddress[256];
int gServerPort;
char gClientIpAddress[256];
int gClientPort;
char gName[256];
char gNames[4][256];
int gId = -1;
int joueurSel = -1;
int objetSel = -1;
int guiltSel = -1;
int guiltGuess[13];
int tableCartes[4][8];
int b[3];
int goEnabled = 0;
int connectEnabled = 1;
int gameActive = 1;
char statusMessage[256] = "";
int joueurCourantId = -1;

// Tableaux de noms (correspondent aux indices 0-7 pour objets, 0-12 pour persos)
char *nbobjets[] = {"5", "5", "5", "5", "4", "3", "3", "3"}; // Nombre total de chaque symbole dans le jeu
char *nomcartes[] = { // Noms des personnages/cartes
    "Sebastian Moran", "irene Adler", "inspector Lestrade",
    "inspector Gregson", "inspector Baynes", "inspector Bradstreet",
    "inspector Hopkins", "Sherlock Holmes", "John Watson", "Mycroft Holmes",
    "Mrs. Hudson", "Mary Morstan", "James Moriarty"};

volatile int synchro; // Flag pour signaler la réception d'un message

// Thread pour écouter les messages du serveur
void *fn_serveur_tcp(void *arg)
{
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    int n;
    int optval = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("ERREUR ouverture socket écoute client");
        exit(1); // Quitter si on ne peut pas écouter
    }

    // Permettre la réutilisation de l'adresse
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt client listen");
        close(sockfd);
        exit(1);
    }

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY; // Écoute sur toutes les interfaces locales
    serv_addr.sin_port = htons(gClientPort);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("ERREUR bind socket écoute client");
        close(sockfd);
        exit(1);
    }

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    printf("Client: Thread d'écoute démarré sur le port %d\n", gClientPort);

    while (gameActive) // Tant que le jeu est actif
    {
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
         if (!gameActive) { // Vérifier si on doit s'arrêter juste après accept
             if (newsockfd >= 0) close(newsockfd);
             break;
        }
        if (newsockfd < 0)
        {
            if (gameActive) perror("ERREUR accept écoute client");
            continue; // Essayer d'accepter à nouveau si ce n'est pas une fermeture intentionnelle
        }

        bzero(gbuffer, 256);
        n = read(newsockfd, gbuffer, 255); // Lire au max 255 pour laisser place au \0
        if (n < 0)
        {
             if (gameActive) perror("ERREUR lecture socket écoute client");
        } else if (n > 0) {
            gbuffer[n] = '\0'; // Assurer la terminaison null
            // Supprimer le newline potentiel à la fin
            gbuffer[strcspn(gbuffer, "\n")] = 0;
            printf("Client: Message reçu: [%s]\n", gbuffer);
            synchro = 1; // Signaler qu'un message est prêt
            // Attendre que le thread principal traite le message (simple synchro)
            while (synchro && gameActive) {
                 SDL_Delay(10); // Petite pause pour éviter busy-waiting excessif
            }
        }
        close(newsockfd); // Fermer la connexion après lecture du message
    }

    close(sockfd); // Fermer le socket d'écoute quand le jeu est fini
    printf("Client: Thread d'écoute terminé.\n");
    return NULL;
}

// Fonction pour envoyer un message au serveur principal
void sendMessageToServer(char *ipAddress, int portno, char *mess)
{
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char sendbuffer[256]; // Le buffer d'envoi

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Client: ERREUR ouverture socket envoi serveur");
        snprintf(statusMessage, sizeof(statusMessage), "Erreur réseau (socket)");
        return;
    }

    server = gethostbyname(ipAddress);
    if (server == NULL) {
        fprintf(stderr, "Client: ERREUR, hote serveur %s non trouvé\n", ipAddress);
        snprintf(statusMessage, sizeof(statusMessage), "Erreur réseau (hote %s?)", ipAddress);
        close(sockfd);
        return;
    }

    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serv_addr.sin_addr.s_addr,
          server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Client: ERREUR connexion au serveur");
        snprintf(statusMessage, sizeof(statusMessage), "Erreur réseau (connexion serveur)");
        close(sockfd);
        return; // Ne pas essayer d'écrire si la connexion a échoué
    }

    // Utilisation de snprintf pour la sécurité et ajouter le newline pour le read côté serveur
    snprintf(sendbuffer, sizeof(sendbuffer), "%s\n", mess);
    sendbuffer[sizeof(sendbuffer)-1] = '\0'; // Assurer la terminaison quoi qu'il arrive

    printf("Client: Envoi au serveur: [%s]\n", sendbuffer);
    if (write(sockfd, sendbuffer, strlen(sendbuffer)) < 0) {
        perror("Client: ERREUR écriture socket serveur");
        snprintf(statusMessage, sizeof(statusMessage), "Erreur réseau (envoi échoué)");
    }
    close(sockfd); // Fermer la connexion après l'envoi
}

// Fonction pour dessiner du texte
void DrawText(SDL_Renderer *renderer, TTF_Font *font, const char *text, int x, int y, SDL_Color color) {
    if (!text || !font) return;
    SDL_Surface* surfaceMessage = TTF_RenderUTF8_Blended(font, text, color); // Blended pour meilleure qualité
    if (!surfaceMessage) {
        printf("TTF_RenderUTF8_Blended Error: %s\n", TTF_GetError());
        return;
    }
    SDL_Texture* Message = SDL_CreateTextureFromSurface(renderer, surfaceMessage);
    if (!Message) {
        printf("CreateTextureFromSurface Error: %s\n", SDL_GetError());
        SDL_FreeSurface(surfaceMessage);
        return;
    }

    SDL_Rect Message_rect;
    Message_rect.x = x;
    Message_rect.y = y;
    Message_rect.w = surfaceMessage->w;
    Message_rect.h = surfaceMessage->h;

    SDL_RenderCopy(renderer, Message, NULL, &Message_rect);

    SDL_DestroyTexture(Message);
    SDL_FreeSurface(surfaceMessage);
}


int main(int argc, char **argv)
{
    int ret;
    int i, j;

    int quit = 0;
    SDL_Event event;
    int mx, my;
    char sendBuffer[256]; // Pour les messages sortants

    if (argc < 6)
    {
        printf("Usage: %s <IP Serveur> <Port Serveur> <IP Client> <Port Client> <Nom Joueur>\n", argv[0]);
        exit(1);
    }

    // Copie des arguments (plus sûr avec strncpy)
    strncpy(gServerIpAddress, argv[1], sizeof(gServerIpAddress) - 1);
    gServerIpAddress[sizeof(gServerIpAddress) - 1] = '\0';
    gServerPort = atoi(argv[2]);
    strncpy(gClientIpAddress, argv[3], sizeof(gClientIpAddress) - 1);
     gClientIpAddress[sizeof(gClientIpAddress) - 1] = '\0';
    gClientPort = atoi(argv[4]);
    strncpy(gName, argv[5], sizeof(gName) - 1);
    gName[sizeof(gName) - 1] = '\0';

    // Initialisation SDL et TTF
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() == -1) {
         printf("TTF_Init Error: %s\n", TTF_GetError());
         SDL_Quit();
         return 1;
    }
     if (IMG_Init(IMG_INIT_PNG) != IMG_INIT_PNG) { // Initialiser SDL_image pour PNG
        printf("IMG_Init Error: %s\n", IMG_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("Sherlock 13 Client",
                                          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1024, 768, 0);
    if (!window) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
         IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Chargement des images (avec vérification basique)
    SDL_Surface *deck_surface[13], *objet_surface[8], *gobutton_surface, *connectbutton_surface;
    char filename[60]; // Un peu plus de marge pour le nom de fichier
    for (i = 0; i < 13; i++) {
        snprintf(filename, sizeof(filename), "assets/SH13_%d.png", i);
        deck_surface[i] = IMG_Load(filename);
        if (!deck_surface[i]) printf("Warning: Could not load %s: %s\n", filename, IMG_GetError());
    }
    char *obj_files[] = {"pipe", "ampoule", "poing", "couronne", "carnet", "collier", "oeil", "crane"};
    for (i = 0; i < 8; i++) {
        snprintf(filename, sizeof(filename), "assets/SH13_%s_120x120.png", obj_files[i]);
         objet_surface[i] = IMG_Load(filename);
         if (!objet_surface[i]) printf("Warning: Could not load %s: %s\n", filename, IMG_GetError());
    }
    gobutton_surface = IMG_Load("assets/gobutton.png");
    if (!gobutton_surface) printf("Warning: Could not load assets/gobutton.png: %s\n", IMG_GetError());
    connectbutton_surface = IMG_Load("assets/connectbutton.png");
    if (!connectbutton_surface) printf("Warning: Could not load assets/connectbutton.png: %s\n", IMG_GetError());

    // Initialisation des données du jeu client
    strcpy(gNames[0], "-");
    strcpy(gNames[1], "-");
    strcpy(gNames[2], "-");
    strcpy(gNames[3], "-");

    joueurSel = -1;
    objetSel = -1;
    guiltSel = -1;
    joueurCourantId = -1;

    b[0] = -1; // Carte 1 non reçue
    b[1] = -1; // Carte 2 non reçue
    b[2] = -1; // Carte 3 non reçue

    for (i = 0; i < 13; i++)
        guiltGuess[i] = 0; // Initialement, personne n'est marqué comme éliminé

    for (i = 0; i < 4; i++)
        for (j = 0; j < 8; j++)
            tableCartes[i][j] = -1; // -1 indique que la valeur n'a pas encore été reçue/déterminée

    goEnabled = 0;
    connectEnabled = 1; // Peut se connecter au début
    gameActive = 1;     // Le jeu est actif
    snprintf(statusMessage, sizeof(statusMessage), "Connectez-vous au serveur.");


    // Création des textures (après initialisation renderer)
    SDL_Texture *texture_deck[13] = {NULL}, *texture_gobutton = NULL, *texture_connectbutton = NULL, *texture_objet[8] = {NULL};
    for (i = 0; i < 13; i++) {
        if (deck_surface[i]) {
             texture_deck[i] = SDL_CreateTextureFromSurface(renderer, deck_surface[i]);
             SDL_FreeSurface(deck_surface[i]); // Libérer la surface après création de la texture
             deck_surface[i] = NULL;
        } else texture_deck[i] = NULL;
    }
    for (i = 0; i < 8; i++) {
         if (objet_surface[i]) {
             texture_objet[i] = SDL_CreateTextureFromSurface(renderer, objet_surface[i]);
             SDL_FreeSurface(objet_surface[i]);
             objet_surface[i] = NULL;
         } else texture_objet[i] = NULL;
    }
    if (gobutton_surface) {
        texture_gobutton = SDL_CreateTextureFromSurface(renderer, gobutton_surface);
        SDL_FreeSurface(gobutton_surface);
        gobutton_surface = NULL;
    }
    if (connectbutton_surface) {
         texture_connectbutton = SDL_CreateTextureFromSurface(renderer, connectbutton_surface);
         SDL_FreeSurface(connectbutton_surface);
         connectbutton_surface = NULL;
    }

    // Charger la police
    TTF_Font *Sans = TTF_OpenFont("assets/sans.ttf", 15);
    TTF_Font *SansBig = TTF_OpenFont("assets/sans.ttf", 20); // Pour les messages importants
    if (!Sans || !SansBig) {
        printf("TTF_OpenFont Error: %s\n", TTF_GetError());
    }

    // Creation du thread serveur tcp (écoute des messages du serveur principal)
    printf("Création du thread serveur tcp !\n");
    synchro = 0; // Réinitialiser le flag avant de créer le thread
    ret = pthread_create(&thread_serveur_tcp_id, NULL, fn_serveur_tcp, NULL);
    if (ret != 0) {
        fprintf(stderr, "Erreur pthread_create : %d\n", ret);
         // Nettoyer SDL et quitter
        if (Sans) TTF_CloseFont(Sans);
        if (SansBig) TTF_CloseFont(SansBig);
        // Supprimer les textures
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        IMG_Quit();
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // Boucle principale du jeu client
    while (!quit && gameActive)
    {
        // Gestion des événements SDL
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_QUIT:
                quit = 1;
                gameActive = 0; // Signaler au thread d'écoute de s'arrêter
                break;
            case SDL_MOUSEBUTTONDOWN:
                SDL_GetMouseState(&mx, &my);

                // Bouton Connect
                if ((mx < 200) && (my < 50) && (connectEnabled == 1))
                {
                    printf("Clic sur Connect.\n");
                    // Formatage prudent pour éviter warning/truncation avec %.*s
                    snprintf(sendBuffer, sizeof(sendBuffer) - 1, "C %.*s %d %.*s", MAX_IP_LEN, gClientIpAddress, gClientPort, MAX_NAME_LEN, gName);
                    sendBuffer[sizeof(sendBuffer)-1] = '\0'; // Ensure null termination
                    sendMessageToServer(gServerIpAddress, gServerPort, sendBuffer);
                    connectEnabled = 0; // Désactiver après le clic
                    snprintf(statusMessage, sizeof(statusMessage), "Connexion en cours...");
                }
                // Zone de sélection des joueurs (pour enquête S)
                else if ((mx >= 0) && (mx < 200) && (my >= 90) && (my < 90 + 4*60)) // 4 joueurs, 60px hauteur chacun
                {
                    int clickedPlayer = (my - 90) / 60;
                    if (clickedPlayer >= 0 && clickedPlayer < 4) { // Remplacé nbClients par 4
                        joueurSel = clickedPlayer;
                        guiltSel = -1; // Désélectionner l'accusation si on sélectionne un joueur
                        printf("Joueur %d sélectionné pour enquête S.\n", joueurSel);
                    }
                }
                // Zone de sélection des objets (pour enquête O ou S)
                else if ((mx >= 200) && (mx < 200 + 8*60) && (my >= 0) && (my < 90)) // 8 objets, 60px largeur
                {
                    objetSel = (mx - 200) / 60;
                    guiltSel = -1; // Désélectionner l'accusation si on sélectionne un objet
                    printf("Objet %d sélectionné pour enquête O/S.\n", objetSel);
                }
                // Zone de sélection des suspects (pour accusation G)
                else if ((mx >= 100) && (mx < 250) && (my >= 350) && (my < 350 + 13*30)) // 13 suspects, 30px hauteur
                {
                    guiltSel = (my - 350) / 30;
                    joueurSel = -1; // Désélectionner joueur/objet si on sélectionne un suspect
                    objetSel = -1;
                    printf("Suspect %d (%s) sélectionné pour accusation G.\n", guiltSel, nomcartes[guiltSel]);
                }
                // Zone pour marquer/démarquer les suspects sur la feuille d'enquête
                else if ((mx >= 250) && (mx < 300) && (my >= 350) && (my < 350 + 13 * 30))
                {
                    int ind = (my - 350) / 30;
                    if (ind >= 0 && ind < 13) {
                         guiltGuess[ind] = 1 - guiltGuess[ind]; // Bascule l'état (0 ou 1) -> 1 = suspect éliminé
                         printf("Marquage suspect %d changé à %d\n", ind, guiltGuess[ind]);
                    }
                }
                // Bouton GO (si activé)
                else if ((mx >= 500) && (mx < 700) && (my >= 350) && (my < 450) && (goEnabled == 1))
                {
                    printf("Clic sur GO! joueurSel=%d objetSel=%d guiltSel=%d\n", joueurSel, objetSel, guiltSel);
                    int actionSent = 0; // Flag pour savoir si une action valide a été envoyée

                    if (guiltSel != -1) // Action = Accusation (G)
                    {
                        snprintf(sendBuffer, sizeof(sendBuffer),"G %d %d", gId, guiltSel);
                        sendMessageToServer(gServerIpAddress, gServerPort, sendBuffer);
                        snprintf(statusMessage, sizeof(statusMessage), "Accusation envoyée...");
                        actionSent = 1;
                    }
                    else if ((objetSel != -1) && (joueurSel == -1)) // Action = Enquête 1 (O)
                    {
                        snprintf(sendBuffer, sizeof(sendBuffer),"O %d %d", gId, objetSel);
                        sendMessageToServer(gServerIpAddress, gServerPort, sendBuffer);
                         snprintf(statusMessage, sizeof(statusMessage), "Enquête 1 (Objet) envoyée...");
                         actionSent = 1;
                    }
                    else if ((objetSel != -1) && (joueurSel != -1)) // Action = Enquête 2 (S)
                    {
                         snprintf(sendBuffer, sizeof(sendBuffer),"S %d %d %d", gId, joueurSel, objetSel);
                         sendMessageToServer(gServerIpAddress, gServerPort, sendBuffer);
                         snprintf(statusMessage, sizeof(statusMessage), "Enquête 2 (Spécifique) envoyée...");
                         actionSent = 1;
                    }
                    else {
                        // Action invalide (rien de sélectionné correctement)
                         snprintf(statusMessage, sizeof(statusMessage), "Action invalide: sélectionnez un suspect OU un objet OU un objet+joueur.");
                         printf("Action GO invalide - rien de sélectionné correctement.\n");
                         // Ne pas envoyer de message, ne pas changer goEnabled, ne pas réinitialiser les sélections
                    }

                    // Si une action valide a été envoyée, réinitialiser les sélections locales
                    // mais NE PAS changer goEnabled ici (attendre le message 'M' du serveur)
                    if (actionSent) {
                        joueurSel = -1;
                        objetSel = -1;
                        guiltSel = -1;
                    }
                }
                // Clic ailleurs : désélectionner tout
                else
                {
                    joueurSel = -1;
                    objetSel = -1;
                    guiltSel = -1;
                }
                break;
            }
        }

        // Traitement des messages reçus du serveur (via le thread d'écoute)
        if (synchro == 1)
        {
            //printf("Traitement synchro=1, message: [%s]\n", gbuffer);
            switch (gbuffer[0])
            {
            // Message 'I' : le joueur reçoit son Id
            case 'I': // Format: I <id>
                if (sscanf(gbuffer, "I %d", &gId) == 1) {
                     printf("Mon ID est: %d\n", gId);
                     snprintf(statusMessage, sizeof(statusMessage), "Connecté avec ID %d. Attente d'autres joueurs...", gId);
                } else {
                     printf("Erreur parsing message 'I': %s\n", gbuffer);
                }
                break;

            // Message 'L' : le joueur reçoit la liste des joueurs
            case 'L': // Format: L <nom0> <nom1> <nom2> <nom3>
                // Utilisation de sscanf pour parser les noms
                if (sscanf(gbuffer, "L %s %s %s %s", gNames[0], gNames[1], gNames[2], gNames[3]) == 4) {
                     printf("Liste des joueurs reçue: %s, %s, %s, %s\n", gNames[0], gNames[1], gNames[2], gNames[3]);
                } else {
                     printf("Erreur parsing message 'L': %s\n", gbuffer);
                }
                break;

            // Message 'D' : le joueur reçoit ses trois cartes
            case 'D': // Format: D <carte1_idx> <carte2_idx> <carte3_idx>
                if (sscanf(gbuffer, "D %d %d %d", &b[0], &b[1], &b[2]) == 3) {
                     printf("Mes cartes reçues: %d (%s), %d (%s), %d (%s)\n",
                           b[0], nomcartes[b[0]],
                           b[1], nomcartes[b[1]],
                           b[2], nomcartes[b[2]]);
                    // Marquer mes propres cartes comme non coupables sur la feuille d'enquête
                     if (b[0]>=0 && b[0]<13) guiltGuess[b[0]] = 1; // 1 signifie 'éliminé/non coupable'
                     if (b[1]>=0 && b[1]<13) guiltGuess[b[1]] = 1;
                     if (b[2]>=0 && b[2]<13) guiltGuess[b[2]] = 1;
                     snprintf(statusMessage, sizeof(statusMessage), "Cartes reçues. Attente début du jeu...");
                } else {
                     printf("Erreur parsing message 'D': %s\n", gbuffer);
                }
                break;

            // Message 'M' : le joueur reçoit le n° du joueur courant
            case 'M': // Format: M <joueurCourantId>
                if (sscanf(gbuffer, "M %d", &joueurCourantId) == 1) {
                     printf("Message 'M': C'est au tour du joueur %d (%s).\n", joueurCourantId, (joueurCourantId >= 0 && joueurCourantId < 4) ? gNames[joueurCourantId] : "Inconnu");
                     if (joueurCourantId == gId) {
                         goEnabled = 1; // C'est mon tour ! Activer le bouton GO.
                         snprintf(statusMessage, sizeof(statusMessage) -1, "C'est votre tour ! Choisissez une action.");
                     } else {
                         goEnabled = 0; // Ce n'est pas mon tour. Désactiver GO.
                         // Utilisation de %.*s pour éviter warning
                         snprintf(statusMessage, sizeof(statusMessage) -1, "C'est au tour de %.*s.", MAX_NAME_LEN, (joueurCourantId >= 0 && joueurCourantId < 4) ? gNames[joueurCourantId] : "Inconnu");
                     }
                     statusMessage[sizeof(statusMessage)-1] = '\0';
                } else {
                    printf("Erreur parsing message 'M': %s\n", gbuffer);
                }
                break;

            // Message 'V' : le joueur reçoit une valeur de tableCartes (résultat d'enquête)
            case 'V': // Format: V <joueurId> <objetId> <valeur>
                int v_playerId, v_objetId, v_valeur;
                 if (sscanf(gbuffer, "V %d %d %d", &v_playerId, &v_objetId, &v_valeur) == 3) {
                    if (v_playerId >= 0 && v_playerId < 4 && v_objetId >= 0 && v_objetId < 8) {
                         printf("Message 'V': Joueur %d, Objet %d, Valeur = %d\n", v_playerId, v_objetId, v_valeur);
                         tableCartes[v_playerId][v_objetId] = v_valeur;
                         // Mettre à jour le message status (seulement si ce n'est pas mon tour)
                         if (goEnabled == 0) {
                             if (v_valeur == 100) { // Convention pour "a l'objet"
                                 // Utilisation de %.*s
                                 snprintf(statusMessage, sizeof(statusMessage)-1, "Info: %.*s possède l'objet %d.", MAX_NAME_LEN, gNames[v_playerId], v_objetId);
                             } else { // Résultat enquête S
                                 // Utilisation de %.*s
                                  snprintf(statusMessage, sizeof(statusMessage)-1, "Info: %.*s possède %d fois l'objet %d.", MAX_NAME_LEN, gNames[v_playerId], v_valeur, v_objetId);
                             }
                              statusMessage[sizeof(statusMessage)-1] = '\0';
                         }

                    } else {
                        printf("Message 'V' invalide (indices hors limites): %s\n", gbuffer);
                    }
                 } else {
                     printf("Erreur parsing message 'V': %s\n", gbuffer);
                 }
                break;

            // Message 'W' : Quelqu'un a gagné
             case 'W': // Format: W <winnerId> <coupableName> <coupableId>
                int winnerId = -1, coupableId = -1;
                char coupableName[MAX_COUPABLE_NAME_LEN + 1] = "Inconnu"; // Utiliser la constante

                // Parsing plus robuste pour le nom avec espaces
                char *space1_w = strchr(gbuffer, ' ');
                if (space1_w) {
                    char *space2_w = strchr(space1_w + 1, ' ');
                    if (space2_w) {
                        char *lastSpace_w = strrchr(gbuffer, ' ');
                        if (lastSpace_w && sscanf(lastSpace_w, " %d", &coupableId) == 1) {
                             size_t nameLen_w = lastSpace_w - (space2_w + 1);
                             if (nameLen_w < sizeof(coupableName)) {
                                 strncpy(coupableName, space2_w + 1, nameLen_w);
                                 coupableName[nameLen_w] = '\0';
                             } else {
                                 strncpy(coupableName, space2_w + 1, sizeof(coupableName) - 1);
                                 coupableName[sizeof(coupableName) - 1] = '\0';
                             }
                             sscanf(space1_w + 1, "%d", &winnerId);
                        }
                    }
                }

                 printf("Message 'W': Joueur %d (%s) a gagné! Le coupable était %s (%d)\n", winnerId, (winnerId >= 0 && winnerId < 4) ? gNames[winnerId] : "N/A", coupableName, coupableId);
                 // Utilisation de %.*s pour les noms
                 if (winnerId == gId) {
                    snprintf(statusMessage, sizeof(statusMessage)-1, "Félicitations, vous avez gagné ! Le coupable était %.*s.", MAX_COUPABLE_NAME_LEN, coupableName);
                 } else if (winnerId >= 0 && winnerId < 4) {
                    snprintf(statusMessage, sizeof(statusMessage)-1, "%.*s a gagné ! Le coupable était %.*s.", MAX_NAME_LEN, gNames[winnerId], MAX_COUPABLE_NAME_LEN, coupableName);
                 } else {
                     snprintf(statusMessage, sizeof(statusMessage)-1, "Fin de partie ! Le coupable était %.*s.", MAX_COUPABLE_NAME_LEN, coupableName);
                 }
                 statusMessage[sizeof(statusMessage)-1] = '\0';
                 gameActive = 0; // Fin de partie
                 goEnabled = 0;
                 connectEnabled = 0;
                 break;

            // Message 'F' : Le joueur a raté son accusation
            case 'F': // Format: F <coupableId>
                 int realCoupableId;
                 if (sscanf(gbuffer, "F %d", &realCoupableId) == 1) {
                     printf("Message 'F': Votre accusation était incorrecte.\n");
                     snprintf(statusMessage, sizeof(statusMessage), "Accusation ratée. Vous ne jouez plus mais répondez aux questions.");
                     goEnabled = 0; // On ne peut plus jouer d'action
                 } else {
                    printf("Erreur parsing message 'F': %s\n", gbuffer);
                 }
                 break;

            default:
                printf("Message serveur inconnu reçu: %s\n", gbuffer);
                break;
            }
            synchro = 0; // Marquer le message comme traité
        } // Fin if (synchro == 1)

        // --- Début du Rendu Graphique ---
        // Couleur de fond
        SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255); // Gris clair
        SDL_RenderClear(renderer);

        // Afficher les sélections en surbrillance
        if (joueurSel != -1) {
            SDL_SetRenderDrawColor(renderer, 180, 180, 255, 255); // Bleu clair
            SDL_Rect rect1 = {0, 90 + joueurSel * 60, 200, 60};
            SDL_RenderFillRect(renderer, &rect1);
        }
        if (objetSel != -1) {
            SDL_SetRenderDrawColor(renderer, 180, 255, 180, 255); // Vert clair
            SDL_Rect rect1 = {200 + objetSel * 60, 0, 60, 90};
            SDL_RenderFillRect(renderer, &rect1);
        }
        if (guiltSel != -1) {
            SDL_SetRenderDrawColor(renderer, 255, 180, 180, 255); // Rouge clair
            SDL_Rect rect1 = {100, 350 + guiltSel * 30, 150, 30};
            SDL_RenderFillRect(renderer, &rect1);
        }

        // Afficher les icones des objets en haut
        for (i = 0; i < 8; i++) {
             if (texture_objet[i]) {
                 SDL_Rect dstRect = {210 + i * 60, 10, 40, 40};
                 SDL_RenderCopy(renderer, texture_objet[i], NULL, &dstRect);
             }
            // Afficher le nombre total de chaque objet
            if (Sans) DrawText(renderer, Sans, nbobjets[i], 230 + i * 60, 50, (SDL_Color){0, 0, 0, 255});
        }

        // Afficher les noms des joueurs sur le côté gauche
        SDL_Color col_black = {0, 0, 0, 255};
        SDL_Color col_red = {200, 0, 0, 255};
        for (i = 0; i < 4; i++) {
            // Afficher le nom seulement si ce n'est pas "-"
            if (strlen(gNames[i]) > 0 && strcmp(gNames[i],"-") != 0) {
                DrawText(renderer, Sans, gNames[i], 10, 110 + i * 60, col_black);
                 // Indiquer le joueur courant avec une flèche rouge
                 if (i == joueurCourantId) {
                    DrawText(renderer, Sans, "<- TOUR", 150, 110 + i * 60, col_red);
                 }
            }
        }

         // Afficher la grille des informations (tableCartes)
        for (i = 0; i < 4; i++) { // Lignes (Joueurs)
             for (j = 0; j < 8; j++) { // Colonnes (Objets)
                 if (tableCartes[i][j] != -1) { // Si on a une info
                     char mess[10];
                     if (tableCartes[i][j] == 100) { // Convention pour "a l'objet"
                         snprintf(mess, sizeof(mess), "*");
                     } else {
                         snprintf(mess, sizeof(mess), "%d", tableCartes[i][j]);
                     }
                     DrawText(renderer, Sans, mess, 230 + j * 60, 110 + i * 60, col_black);
                 }
             }
        }

        // Afficher la liste des suspects et leurs symboles
        for (i = 0; i < 13; i++) {
            // Nom du suspect
            DrawText(renderer, Sans, nomcartes[i], 105, 350 + i * 30, col_black);
            // Symboles associés
            SDL_Rect dstRect = {0, 350 + i * 30, 30, 30}; // Position de base
            int symbolOffset = 0; // Décalage horizontal

             switch (i) { // Ajout de vérifs sur les textures avant RenderCopy
                 case 0: // Moran: Crane, Poing
                     dstRect.x = symbolOffset; if(texture_objet[7]) SDL_RenderCopy(renderer, texture_objet[7], NULL, &dstRect); symbolOffset += 30;
                     dstRect.x = symbolOffset; if(texture_objet[2]) SDL_RenderCopy(renderer, texture_objet[2], NULL, &dstRect); symbolOffset += 30;
                     break;
                 case 1: // Adler: Crane, Ampoule, Collier
                     dstRect.x = symbolOffset; if(texture_objet[7]) SDL_RenderCopy(renderer, texture_objet[7], NULL, &dstRect); symbolOffset += 30;
                     dstRect.x = symbolOffset; if(texture_objet[1]) SDL_RenderCopy(renderer, texture_objet[1], NULL, &dstRect); symbolOffset += 30;
                     dstRect.x = symbolOffset; if(texture_objet[5]) SDL_RenderCopy(renderer, texture_objet[5], NULL, &dstRect); symbolOffset += 30;
                     break;
                 case 2: // Lestrade: Couronne, Oeil, Carnet
                     dstRect.x = symbolOffset; if(texture_objet[3]) SDL_RenderCopy(renderer, texture_objet[3], NULL, &dstRect); symbolOffset += 30;
                     dstRect.x = symbolOffset; if(texture_objet[6]) SDL_RenderCopy(renderer, texture_objet[6], NULL, &dstRect); symbolOffset += 30;
                     dstRect.x = symbolOffset; if(texture_objet[4]) SDL_RenderCopy(renderer, texture_objet[4], NULL, &dstRect); symbolOffset += 30;
                     break;
                 case 3: // Gregson: Couronne, Poing, Carnet
                      dstRect.x = symbolOffset; if(texture_objet[3]) SDL_RenderCopy(renderer, texture_objet[3], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[2]) SDL_RenderCopy(renderer, texture_objet[2], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[4]) SDL_RenderCopy(renderer, texture_objet[4], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 4: // Baynes: Couronne, Ampoule
                      dstRect.x = symbolOffset; if(texture_objet[3]) SDL_RenderCopy(renderer, texture_objet[3], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[1]) SDL_RenderCopy(renderer, texture_objet[1], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 5: // Bradstreet: Couronne, Poing
                      dstRect.x = symbolOffset; if(texture_objet[3]) SDL_RenderCopy(renderer, texture_objet[3], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[2]) SDL_RenderCopy(renderer, texture_objet[2], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 6: // Hopkins: Couronne, Pipe, Oeil
                      dstRect.x = symbolOffset; if(texture_objet[3]) SDL_RenderCopy(renderer, texture_objet[3], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[0]) SDL_RenderCopy(renderer, texture_objet[0], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[6]) SDL_RenderCopy(renderer, texture_objet[6], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 7: // Holmes: Pipe, Ampoule, Poing
                      dstRect.x = symbolOffset; if(texture_objet[0]) SDL_RenderCopy(renderer, texture_objet[0], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[1]) SDL_RenderCopy(renderer, texture_objet[1], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[2]) SDL_RenderCopy(renderer, texture_objet[2], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 8: // Watson: Pipe, Oeil, Poing
                      dstRect.x = symbolOffset; if(texture_objet[0]) SDL_RenderCopy(renderer, texture_objet[0], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[6]) SDL_RenderCopy(renderer, texture_objet[6], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[2]) SDL_RenderCopy(renderer, texture_objet[2], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 9: // Mycroft: Pipe, Ampoule, Carnet
                      dstRect.x = symbolOffset; if(texture_objet[0]) SDL_RenderCopy(renderer, texture_objet[0], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[1]) SDL_RenderCopy(renderer, texture_objet[1], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[4]) SDL_RenderCopy(renderer, texture_objet[4], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 10: // Hudson: Pipe, Collier
                      dstRect.x = symbolOffset; if(texture_objet[0]) SDL_RenderCopy(renderer, texture_objet[0], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[5]) SDL_RenderCopy(renderer, texture_objet[5], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 11: // Morstan: Carnet, Collier
                      dstRect.x = symbolOffset; if(texture_objet[4]) SDL_RenderCopy(renderer, texture_objet[4], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[5]) SDL_RenderCopy(renderer, texture_objet[5], NULL, &dstRect); symbolOffset += 30;
                      break;
                 case 12: // Moriarty: Crane, Ampoule
                      dstRect.x = symbolOffset; if(texture_objet[7]) SDL_RenderCopy(renderer, texture_objet[7], NULL, &dstRect); symbolOffset += 30;
                      dstRect.x = symbolOffset; if(texture_objet[1]) SDL_RenderCopy(renderer, texture_objet[1], NULL, &dstRect); symbolOffset += 30;
                      break;
             }

            // Afficher la croix si le joueur a marqué le suspect comme éliminé
            if (guiltGuess[i])
            {
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // Rouge
                SDL_RenderDrawLine(renderer, 250, 350 + i * 30, 300, 380 + i * 30);
                SDL_RenderDrawLine(renderer, 250, 380 + i * 30, 300, 350 + i * 30);
            }
        }

        // Dessiner les lignes de la grille
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // Noir
        // Lignes horizontales grille info (joueurs)
        for (i = 0; i < 5; i++) SDL_RenderDrawLine(renderer, 0, 90 + i * 60, 680, 90 + i * 60);
        // Lignes verticales grille info (objets)
        SDL_RenderDrawLine(renderer, 200, 0, 200, 90 + 4 * 60); // Limite noms joueurs
        for (i = 0; i < 9; i++) SDL_RenderDrawLine(renderer, 200 + i * 60, 0, 200 + i * 60, 90 + 4 * 60);

        // Lignes horizontales liste suspects
        for (i = 0; i < 14; i++) SDL_RenderDrawLine(renderer, 0, 350 + i * 30, 300, 350 + i * 30);
        // Lignes verticales liste suspects
         SDL_RenderDrawLine(renderer, 0, 350, 0, 350 + 13 * 30); // Bord gauche
        SDL_RenderDrawLine(renderer, 100, 350, 100, 350 + 13 * 30); // Limite symboles/noms
        SDL_RenderDrawLine(renderer, 250, 350, 250, 350 + 13 * 30); // Limite noms/marquage
        SDL_RenderDrawLine(renderer, 300, 350, 300, 350 + 13 * 30); // Bord droit

        // Afficher les cartes du joueur sur la droite
        int card_y_offset = 0;
        for (i = 0; i < 3; i++) {
             if (b[i] != -1 && b[i] < 13 && texture_deck[b[i]]) {
                 SDL_Rect dstrect_card = {750, card_y_offset, 1000 / 4, 660 / 4}; // Taille réduite
                 SDL_RenderCopy(renderer, texture_deck[b[i]], NULL, &dstrect_card);
                 card_y_offset += 660 / 4 + 10; // Espacement entre cartes
             }
        }

        // Afficher le bouton GO si c'est le tour du joueur
        if (goEnabled == 1 && texture_gobutton) {
            SDL_Rect dstrect_go = {500, 350, 200, 150};
            SDL_RenderCopy(renderer, texture_gobutton, NULL, &dstrect_go);
        }

        // Afficher le bouton Connect si activé
        if (connectEnabled == 1 && texture_connectbutton) {
            SDL_Rect dstrect_connect = {0, 0, 200, 50};
            SDL_RenderCopy(renderer, texture_connectbutton, NULL, &dstrect_connect);
        }

        // Afficher le message de statut en bas (avec la police plus grande)
         if (SansBig) DrawText(renderer, SansBig, statusMessage, 10, 768 - 30, col_black);

        // Mettre à jour l'écran
        SDL_RenderPresent(renderer);

        SDL_Delay(10); // Petite pause

    }

    // Nettoyage avant de quitter
    printf("Nettoyage et fermeture...\n");
    gameActive = 0; // Assurer que le thread s'arrête

     // Tenter de réveiller le thread d'écoute s'il est bloqué sur accept en se connectant à son socket d'écoute localement.
    int wake_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (wake_sock >= 0) {
        struct sockaddr_in client_addr;
        bzero((char *)&client_addr, sizeof(client_addr));
        client_addr.sin_family = AF_INET;
        // Utiliser inet_pton pour convertir "127.0.0.1" en structure d'adresse
        if (inet_pton(AF_INET, "127.0.0.1", &client_addr.sin_addr) <= 0) {
             perror("inet_pton failed for 127.0.0.1");
        }
        client_addr.sin_port = htons(gClientPort);
        // Se connecter brièvement (ignorer erreur si le thread n'est pas sur accept)
        connect(wake_sock, (struct sockaddr *)&client_addr, sizeof(client_addr));
        close(wake_sock);
    }

     if (thread_serveur_tcp_id) {
         printf("Attente de la fin du thread d'écoute...\n");
         pthread_join(thread_serveur_tcp_id, NULL);
         printf("Thread d'écoute terminé.\n");
     }

    if (Sans) TTF_CloseFont(Sans);
    if (SansBig) TTF_CloseFont(SansBig);

    for (i = 0; i < 13; i++) {
         if (texture_deck[i]) SDL_DestroyTexture(texture_deck[i]);
         // Surfaces déjà libérées lors de la création des textures
    }
     for (i = 0; i < 8; i++) {
         if (texture_objet[i]) SDL_DestroyTexture(texture_objet[i]);
         // Surfaces déjà libérées
     }
    if (texture_gobutton) SDL_DestroyTexture(texture_gobutton);
    if (texture_connectbutton) SDL_DestroyTexture(texture_connectbutton);
    // Surfaces gobutton/connectbutton déjà libérées

    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();

    return 0;
}