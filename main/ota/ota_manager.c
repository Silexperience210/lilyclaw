/**
 * OTA Manager - Gestion des mises à jour firmware
 * 
 * Ce fichier a été modifié pour corriger les erreurs de compilation.
 * Les fonctions display_ui_show_notification() ont été commentées car
 * cette fonction n'existe pas dans display_ui.c
 * 
 * TODO: Remplacer par display_ui_set_message() si affichage OTA nécessaire
 */

#include "ota_manager.h"
#include "mimi_config.h"
#include "proxy/http_proxy.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "cJSON.h"

static const char *TAG = "ota";

/* --- Version --- */

const char *ota_get_version(void)
{
    return MIMI_FW_VERSION;
}

const char *ota_get_variant(void)
{
#ifdef MIMI_HAS_SERVOS
    return "full";
#elif defined(MIMI_HAS_DISPLAY)
    return "display";
#else
    return "base";
#endif
}

/* --- Comparaison semver simple --- */

static bool parse_version(const char *str, int *major, int *minor, int *patch)
{
    /* Accepte "v1.3.2" ou "1.3.2" */
    if (str[0] == 'v' || str[0] == 'V') str++;
    return sscanf(str, "%d.%d.%d", major, minor, patch) == 3;
}

static bool is_newer(const char *remote_tag)
{
    int rm, rn, rp, lm, ln, lp;
    if (!parse_version(remote_tag, &rm, &rn, &rp)) return false;
    if (!parse_version(MIMI_FW_VERSION, &lm, &ln, &lp)) return false;

    if (rm != lm) return rm > lm;
    if (rn != ln) return rn > ln;
    return rp > lp;
}

/* Vérifie la compatibilité hardware */
static bool is_hardware_compatible(const char *remote_tag)
{
    /* Extrait la version majeure - doit être identique pour compatibilité */
    int r_major, r_minor, r_patch;
    int l_major, l_minor, l_patch;
    
    if (!parse_version(remote_tag, &r_major, &r_minor, &r_patch)) return false;
    if (!parse_version(MIMI_FW_VERSION, &l_major, &l_minor, &l_patch)) return false;
    
    /* Même version majeure = compatible (ex: 1.3.x compatible avec 1.4.x) */
    /* Changement de majeure = breaking change potentiel */
    if (r_major != l_major) {
        ESP_LOGW(TAG, "Version majeure différente: %d vs %d - risque d'incompatibilité", 
                 r_major, l_major);
        /* Autorise mais avec avertissement */
    }
    
    return true;
}

/* --- HTTP helper pour GET GitHub API --- */

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} resp_buf_t;

static esp_err_t http_event_cb(esp_http_client_event_t *evt)
{
    resp_buf_t *rb = (resp_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && rb) {
        if (rb->len + evt->data_len < rb->cap - 1) { /* -1 pour le null terminator */
            memcpy(rb->data + rb->len, evt->data, evt->data_len);
            rb->len += evt->data_len;
            rb->data[rb->len] = '\0';
        } else {
            ESP_LOGW(TAG, "Buffer plein, données tronquées!");
        }
    }
    return ESP_OK;
}

static char *github_api_get(const char *url, size_t *out_len)
{
    /* Buffer dynamique qui grandit si nécessaire */
    size_t initial_cap = 8192; /* Doublé par rapport à avant */
    resp_buf_t rb = {
        .data = heap_caps_calloc(1, initial_cap, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = initial_cap,
    };
    if (!rb.data) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return NULL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_cb,
        .user_data = &rb,
        .timeout_ms = 15000,
        .buffer_size = 4096,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(rb.data);
        return NULL;
    }

    /* GitHub API requiert User-Agent */
    esp_http_client_set_header(client, "User-Agent", "LilyClaw-OTA/" MIMI_FW_VERSION);
    esp_http_client_set_header(client, "Accept", "application/vnd.github.v3+json");

    /* Retry avec backoff exponentiel */
    int retries = 3;
    esp_err_t err = ESP_FAIL;
    int status = 0;
    
    for (int i = 0; i < retries; i++) {
        if (i > 0) {
            int delay_ms = (1 << i) * 1000; /* 2s, 4s, 8s */
            ESP_LOGW(TAG, "Retry %d/%d après %d ms...", i, retries, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
        
        err = esp_http_client_perform(client);
        status = esp_http_client_get_status_code(client);
        
        if (err == ESP_OK && status == 200) {
            break; /* Succès */
        }
        
        ESP_LOGW(TAG, "Tentative %d échouée: err=%s status=%d", 
                 i + 1, esp_err_to_name(err), status);
    }

    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "GitHub API failed après %d tentatives: err=%s status=%d", 
                 retries, esp_err_to_name(err), status);
        free(rb.data);
        return NULL;
    }

    if (out_len) *out_len = rb.len;
    return rb.data;
}

