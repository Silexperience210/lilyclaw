#pragma once

/*
 * soul_task.h — la tache qui fait courir l'etat interieur.
 *
 * Un tick par seconde : fait deriver les pulsions (drives), echantillonne les
 * capteurs, et decide via salience si quelque chose merite de reveiller le
 * LLM. Si oui, pousse un message sur le canal MIMI_CHAN_SELF vers l'agent.
 *
 * Tourne sur toutes les variantes materielles, y compris celle sans capteurs :
 * un appareil sans corps garde une horloge, une memoire et une histoire.
 */

#include "esp_err.h"

/** Demarre la tache de vie. Appelle drives_init() en interne. */
esp_err_t soul_task_start(void);

/**
 * Signale que la sentinelle radar a vu la piece changer.
 *
 * En mode push et non en poll : sonar_radar_check_intrusion() *consomme*
 * l'alerte, et body_animator la consomme deja. Deux lecteurs se voleraient
 * l'evenement — l'un des deux ne verrait jamais rien, de facon intermittente.
 * C'est le genre de bug qu'on met un mois a reproduire.
 */
void soul_notify_room_changed(void);

/**
 * Signale un contact humain (message recu). Remet a zero la faim de contact
 * et le compteur de silence.
 */
void soul_notify_contact(void);
