/* SPDX-License-Identifier: MIT */
/* Unit tests for the file-backed object store (FEAT-033). 100% line+branch. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "store.h"

/* A scratch directory for the run; each test uses a subdir under it. */
static char g_tmp[256];

static void make_tmp(void) {
    strcpy(g_tmp, "/tmp/norn_store_testXXXXXX");
    assert(mkdtemp(g_tmp) != NULL);
}

static void test_init(void) {
    nornd_store_t s;
    /* Null arguments. */
    assert(nornd_store_init(NULL, g_tmp) == -1);
    assert(nornd_store_init(&s, NULL) == -1);
    /* Empty and over-long roots. */
    assert(nornd_store_init(&s, "") == -1);
    char longp[600];
    memset(longp, 'a', sizeof(longp) - 1);
    longp[sizeof(longp) - 1] = '\0';
    assert(nornd_store_init(&s, longp) == -1);
    /* mkdir failure: parent does not exist. */
    char bad[300];
    snprintf(bad, sizeof(bad), "%s/nope/child", g_tmp);
    assert(nornd_store_init(&s, bad) == -1);

    /* Fresh create, then idempotent re-init over the existing dir (EEXIST). */
    char root[300];
    snprintf(root, sizeof(root), "%s/obj", g_tmp);
    assert(nornd_store_init(&s, root) == 0);
    assert(s.rootlen == strlen(root));
    nornd_store_t s2;
    assert(nornd_store_init(&s2, root) == 0);
}

static void test_put_and_stat(void) {
    nornd_store_t s;
    char root[300];
    snprintf(root, sizeof(root), "%s/objects", g_tmp);
    assert(nornd_store_init(&s, root) == 0);

    char h[NORND_STORE_HASH_HEX + 1];
    /* put guards. */
    assert(nornd_store_put(NULL, (const unsigned char *)"x", 1, h) == -1);
    assert(nornd_store_put(&s, NULL, 1, h) == -1);
    assert(nornd_store_put(&s, (const unsigned char *)"x", 1, NULL) == -1);

    /* Normal object. */
    const unsigned char body[] = "hello, norn";
    assert(nornd_store_put(&s, body, sizeof(body) - 1, h) == 0);
    assert(strlen(h) == NORND_STORE_HASH_HEX);

    /* Empty object exercises the `len == 0` branch (no fwrite). */
    char he[NORND_STORE_HASH_HEX + 1];
    assert(nornd_store_put(&s, (const unsigned char *)"", 0, he) == 0);

    /* Re-storing identical bytes is idempotent (stat hits, write skipped). */
    char h2[NORND_STORE_HASH_HEX + 1];
    assert(nornd_store_put(&s, body, sizeof(body) - 1, h2) == 0);
    assert(strcmp(h, h2) == 0);

    /* stat guards. */
    uint64_t len = 0;
    char path[600];
    assert(nornd_store_stat(NULL, h, NORND_STORE_HASH_HEX, path, sizeof(path),
                            &len) == -1);
    assert(nornd_store_stat(&s, NULL, NORND_STORE_HASH_HEX, path, sizeof(path),
                            &len) == -1);
    assert(nornd_store_stat(&s, h, 5, path, sizeof(path), &len) == -1);

    /* Malformed hash (a non-hex char keeps the length but fails validation). */
    char bad[NORND_STORE_HASH_HEX + 1];
    memcpy(bad, h, NORND_STORE_HASH_HEX + 1);
    bad[10] = 'Z';
    assert(nornd_store_stat(&s, bad, NORND_STORE_HASH_HEX, path, sizeof(path),
                            &len) == -1);

    /* Well-formed but absent. */
    char absent[NORND_STORE_HASH_HEX + 1];
    memset(absent, '0', NORND_STORE_HASH_HEX);
    absent[NORND_STORE_HASH_HEX] = '\0';
    assert(nornd_store_stat(&s, absent, NORND_STORE_HASH_HEX, path, sizeof(path),
                            &len) == -1);

    /* Present: path + length resolved. */
    assert(nornd_store_stat(&s, h, NORND_STORE_HASH_HEX, path, sizeof(path),
                            &len) == 0);
    assert(len == sizeof(body) - 1);
    /* The resolved path is readable and holds exactly the stored bytes. */
    FILE *f = fopen(path, "rb");
    assert(f);
    unsigned char rb[64];
    size_t rn = fread(rb, 1, sizeof(rb), f);
    fclose(f);
    assert(rn == sizeof(body) - 1 && memcmp(rb, body, rn) == 0);

    /* path_out NULL and len_out NULL are both honored (optional outputs). */
    assert(nornd_store_stat(&s, h, NORND_STORE_HASH_HEX, NULL, 0, NULL) == 0);
    /* Too-small path buffer. */
    assert(nornd_store_stat(&s, h, NORND_STORE_HASH_HEX, path, 4, &len) == -1);

    /* A directory whose name is a valid hash is rejected (not a regular file). */
    char dirhash[NORND_STORE_HASH_HEX + 1];
    memset(dirhash, 'a', NORND_STORE_HASH_HEX);
    dirhash[NORND_STORE_HASH_HEX] = '\0';
    char dpath[600];
    snprintf(dpath, sizeof(dpath), "%s/%s", root, dirhash);
    assert(mkdir(dpath, 0700) == 0);
    assert(nornd_store_stat(&s, dirhash, NORND_STORE_HASH_HEX, path,
                            sizeof(path), &len) == -1);
}

int main(void) {
    make_tmp();
    test_init();
    test_put_and_stat();
    printf("all nornd store tests passed\n");
    return 0;
}
