#include "nsdock/common.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGISTRY_AUTH   "https://auth.docker.io/token"
#define REGISTRY_HOST   "https://registry-1.docker.io"
#define MAX_LAYERS      32
#define MAX_JSON_SIZE   (1024 * 1024)

typedef struct {
    char *data;
    size_t size;
} http_buffer_t;

typedef struct {
    char digest[128];
} nsdock_layer_ref_t;

typedef struct {
    nsdock_layer_ref_t layers[MAX_LAYERS];
    int layer_count;
} nsdock_manifest_t;

// callback do curl: vai acumulando a resposta HTTP num buffer dinâmico
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    http_buffer_t *buf = (http_buffer_t *)userp;

    char *new_data = realloc(buf->data, buf->size + total + 1);
    if (!new_data)
        return 0; // sinaliza erro pro curl

    buf->data = new_data;
    memcpy(buf->data + buf->size, contents, total);
    buf->size += total;
    buf->data[buf->size] = '\0';

    return total;
}

static nsdock_status_t http_get(const char *url, const char *auth_header, http_buffer_t *out) {
    CURL *curl = curl_easy_init();
    if (!curl)
        return NSDOCK_ERR_UNKNOWN;

    out->data = malloc(1);
    out->size = 0;
    out->data[0] = '\0';

    struct curl_slist *headers = NULL;
    if (auth_header)
        headers = curl_slist_append(headers, auth_header);
    // pede o formato de manifest v2 (Docker) e também OCI, o registry escolhe o que tiver
    headers = curl_slist_append(headers, "Accept: application/vnd.docker.distribution.manifest.v2+json");
    headers = curl_slist_append(headers, "Accept: application/vnd.oci.image.manifest.v1+json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return NSDOCK_ERR_SYSCALL;
    if (http_code == 401 || http_code == 403)
        return NSDOCK_ERR_PERMISSION;
    if (http_code == 404)
        return NSDOCK_ERR_NOT_FOUND;
    if (http_code >= 400)
        return NSDOCK_ERR_UNKNOWN;

    return NSDOCK_OK;
}

// Extrai um campo string simples de um JSON, ex: "token":"abc123"
// Nota: isso é um scanner ingênuo, não um parser JSON completo -- serve
// pros campos previsíveis do registry. Se o projeto crescer, trocar por cJSON.
static nsdock_status_t json_extract_string(const char *json, const char *key, char *out, size_t out_size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":\"", key);

    const char *start = strstr(json, search);
    if (!start)
        return NSDOCK_ERR_NOT_FOUND;

    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end)
        return NSDOCK_ERR_UNKNOWN;

    size_t len = end - start;
    if (len >= out_size)
        len = out_size - 1;

    memcpy(out, start, len);
    out[len] = '\0';
    return NSDOCK_OK;
}

// Docker Hub exige um token anônimo de "pull" antes de qualquer request no registry,
// mesmo pra imagens públicas
static nsdock_status_t fetch_pull_token(const char *image_name, char *token_out, size_t token_size) {
    char url[NSDOCK_MAX_PATH];
    snprintf(url, sizeof(url),
             "%s?service=registry.docker.io&scope=repository:library/%s:pull",
             REGISTRY_AUTH, image_name);

    http_buffer_t buf;
    nsdock_status_t status = http_get(url, NULL, &buf);
    if (status != NSDOCK_OK) {
        free(buf.data);
        return status;
    }

    status = json_extract_string(buf.data, "token", token_out, token_size);
    free(buf.data);
    return status;
}

// Baixa e faz parse do manifest, populando a lista de digests das camadas.
// Nota: parser simplificado -- assume que cada camada aparece como
// {"digest":"sha256:...", ...} dentro de "layers":[...]
nsdock_status_t nsdock_manifest_resolve_platform(const char *manifest_list_json,
                                                   char *digest_out, size_t digest_out_size);

