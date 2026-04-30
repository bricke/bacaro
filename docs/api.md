# Bacaro — API Reference

The full public API is declared in [`include/bacaro.h`](../include/bacaro.h).

## Lifecycle

```c
bacaro_t *bacaro_new    (const char *name, const char **published_domains);
void      bacaro_destroy(bacaro_t **self);
```

`bacaro_new` creates a new instance with the given process name, binds its sockets, and starts peer discovery. Returns `NULL` on failure.

`published_domains` is an optional NULL-terminated array of domain strings that this process will publish. When provided, a `.manifest` file is written alongside the `.pub` and `.rep` files so that peers can skip snapshot requests for non-overlapping domains. Pass `NULL` if you don't want to declare a manifest — the bus works identically without one.

```c
// Declare published domains (optional optimisation)
const char *domains[] = {"sensors", "power", NULL};
bacaro_t *b = bacaro_new("powerd", domains);

// No manifest — backwards compatible
bacaro_t *b = bacaro_new("monitor", NULL);
```

`bacaro_destroy` disconnects all peers, removes IPC files (including the `.manifest` if one was written), and frees all resources. Sets `*self` to `NULL`.

## Subscriptions

```c
int bacaro_subscribe    (bacaro_t *self, const char *domain);
int bacaro_subscribe_all(bacaro_t *self);
int bacaro_unsubscribe  (bacaro_t *self, const char *domain);
```

`bacaro_subscribe` adds a domain prefix filter. All existing peer connections are updated immediately and a snapshot is requested for the new domain.

`bacaro_subscribe_all` is equivalent to `bacaro_subscribe(self, "")` — receives every property on the bus.

`bacaro_unsubscribe` removes the filter from all peer connections.

## Publishing

```c
int bacaro_set(bacaro_t *self, const char *path,
               const uint8_t *msgpack_value, size_t len);
```

Publishes a property update. `msgpack_value` must be a valid MessagePack-encoded byte sequence. Updates the local cache immediately, then broadcasts to all subscribers.

## Reading

```c
int        bacaro_get          (bacaro_t *self, const char *path,
                                const uint8_t **out, size_t *out_len);
const char *bacaro_get_publisher(bacaro_t *self, const char *path);
```

Both functions perform an immediate local cache lookup — no network call.

`bacaro_get` returns `BACARO_OK` and sets `*out` / `*out_len` on success, or `BACARO_ENOTFOUND` if the property is not cached.

`bacaro_get_publisher` returns the name of the last process to publish the property, or `NULL` if not found.

### Memory ownership

- `*out` from `bacaro_get` points **into internal cache storage**. Valid until the next `bacaro_set` or `bacaro_dispatch` on the same path. **Do not call `free()` on it.**
- The pointer from `bacaro_get_publisher` follows the same rules. **Do not free.**

## Batch read

```c
bacaro_proplist_t *bacaro_get_domain      (bacaro_t *self, const char *domain_prefix);
void              bacaro_proplist_destroy(bacaro_proplist_t **list);

size_t         bacaro_proplist_size     (const bacaro_proplist_t *list);
const char    *bacaro_proplist_path     (const bacaro_proplist_t *list, size_t idx);
const uint8_t *bacaro_proplist_value    (const bacaro_proplist_t *list, size_t idx, size_t *len);
const char    *bacaro_proplist_publisher(const bacaro_proplist_t *list, size_t idx);
uint64_t       bacaro_proplist_sequence (const bacaro_proplist_t *list, size_t idx);
uint64_t       bacaro_proplist_timestamp(const bacaro_proplist_t *list, size_t idx);
```

`bacaro_get_domain` returns a snapshot of all cached properties whose path starts with `domain_prefix`. An empty prefix returns everything. Returns `NULL` on error.

The caller **must** free the result with `bacaro_proplist_destroy()`.

All accessor pointers are valid for the lifetime of the `bacaro_proplist_t`.

## Event loop integration

```c
int  bacaro_fd      (bacaro_t *self);
int  bacaro_dispatch(bacaro_t *self);
void bacaro_on_update(bacaro_t *self, bacaro_fn callback, void *userdata);

typedef void (*bacaro_fn)(bacaro_t *self, const char *path,
                          const uint8_t *data, size_t len, void *userdata);
```

`bacaro_fd` returns an epoll fd that becomes readable when there is activity (new peers, incoming messages, snapshot replies). Add it to your own event loop.

`bacaro_dispatch` processes all pending events: peer discovery, snapshot requests/replies, and live messages. Call it when the epoll fd is readable, or periodically if you prefer a polling approach.

`bacaro_on_update` registers a callback fired on every incoming property update (both from live PUB/SUB and from snapshot replies).

## Error codes

| Code | Value | Meaning |
|------|-------|---------|
| `BACARO_OK`        |  0 | Success |
| `BACARO_ENOTFOUND` | -1 | Property not in local cache |
| `BACARO_EINVAL`    | -2 | Invalid argument (null pointer, bad format) |
| `BACARO_EZMQ`      | -3 | ZeroMQ error |

## Usage example

```c
#include <bacaro.h>
#include <msgpack.hpp>

// Publisher
const char *domains[] = {"system", "power", nullptr};
bacaro_t *pub = bacaro_new("powerd", domains);

msgpack::sbuffer buf;
msgpack::pack(buf, 87.3);
bacaro_set(pub, "system.battery.level",
           (const uint8_t *)buf.data(), buf.size());

bacaro_destroy(&pub);

// Subscriber with callback
static void on_update(bacaro_t *self, const char *path,
                      const uint8_t *data, size_t len, void *ud)
{
    // called on every received update
}

bacaro_t *sub = bacaro_new("monitor", nullptr);
bacaro_subscribe(sub, "system");
bacaro_on_update(sub, on_update, NULL);

// Integrate with your event loop
int fd = bacaro_fd(sub);
// ... add fd to epoll/poll/select, then on activity:
bacaro_dispatch(sub);

// Or poll manually
while (running) {
    bacaro_dispatch(sub);
    usleep(1000);
}

// Read from cache at any time
const uint8_t *value;
size_t len;
if (bacaro_get(sub, "system.battery.level", &value, &len) == BACARO_OK) {
    double level = msgpack::unpack((const char *)value, len).get().as<double>();
}

// Batch read
bacaro_proplist_t *list = bacaro_get_domain(sub, "system");
for (size_t i = 0; i < bacaro_proplist_size(list); ++i) {
    printf("%s published by %s\n",
           bacaro_proplist_path(list, i),
           bacaro_proplist_publisher(list, i));
}
bacaro_proplist_destroy(&list);

bacaro_destroy(&sub);
```