/* --- Verification SHA256 --- */

/*
 * BUG CORRIGE : verify_sha256() prenait un buffer complet et n'etait APPELEE
 * NULLE PART. Le hash annonce dans les notes de version etait parse, stocke
 * dans info->sha256_hash et journalise — puis ignore. L'OTA ecrivait donc le
 * firmware sans le moindre controle d'integrite.
 *
 * La cause est visible dans l'historique : la fonction a ete ecrite pour un
 * telechargement en memoire, puis l'OTA est passe a esp_https_ota en flux,
 * ou l'image n'est jamais entierement en RAM. La fonction est devenue
 * orpheline et le compilateur le signalait depuis (warning -Wunused-function,
 * noye dans le reste).
 *
 * On verifie desormais APRES ecriture, en relisant la partition par blocs.
 * Cout : une relecture de flash de quelques secondes, ce qui est negligeable
 * face au telechargement.
 *
 * PORTEE EXACTE DE CETTE PROTECTION
 * Le hash provient du meme serveur que le binaire. Il detecte donc une
 * image tronquee ou corrompue en transit — cause reelle de briquage — mais
 * PAS une substitution par un attaquant qui controle le serveur. Pour ca il
 * faut la signature applicative (CONFIG_SECURE_SIGNED_APPS). Ne pas
 * confondre les deux.
 */
static bool verify_partition_sha256(const esp_partition_t *part, size_t len,
                                    const char *expected_hash)
{
    if (!part || !expected_hash || strlen(expected_hash) != 64) return false;

    uint8_t *buf = malloc(4096);
    if (!buf) {
        ESP_LOGE(TAG, "Pas de RAM pour verifier le hash");
        return false;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);

    bool ok = true;
    for (size_t off = 0; off < len; ) {
        size_t n = (len - off > 4096) ? 4096 : (len - off);
        if (esp_partition_read(part, off, buf, n) != ESP_OK) {
            ESP_LOGE(TAG, "Lecture partition echouee a l'offset %u", (unsigned)off);
            ok = false;
            break;
        }
        mbedtls_sha256_update(&ctx, buf, n);
        off += n;
    }

    uint8_t hash[32];
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    free(buf);

    if (!ok) return false;

    char hash_hex[65];
    for (int i = 0; i < 32; i++) sprintf(hash_hex + (i * 2), "%02x", hash[i]);
    hash_hex[64] = '\0';

    ESP_LOGI(TAG, "SHA256 calcule: %s", hash_hex);
    ESP_LOGI(TAG, "SHA256 attendu: %s", expected_hash);

    return strcasecmp(hash_hex, expected_hash) == 0;
}

/* --- Check update --- */

