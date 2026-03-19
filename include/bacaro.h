#pragma once

#include <stdint.h>
#include <stddef.h>

#define BACARO_VERSION_MAJOR 0
#define BACARO_VERSION_MINOR 1
#define BACARO_VERSION_PATCH 0
#define BACARO_VERSION_STRING "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bacaro_s bacaro_t;

typedef void (*bacaro_fn)(bacaro_t *self,
                         const char *path,
                         const uint8_t *data, size_t len,
                         void *userdata);

/* Lifecycle */
bacaro_t *bacaro_new    (const char *name);
void     bacaro_destroy(bacaro_t **self);

/* Subscriptions */
int bacaro_subscribe     (bacaro_t *self, const char *domain);
int bacaro_subscribe_all (bacaro_t *self);
int bacaro_unsubscribe   (bacaro_t *self, const char *domain);

/* Publishing — broadcasts value to all subscribers and stores in local cache.
   msgpack_value must be a valid msgpack-encoded byte sequence. */
int bacaro_set (bacaro_t *self, const char *path,
               const uint8_t *msgpack_value, size_t len);

/* Reading — immediate cache lookup. *out points into internal storage;
   valid until the next bacaro_set or bacaro_dispatch on the same path.
   Do NOT call free() on *out — it is not caller-allocated.
   Returns BACARO_ENOTFOUND if the property is not cached. */
int bacaro_get (bacaro_t *self, const char *path,
               const uint8_t **out, size_t *out_len);

/* Returns the name of the last process to publish a cached property,
   or NULL if not found. Pointer valid until next set/dispatch on that path.
   Do NOT call free() on the returned pointer. */
const char *bacaro_get_publisher(bacaro_t *self, const char *path);

/* Batch read — returns all cached properties whose path starts with
   domain_prefix (empty string = all). Returns NULL on error.
   Caller must free with bacaro_proplist_destroy(). */
typedef struct bacaro_proplist_s bacaro_proplist_t;

bacaro_proplist_t *bacaro_get_domain      (bacaro_t *self, const char *domain_prefix);
void              bacaro_proplist_destroy(bacaro_proplist_t **list);

size_t         bacaro_proplist_size     (const bacaro_proplist_t *list);
const char    *bacaro_proplist_path     (const bacaro_proplist_t *list, size_t idx);
const uint8_t *bacaro_proplist_value    (const bacaro_proplist_t *list, size_t idx, size_t *len);
const char    *bacaro_proplist_publisher(const bacaro_proplist_t *list, size_t idx);
uint64_t       bacaro_proplist_sequence (const bacaro_proplist_t *list, size_t idx);
uint64_t       bacaro_proplist_timestamp(const bacaro_proplist_t *list, size_t idx);

/* Event loop integration.
   bacaro_fd() returns an epoll fd — add it to your own event loop.
   Call bacaro_dispatch() whenever the fd is readable, or periodically. */
int bacaro_fd      (bacaro_t *self);
int bacaro_dispatch(bacaro_t *self);

/* Register a callback invoked on every incoming property update. */
void bacaro_on_update(bacaro_t *self, bacaro_fn callback, void *userdata);

/* Error codes */
#define BACARO_OK        0
#define BACARO_ENOTFOUND -1
#define BACARO_EINVAL    -2
#define BACARO_EZMQ      -3

#ifdef __cplusplus
}
#endif
