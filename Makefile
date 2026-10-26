# Makefile du projet ipc-charging-station-network
#
# Commandes utiles :
#   make        compile tous les modules (.o) dans build/
#   make test   compile et lance les tests
#   make clean  supprime build/
#
# RAPPEL : les lignes de commande sous une regle doivent commencer par une
# TABULATION, pas par des espaces (sinon : "missing separator").

# --- Configuration ---------------------------------------------------------

CC      = gcc
# -std=c11              : norme du C utilisee
# -Wall -Wextra -pedantic : un maximum d'avertissements (a corriger, pas ignorer)
# -g                    : infos de debogage (pour gdb / valgrind)
# -Isrc/include         : dossier ou chercher les #include "xxx.h"
CFLAGS  = -std=c11 -Wall -Wextra -pedantic -g -Isrc/include
# Bibliotheques a lier (vide pour l'instant ; -pthread quand on aura des threads)
LDLIBS  =

BUILD   = build

# Tous les .c de src/ ; chaque collegue n'a qu'a deposer son fichier dans
# src/ pour qu'il soit compile, sans toucher au Makefile.
SRC     = $(wildcard src/*.c)
# src/allocator.c -> build/allocator.o
OBJ     = $(patsubst src/%.c,$(BUILD)/%.o,$(SRC))

# --- Regles ----------------------------------------------------------------

# "phony" = ce ne sont pas des fichiers, make doit toujours les executer.
.PHONY: all test clean

# Premiere regle = regle par defaut (celle lancee par "make" tout court).
all: $(OBJ)

# Regle generique : comment fabriquer build/X.o a partir de src/X.c.
#   $<  = la premiere dependance (le .c)
#   $@  = la cible (le .o)
# Le .o est recompile si le .c OU les .h ont change.
# "| $(BUILD)" : il faut que le dossier build existe (sans dependre de sa date).
$(BUILD)/%.o: src/%.c $(wildcard src/include/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

# --- Tests -----------------------------------------------------------------

# Executable de test = le fichier de test + le module teste.
#   $^ = toutes les dependances
$(BUILD)/test_message_queue: tests/test_message_queue.c $(BUILD)/message_queue.o | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

test: $(BUILD)/test_message_queue
	./$(BUILD)/test_message_queue

clean:
	rm -rf $(BUILD)