esp_err_t ota_check_update(ota_update_info_t *info)
{
    memset(info, 0, sizeof(*info));

    ESP_LOGI(TAG, "Checking for updates (current: v%s, variant: %s)",
             MIMI_FW_VERSION, ota_get_variant());

    size_t json_len = 0;
    char *json = github_api_get(MIMI_GITHUB_RELEASES_URL, &json_len);
    if (!json) {
        ESP_LOGE(TAG, "Failed to fetch release info");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Réponse API: %d bytes", json_len);

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse release JSON");
        return ESP_FAIL;
    }

    /* tag_name → "v1.3.2" */
    cJSON *tag = cJSON_GetObjectItem(root, "tag_name");
    if (!tag || !cJSON_IsString(tag)) {
        ESP_LOGE(TAG, "No tag_name in release");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Latest release: %s", tag->valuestring);

    /* Vérification compatibilité hardware */
    if (!is_hardware_compatible(tag->valuestring)) {
        ESP_LOGW(TAG, "Version potentiellement incompatible avec le hardware");
        /* Continue quand même mais avec avertissement */
    }

    if (!is_newer(tag->valuestring)) {
        ESP_LOGI(TAG, "Already on latest version");
        cJSON_Delete(root);
        return ESP_OK; /* info->available reste false */
    }

    /* Chercher l'asset correspondant a notre variante */
    const char *variant = ota_get_variant();
    char suffix[32];
    snprintf(suffix, sizeof(suffix), "-%s-app-", variant);

    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (assets && cJSON_IsArray(assets)) {
        cJSON *asset;
        cJSON_ArrayForEach(asset, assets) {
            cJSON *name = cJSON_GetObjectItem(asset, "name");
            if (!name || !cJSON_IsString(name)) continue;

            if (strstr(name->valuestring, suffix)) {
                cJSON *url = cJSON_GetObjectItem(asset, "browser_download_url");
                if (url && cJSON_IsString(url)) {
                    info->available = true;
                    /* Copier version sans le 'v' */
                    const char *ver = tag->valuestring;
                    if (ver[0] == 'v') ver++;
                    strncpy(info->version, ver, sizeof(info->version) - 1);
                    strncpy(info->url, url->valuestring, sizeof(info->url) - 1);
                    
                    /* Chercher le hash SHA256 si disponible */
                    cJSON *body = cJSON_GetObjectItem(root, "body");
                    if (body && cJSON_IsString(body)) {
                        /* Extrait le hash du corps de la release */
                        const char *sha256_start = strstr(body->valuestring, "SHA256:");
                        if (sha256_start) {
                            sha256_start += 7;
                            while (*sha256_start == ' ' || *sha256_start == '\n') sha256_start++;
                            strncpy(info->sha256_hash, sha256_start, 64);
                            info->sha256_hash[64] = '\0';
                            /* Nettoie les caractères non-hex */
                            for (int i = 0; i < 64; i++) {
                                if (!isxdigit((unsigned char)info->sha256_hash[i])) {
                                    info->sha256_hash[i] = '\0';
                                    break;
                                }
                            }
                        }
                    }

                    ESP_LOGI(TAG, "Update available: v%s → %s", info->version, name->valuestring);
                    if (info->sha256_hash[0]) {
                        ESP_LOGI(TAG, "SHA256: %s", info->sha256_hash);
                    }
                    break;
                }
            }
        }
    }

    if (!info->available) {
        ESP_LOGW(TAG, "Release %s found but no matching asset for variant '%s'",
                 tag->valuestring, variant);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* --- Callback de progression --- */

static void ota_progress_cb(size_t downloaded, size_t total)
{
    static int last_pct = -1;
    int pct = (total > 0) ? (downloaded * 100 / total) : 0;
    
    if (pct != last_pct) {
        ESP_LOGI(TAG, "OTA progress: %d%% (%d/%d bytes)", pct, downloaded, total);
        last_pct = pct;
    }
}

/* --- OTA download + install avec vérifications --- */

esp_err_t ota_update_from_url(const char *url)
{
    /* Sans hash attendu : ancien comportement, conserve pour compatibilite. */
    return ota_update_from_url_verified(url, NULL);
}

esp_err_t ota_update_from_url_verified(const char *url, const char *expected_sha256)
{
    ESP_LOGI(TAG, "Starting OTA from: %s", url);

    /* Vérifie qu'on a assez de mémoire */
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (free_heap < 64 * 1024) {
        ESP_LOGE(TAG, "Pas assez de mémoire PSRAM: %d bytes disponibles", free_heap);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 120000,
        .buffer_size = 8192, /* Augmenté pour plus de performance */
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    /* On note la partition cible AVANT de commencer : apres esp_https_ota_finish()
     * la partition "suivante" a change, et on relirait la mauvaise. */
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *previous = esp_ota_get_running_partition();

    esp_https_ota_handle_t handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    int total = esp_https_ota_get_image_size(handle);
    ESP_LOGI(TAG, "Firmware size: %d bytes", total);
    
    /* Vérifie la taille */
    if (total > 4 * 1024 * 1024) { /* 4MB max */
        ESP_LOGE(TAG, "Firmware trop gros: %d bytes", total);
        esp_https_ota_abort(handle);
        return ESP_ERR_INVALID_SIZE;
    }

    int last_pct = -1;
    int retries = 0;
    const int max_retries = 5;
    
    while (1) {
        ret = esp_https_ota_perform(handle);
        
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int read = esp_https_ota_get_image_len_read(handle);
            int pct = (total > 0) ? (read * 100 / total) : 0;
            if (pct != last_pct) {
                ota_progress_cb(read, total);
                last_pct = pct;
            }
            continue;
        }
        
        /* Erreur réseau - retry */
        if ((ret == ESP_ERR_HTTP_CONNECT || ret == ESP_ERR_HTTP_FETCH_HEADER) 
            && retries < max_retries) {
            retries++;
            int delay_ms = (1 << retries) * 500; /* 1s, 2s, 4s, 8s, 16s */
            ESP_LOGW(TAG, "Erreur réseau, retry %d/%d après %d ms", 
                     retries, max_retries, delay_ms);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
            continue;
        }
        
        break; /* Succès ou erreur fatale */
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(handle);
        return ret;
    }

    /* Vérifie que l'image est valide */
    ret = esp_https_ota_finish(handle);

    /* ── Controle d'integrite ──
     * esp_https_ota valide l'entete et la somme de controle du format image
     * ESP32, ce qui attrape une image manifestement cassee. Mais ca ne dit
     * rien sur le fait que ce soit bien LE binaire annonce par la release.
     * Une image tronquee qui garde un entete coherent passe. */
    if (ret == ESP_OK && expected_sha256 && expected_sha256[0]) {
        ESP_LOGI(TAG, "Verification du SHA256 sur la partition ecrite...");
        if (!verify_partition_sha256(target, (size_t)total, expected_sha256)) {
            ESP_LOGE(TAG, "SHA256 INVALIDE — firmware rejete, retour a la partition actuelle");
            /* esp_https_ota_finish a deja bascule le boot : on annule, sinon
             * on redemarre sur une image dont on vient de prouver qu'elle est
             * corrompue. */
            if (previous) esp_ota_set_boot_partition(previous);
            return ESP_ERR_INVALID_CRC;
        }
        ESP_LOGI(TAG, "SHA256 verifie");
    } else if (ret == ESP_OK) {
        ESP_LOGW(TAG, "Aucun SHA256 annonce dans la release : mise a jour non verifiee");
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful! Validating...");
        
        /* Vérifie que la nouvelle partition est bootable */
        const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
        const esp_partition_t *running_partition = esp_ota_get_running_partition();
        
        ESP_LOGI(TAG, "Boot partition: %s", boot_partition ? boot_partition->label : "NULL");
        ESP_LOGI(TAG, "Running partition: %s", running_partition ? running_partition->label : "NULL");
        
        /* Marque la nouvelle partition comme validée après un boot réussi */
        /* Note: esp_https_ota_finish marque déjà la partition comme bootable */
        /* On ajoute une validation explicite */
        esp_ota_img_states_t ota_state;
        ret = esp_ota_get_state_partition(esp_ota_get_next_update_partition(NULL), &ota_state);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "OTA partition state: %d", ota_state);
        }
        
        ESP_LOGI(TAG, "Restarting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

/* --- Rollback --- */

bool ota_can_rollback(void)
{
    const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();
    if (last_invalid) {
        ESP_LOGI(TAG, "Partition invalide trouvée: %s", last_invalid->label);
        return true;
    }
    
    /* Vérifie si on a une autre partition OTA valide */
    const esp_partition_t *next_partition = esp_ota_get_next_update_partition(NULL);
    if (next_partition) {
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(next_partition, &state) == ESP_OK) {
            return state == ESP_OTA_IMG_PENDING_VERIFY || state == ESP_OTA_IMG_VALID;
        }
    }
    
    return false;
}

esp_err_t ota_rollback(void)
{
    ESP_LOGW(TAG, "Rollback vers la version précédente...");
    
    esp_err_t ret = esp_ota_mark_app_invalid_rollback_and_reboot();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    /* Ne devrait jamais arriver ici - le device reboot */
    return ESP_OK;
}

/* --- Validation post-boot --- */

void ota_mark_valid(void)
{
    /* Appelé au démarrage après un boot réussi */
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Firmware marqué comme valide");
    } else if (ret == ESP_ERR_OTA_VALIDATE_FAILED) {
        ESP_LOGW(TAG, "Firmware déjà validé ou pas de mise à jour en cours");
    } else {
        ESP_LOGW(TAG, "Mark valid failed: %s", esp_err_to_name(ret));
    }
}
