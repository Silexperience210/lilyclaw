# Apercu de l'ecran sur hote, sans materiel.
#   make && ./preview 3     -> genere out/*.ppm a l'echelle 3
#
# Permet de juger un changement de design en quelques secondes au lieu de
# flasher une carte. C'est ce qui a permis de detecter que la premiere
# version du rendu etait quasi invisible (1,3 % de pixels au-dessus de 60).
CFLAGS = -O2 -Wall -Wextra -std=gnu11 -I../../main
SRC  = preview.c  ../../main/display/render.c ../../main/display/screen_presence.c
SRC2 = preview2.c ../../main/display/render.c ../../main/display/screen_field.c

all: preview preview2

preview: $(SRC)
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

preview2: $(SRC2)
	@mkdir -p out
	$(CC) $(CFLAGS) -o $@ $(SRC2) -lm

.PHONY: run clean
run: preview preview2
	./preview 3
	./preview2 3

clean:
	rm -f preview preview2 out/*.ppm out/*.png
