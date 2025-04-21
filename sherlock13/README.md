# Projet **Sherlock 13** – Jeu de déduction multi‑client

> _Module **OS USER** · Filière EI4 · Polytech Sorbonne · année 2024 / 2025_  
> Auteur : **Hakan PEKUR**  
> Encadrants : François Pecheux · Thibault Hilaire

---

## 1. Présentation

Cette implémentation réseau du jeu de société **Sherlock 13** permet à **4 joueurs** de s’affronter afin de découvrir, à l’aide d’indices, quel personnage parmi **13 suspects** est le coupable.  
Le travail partait d’une _base de code incomplète_ fournie dans le cadre du module, que j’ai complétée et sécurisée :

* Ajout d’un **mélange aléatoire** du deck (`melangerDeck`),
* Calcul exhaustif de la table des symboles (`createTable`),
* Gestion **mono‑thread** du serveur (`server.c`) avec machine à états (`fsmServer`),
* Interface graphique SDL2 complète côté client (`sh13.c`) propulsée par **deux threads** :  
  1) _main_ : rendu & saisie utilisateur, 
  2) _listener_ : réception réseau.
* Nettoyage de ressources, robustesse des `snprintf`, borne des tableaux, etc.

L’objectif pédagogique principal était la **programmation système** : _sockets_ TCP, synchronisation de threads, gestion d’état distribué.

---

## 2. Architecture

```
┌──────────────┐        connect / write()         ┌──────────────┐
│   Client 0   │  ───────────────────────────────►│              │
│  (sh13.c)    │◄─────────────────────────────────┤              │
└──────────────┘        accept / read()           │   server.c   │
        ⋮                                         │ mono‑thread  │
┌──────────────┐                                  │  state‑full  │
│   Client 3   │                                  └──────────────┘
└──────────────┘                                 
```

| Élément | Langage | Spécificités |
|---------|---------|--------------|
| **server.c** | C99 | *Mono‑thread* : le processus accepte une connexion par message entrant, traite la requête puis ferme le socket. Les réponses sont envoyées via de **nouvelles connexions TCP sortantes** vers les clients. Toute la logique de jeu est contenue dans le thread principal. |
| **sh13.c** | C99 + SDL2 | *Bi‑thread* : le thread graphique gère SDL / IMG / TTF, tandis qu’un thread d’écoute ouvre un **socket passif** (`bind`, `listen`, `accept`) pour recevoir les paquets pushés par le serveur. Un simple drapeau `volatile int synchro` synchronise les deux threads. |

### 2.1 Serveur

* **Phase 0 – Lobby** : attente des 4 commandes `C <ip> <port> <name>` → attribution d’un ID (`I`), broadcast de la liste (`L`).  
* **Phase 1 – Jeu** : gestion des commandes `G`, `O`, `S` décrites §3.  
* **Phase 2 – Fin** : broadcast `W` (ou `F` pour échec individuel), puis arrêt.

Le code repose sur plusieurs éléments :

* `deck[13]` : permutation aléatoire des cartes.  
* `tableCartes[4][8]` : nombre de symboles détenus par chaque joueur, calculé une seule fois.  
* `tcpClients[4]` : ip, port, nom, état _actif_ / _inactif_.

### 2.2 Client SDL

* **Rendu** : plateau, liste des suspects et de leurs symboles, cartouches des 3 cartes détenues, bouton **Connect** puis **GO**.  
* **Saisie** : clics sur les zones interactives (joueur, objet, suspect) → envoi des commandes réseau.  
* **Thread _listener_** : écrit le message reçu dans `gbuffer` et lève `synchro ;` le thread principal consomme alors le message (`switch` sur le premier caractère).

---

## 3. Protocole réseau

| Message | Sens | Format | Sémantique |
|---------|------|--------|------------|
| `C` | Client → serveur | `C ip port name` | Demande d’inscription |
| `I` | Serveur → client | `I id` | ID attribué |
| `L` | S → C* | `L n0 n1 n2 n3` | Noms des joueurs |
| `D` | S → Ci | `D c0 c1 c2` | Cartes du joueur _i_ |
| `V` | S → Ci | `V p o v` | Résultat d’enquête |
| `M` | S → C* | `M id` | ID du joueur dont c’est le tour |
| `G` | C → S | `G id suspect` | Accusation |
| `O` | C → S | `O id objet` | Enquête « qui possède » |
| `S` | C → S | `S id cible objet` | Enquête spécifique |
| `F` | S → Ci | `F coupable` | Accusation ratée |
| `W` | S → C* | `W winner coupableName coupableId` | Victoire / fin de partie |

---

## 4. Règles (rappel)

1. **Accuser (`G`)** : juste → victoire immédiate, faux → joueur inactif.  
2. **Enquête Objet (`O`)** : “Qui possède l’objet _o_ ?” → `V` pour chaque possesseur.  
3. **Enquête Spécifique (`S`)** : “Combien de _o_ possède le joueur _j_ ?” → `V` unique.  

Le tour passe ensuite au **prochain joueur actif** ; si un seul actif reste, il gagne.

---

## 5. Organisation du dépôt

```
.
├── assets/              # images PNG + police TTF
├── server.c             # logique serveur
├── sh13.c               # client graphique SDL2
├── Makefile             # cibles : all / clean
└── README.md            # ce document
```

---

## 6. Compilation

Assurez‑vous d’avoir :

```bash
sudo apt install build-essential libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev
```

Puis :

```bash
make          # construit les binaires server et sh13
make clean    # nettoyage
```

Le _Makefile_ utilise : `gcc -Wall -g -pthread -lSDL2 -lSDL2_image -lSDL2_ttf -lm`.

---

## 7. Exécution

### 7.1 Lancer le serveur

```bash
./server 12345        # 12345 = port d’écoute
```

### 7.2 Lancer 4 clients (exemple local)

```bash
./sh13 127.0.0.1 12345 127.0.0.1 12346 Joueur1
./sh13 127.0.0.1 12345 127.0.0.1 12347 Joueur2
./sh13 127.0.0.1 12345 127.0.0.1 12348 Joueur3
./sh13 127.0.0.1 12345 127.0.0.1 12349 Joueur4
```

Chaque client **écoute** sur un port qui lui est propre (`12346‑9` ci‑dessus) pour que le serveur puisse pousser les messages.

---

## 8. Notes de développement

* La synchronisation serveur → clients par **connexions TCP éphémères** simplifie énormément la logique côté serveur (aucune `select` / `poll` nécessaire).  
* L’interface SDL a été pensée pour rester fluide : seul le thread de rendu touche à SDL, le thread réseau n’interagit qu’avec des **buffers C bas niveau**.  
* Au besoin, la taille des buffers (messages ≤ 255 caractères) est vérifiée, et toutes les chaînes copiées via `snprintf` / `strncpy` longueur‑bornée.

---

## 9. Remerciements

> Un grand merci à **François Pecheux** et **Thibault Hilaire** pour la qualité de leurs cours et leurs conseils précieux tout au long de ce projet.

---

## 10. Licence

Projet réalisé dans un cadre pédagogique , utilisation open-source.
