/* SPDX-License-Identifier: MIT */
#include "norn_bonjour.h"
#include "norn.h"
#include "norn_internal.h"
#include "mainline.h"

#ifdef HAVE_AVAHI
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-client/lookup.h>
#include <avahi-common/thread-watch.h>
#include <avahi-common/strlst.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NORN_SERVICE_TYPE "_norn._udp"
#define NORN_SERVICE_NAME "norn node"

struct norn_bonjour {
    struct norn_client *client;
    uint16_t port;
    unsigned char pubkey[32];

    AvahiThreadedPoll *threaded_poll;
    AvahiClient *avahi_client;
    AvahiEntryGroup *entry_group;
    AvahiServiceBrowser *browser;

    int running;
};

static void resolve_callback(AvahiServiceResolver *r,
                             AvahiIfIndex interface,
                             AvahiProtocol protocol,
                             AvahiResolverEvent event,
                             const char *name,
                             const char *type,
                             const char *domain,
                             const char *host_name,
                             const AvahiAddress *address,
                             uint16_t port,
                             AvahiStringList *txt,
                             AvahiLookupResultFlags flags,
                             void *userdata) {
    (void)protocol;
    (void)name;
    (void)type;
    (void)domain;
    (void)host_name;
    (void)txt;
    (void)flags;

    norn_bonjour_t *bj = userdata;
    if (event == AVAHI_RESOLVER_FOUND && bj && bj->client) {
        uint32_t ip = 0;
        if (address->proto == AVAHI_PROTO_INET) {
            ip = address->data.ipv4.address;
        }
        if (ip != 0 && port != 0) {
            mainline_add_bootstrap(&bj->client->ml, ip, port);
        }
    }
    if (r) avahi_service_resolver_free(r);
}

static void browse_callback(AvahiServiceBrowser *b,
                            AvahiIfIndex interface,
                            AvahiProtocol protocol,
                            AvahiBrowserEvent event,
                            const char *name,
                            const char *type,
                            const char *domain,
                            AvahiLookupResultFlags flags,
                            void *userdata) {
    (void)b;
    (void)interface;
    (void)protocol;
    (void)flags;

    norn_bonjour_t *bj = userdata;
    if (!bj) return;

    switch (event) {
    case AVAHI_BROWSER_NEW:
        avahi_service_resolver_new(bj->avahi_client, interface, protocol,
                                   name, type, domain,
                                   AVAHI_PROTO_UNSPEC, (AvahiLookupFlags)0,
                                   resolve_callback, bj);
        break;
    case AVAHI_BROWSER_REMOVE:
    case AVAHI_BROWSER_CACHE_EXHAUSTED:
    case AVAHI_BROWSER_ALL_FOR_NOW:
    case AVAHI_BROWSER_FAILURE:
        break;
    }
}

static void entry_group_callback(AvahiEntryGroup *g,
                                 AvahiEntryGroupState state,
                                 void *userdata) {
    (void)g;
    (void)userdata;
    if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
        fprintf(stderr, "bonjour: announced _norn._udp\n");
    }
}

static void client_callback(AvahiClient *c,
                            AvahiClientState state,
                            void *userdata) {
    norn_bonjour_t *bj = userdata;
    if (!bj) return;

    switch (state) {
    case AVAHI_CLIENT_S_RUNNING: {
        if (!bj->entry_group) {
            bj->entry_group = avahi_entry_group_new(c, entry_group_callback, bj);
        }
        if (bj->entry_group) {
            AvahiStringList *txt = avahi_string_list_new(
                "proto=norn-dht",
                NULL);
            avahi_entry_group_add_service(
                bj->entry_group,
                AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, (AvahiPublishFlags)0,
                NORN_SERVICE_NAME, NORN_SERVICE_TYPE, NULL, NULL,
                bj->port, txt);
            avahi_entry_group_commit(bj->entry_group);
            avahi_string_list_free(txt);
        }

        if (!bj->browser) {
            bj->browser = avahi_service_browser_new(
                c, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
                NORN_SERVICE_TYPE, NULL, (AvahiLookupFlags)0,
                browse_callback, bj);
        }
        break;
    }
    case AVAHI_CLIENT_FAILURE:
        fprintf(stderr, "bonjour: client failure: %s\n",
                avahi_strerror(avahi_client_errno(c)));
        break;
    case AVAHI_CLIENT_S_COLLISION:
    case AVAHI_CLIENT_S_REGISTERING:
        if (bj->entry_group)
            avahi_entry_group_reset(bj->entry_group);
        break;
    case AVAHI_CLIENT_CONNECTING:
        break;
    }
}

norn_bonjour_t *norn_bonjour_new(struct norn_client *client,
                                  uint16_t port,
                                  const unsigned char pubkey[32]) {
    if (!client) return NULL;

    norn_bonjour_t *bj = calloc(1, sizeof(*bj));
    if (!bj) return NULL;

    bj->client = client;
    bj->port = port;
    if (pubkey) memcpy(bj->pubkey, pubkey, 32);

    bj->threaded_poll = avahi_threaded_poll_new();
    if (!bj->threaded_poll) {
        free(bj);
        return NULL;
    }

    int error;
    bj->avahi_client = avahi_client_new(avahi_threaded_poll_get(bj->threaded_poll),
                                        (AvahiClientFlags)0,
                                        client_callback, bj, &error);
    if (!bj->avahi_client) {
        fprintf(stderr, "bonjour: failed to create avahi client: %s\n",
                avahi_strerror(error));
        avahi_threaded_poll_free(bj->threaded_poll);
        free(bj);
        return NULL;
    }

    if (avahi_threaded_poll_start(bj->threaded_poll) < 0) {
        fprintf(stderr, "bonjour: failed to start threaded poll\n");
        avahi_client_free(bj->avahi_client);
        avahi_threaded_poll_free(bj->threaded_poll);
        free(bj);
        return NULL;
    }

    bj->running = 1;
    return bj;
}

void norn_bonjour_free(norn_bonjour_t *bj) {
    if (!bj) return;

    bj->running = 0;

    if (bj->threaded_poll)
        avahi_threaded_poll_stop(bj->threaded_poll);

    if (bj->browser) {
        avahi_service_browser_free(bj->browser);
        bj->browser = NULL;
    }
    if (bj->entry_group) {
        avahi_entry_group_free(bj->entry_group);
        bj->entry_group = NULL;
    }
    if (bj->avahi_client) {
        avahi_client_free(bj->avahi_client);
        bj->avahi_client = NULL;
    }
    if (bj->threaded_poll) {
        avahi_threaded_poll_free(bj->threaded_poll);
        bj->threaded_poll = NULL;
    }

    free(bj);
}

#else /* !HAVE_AVAHI */

/* Placeholder so the type is complete when Avahi is unavailable; the member is
 * intentionally unused. */
/* cppcheck-suppress unusedStructMember */
struct norn_bonjour { int dummy; };

norn_bonjour_t *norn_bonjour_new(struct norn_client *client,
                                  uint16_t port,
                                  const unsigned char pubkey[32]) {
    (void)client;
    (void)port;
    (void)pubkey;
    return NULL;
}

void norn_bonjour_free(norn_bonjour_t *bj) {
    (void)bj;
}

#endif /* HAVE_AVAHI */
