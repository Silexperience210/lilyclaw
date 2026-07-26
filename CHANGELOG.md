# Changelog

## v1.5.0 — Audit de securite et de fiabilite

Release de correction. Aucune fonctionnalite nouvelle : 24 defauts corriges,
dont 5 pouvant corrompre la memoire et 4 de securite.

### Corrige — corruption memoire

- **Debordement de tas generalise** : le motif `off += snprintf(buf+off, size-off, ...)`
  etait utilise dans 6 fichiers. `snprintf` retourne la longueur *qu'il aurait ecrite* :
  a la premiere troncature `size - off` deborde en `size_t`. Pire cas : un `fread`
  de ~4 Go dans `context_builder.c`, declenchable par un `MEMORY.md` volumineux
  ecrit par le LLM lui-meme. Nouveau helper `main/util/safe_str.h`.
- **Debordement de pile** : `context_build_system_prompt()` declarait 8 Ko de
  locaux dans une tache qui dispose de 12 Ko. Buffers deplaces en PSRAM.
- **Lecture hors pile** : `snprintf` non borne avant `proxy_conn_write()` dans les
  3 chemins proxy — pouvait envoyer de la memoire de pile vers l'API distante.
- **Ecriture hors tableau** dans `session_get_history_json` quand
  `MIMI_AGENT_MAX_HISTORY > MIMI_SESSION_MAX_MSGS`. Clamp + `_Static_assert`.
- **Use-after-free** dans `tg_response_is_ok` : le pointeur rendu designait
  l'arbre cJSON libere la ligne suivante.

### Corrige — securite

- **WebSocket sans authentification** (`0.0.0.0:18789`) : n'importe qui sur le
  reseau pouvait piloter l'agent (write_file, OTA, servos) et usurper le
  `chat_id` d'un autre client pour lire son historique. Jeton NVS + handshake
  `{"type":"auth","token":"..."}` + commande CLI `ws_token`.
- **SSRF via `http_fetch`** : blocage par defaut des plages privees, loopback,
  link-local et metadata cloud. Reactivable via `MIMI_HTTP_FETCH_ALLOW_LAN=1`.
- **Reponse aux chats non autorises** : "Access denied." confirmait l'existence
  du bot et generait du trafic sortant. On journalise seulement.
- **Champ interne `_kimi_reasoning`** fuite vers l'API Anthropic au changement
  de fournisseur (rejet 400).

### Corrige — fiabilite

- **`Transfer-Encoding: chunked` non decode** sur les 3 chemins proxy. Anthropic,
  Telegram et Brave repondent en chunked : les marqueurs de taille hexadecimaux
  restaient dans le JSON. Cause des "Failed to parse response" aleatoires.
  Nouveau helper `main/util/http_raw.h`.
- **Boucle serree** sur token Telegram invalide (401 immediat, aucun delai).
  Backoff exponentiel plafonne a 64 s.
- **Spam de messages "working..."** : envoye avant chaque appel API de la boucle
  ReAct (jusqu'a 10 par question). Desormais aux iterations 0, 3 et 6.
- **Fichiers de session sans limite** : compaction automatique au-dela de 24 Ko.
- **Messages longs perdus** de l'historique (`char line[2048]` vs reponses 4096).
- **Decoupage Telegram cassant l'UTF-8** : les emojis et accents coupes en deux
  faisaient rejeter le message entier. Coupe sur frontiere UTF-8 puis sur
  saut de ligne ou espace.
- **Deduplication a collisions** : `h << 16` jetait les bits hauts du hash FNV.
- **`MIMI_SCHEDULER_MAX_TASKS`** defini mais jamais applique — depasser la taille
  effacait silencieusement toutes les taches planifiees.
- **Tache planifiee sans `chat_id`** : creee puis declenchee dans le vide.
- **Reessais inutiles** sur erreur definitive (401) : 3 tentatives, 9 s perdues.
- **Messages d'erreur** : une cause distincte par panne au lieu de
  "Sorry, I encountered an error."
- **Fuites memoire** : retours de `message_bus_push_*` ignores, `cJSON` non libere.
- **Course de donnees** sur `s_clients` (thread httpd vs tache outbound). Mutex.
- **Dereferencements NULL** : `valuestring` sans `cJSON_IsString` (8 occurrences).
- **Code mort** : la branche creant l'en-tete des notes quotidiennes etait
  inatteignable, l'en-tete n'etait donc jamais ecrit.
- **Ecritures SPIFFS non verifiees** : disque plein = perte silencieuse.

### Ajoute

- `main/util/safe_str.h` — constructeur de chaines borne + troncature UTF-8.
- `main/util/http_raw.h` — decodeur `Transfer-Encoding: chunked`.
- Commande CLI `ws_token`.