static nsdock_status_t fetch_manifest(const char *image_name, const char *tag,
                                       const char *token, nsdock_manifest_t *manifest) {
    char url[NSDOCK_MAX_PATH];
    snprintf(url, sizeof(url), "%s/v2/library/%s/manifests/%s", REGISTRY_HOST, image_name, tag);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);

    http_buffer_t buf;
    nsdock_status_t status = http_get(url, auth_header, &buf);
    if (status != NSDOCK_OK) {
        free(buf.data);
        return status;
    }

    // Se vier uma manifest list (multi-arquitetura), precisamos resolver
    // pra plataforma certa e buscar o manifest real de novo
    if (strstr(buf.data, "\"manifests\":[") != NULL) {
        char resolved_digest[128];
        nsdock_status_t resolve_status = nsdock_manifest_resolve_platform(
            buf.data, resolved_digest, sizeof(resolved_digest));

        free(buf.data);

        if (resolve_status != NSDOCK_OK)
            return resolve_status;

        // busca de novo, agora usando o digest específico da arquitetura certa
        char digest_url[NSDOCK_MAX_PATH];
        snprintf(digest_url, sizeof(digest_url), "%s/v2/library/%s/manifests/%s",
                 REGISTRY_HOST, image_name, resolved_digest);

        status = http_get(digest_url, auth_header, &buf);
        if (status != NSDOCK_OK) {
            free(buf.data);
            return status;
        }
    }

    manifest->layer_count = 0;
    const char *cursor = buf.data;

    while (manifest->layer_count < MAX_LAYERS) {
        const char *digest_key = strstr(cursor, "\"digest\":\"");
        if (!digest_key)
            break;

        digest_key += strlen("\"digest\":\"");
        const char *digest_end = strchr(digest_key, '"');
        if (!digest_end)
            break;

        size_t len = digest_end - digest_key;
        if (len >= sizeof(manifest->layers[0].digest))
            len = sizeof(manifest->layers[0].digest) - 1;

        memcpy(manifest->layers[manifest->layer_count].digest, digest_key, len);
        manifest->layers[manifest->layer_count].digest[len] = '\0';
        manifest->layer_count++;

        cursor = digest_end;
    }

    free(buf.data);

    if (manifest->layer_count == 0)
        return NSDOCK_ERR_UNKNOWN;

    return NSDOCK_OK;
}

// Baixa uma camada (blob) e salva como arquivo .tar.gz no destino indicado.
// A extração de fato fica pro layer.c
static nsdock_status_t download_blob(const char *image_name, const char *digest,
                                      const char *token, const char *dest_path) {
    char url[NSDOCK_MAX_PATH];
    snprintf(url, sizeof(url), "%s/v2/library/%s/blobs/%s", REGISTRY_HOST, image_name, digest);

    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);

    CURL *curl = curl_easy_init();
    if (!curl)
        return NSDOCK_ERR_UNKNOWN;

    FILE *f = fopen(dest_path, "wb");
    if (!f) {
        curl_easy_cleanup(curl);
        return NSDOCK_ERR_SYSCALL;
    }

    struct curl_slist *headers = curl_slist_append(NULL, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L); // camadas podem ser grandes

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    fclose(f);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code >= 400)
        return NSDOCK_ERR_SYSCALL;

    return NSDOCK_OK;
}

// Ponto de entrada: baixa uma imagem completa (todas as camadas) pra pasta destino.
// image_name ex: "alpine", tag ex: "latest". dest_dir é onde os .tar.gz das camadas vão parar.
nsdock_status_t nsdock_image_pull(const char *image_name, const char *tag, const char *dest_dir) {
    char token[2048];
    nsdock_status_t status = fetch_pull_token(image_name, token, sizeof(token));
    if (status != NSDOCK_OK)
        return status;

    nsdock_manifest_t manifest;
    status = fetch_manifest(image_name, tag, token, &manifest);
    if (status != NSDOCK_OK)
        return status;

    for (int i = 0; i < manifest.layer_count; i++) {
        char dest_path[NSDOCK_MAX_PATH];
        snprintf(dest_path, sizeof(dest_path), "%s/layer_%d.tar.gz", dest_dir, i);

        status = download_blob(image_name, manifest.layers[i].digest, token, dest_path);
        if (status != NSDOCK_OK)
            return status;
    }

    return NSDOCK_OK;
}
