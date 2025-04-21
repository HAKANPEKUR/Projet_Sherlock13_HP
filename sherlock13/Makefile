# Makefile pour Sherlock 13

CC=gcc

CFLAGS=-Wall -g -I/usr/include/SDL2 -pthread

LDFLAGS=-lSDL2 -lSDL2_image -lSDL2_ttf -lm -pthread

# Cibles exécutables
TARGET_SERVER=server
TARGET_CLIENT=sh13

# Fichiers source
SRC_SERVER=server.c
SRC_CLIENT=sh13.c

# Règle par défaut (exécutée si on tape juste 'make')
all: $(TARGET_SERVER) $(TARGET_CLIENT)

# Règle pour compiler le serveur
$(TARGET_SERVER): $(SRC_SERVER)
	$(CC) $(CFLAGS) -o $(TARGET_SERVER) $(SRC_SERVER)

# Règle pour compiler le client
$(TARGET_CLIENT): $(SRC_CLIENT)
	$(CC) $(CFLAGS) -o $(TARGET_CLIENT) $(SRC_CLIENT) $(LDFLAGS)

# Règle pour nettoyer les fichiers compilés
clean:
	rm -f $(TARGET_SERVER) $(TARGET_CLIENT) *.o

# Déclare les règles qui ne correspondent pas à des noms de fichiers
.PHONY: all clean