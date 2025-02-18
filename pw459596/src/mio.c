#include "mio.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "debug.h"
#include "executor.h"
#include "waker.h"

// Maximum number of events to handle per epoll_wait call.
#define MAX_EVENTS 64

struct Mio {
    Executor* executor;
    int epoll_fd;
    int registered_count;
    struct epoll_event events[MAX_EVENTS];
};

Mio* mio_create(Executor* executor)
{
    debug("Creating Mio\n");

    Mio* mio = malloc(sizeof(Mio));
    if (!mio)
        exit(1);

    mio->executor = executor;
    mio->registered_count = 0;
    mio->epoll_fd = epoll_create1(0);
    if (mio->epoll_fd == -1) {
        perror("epoll_create1");
        free(mio);
        exit(1);
    }
    return mio;
}

void mio_destroy(Mio* mio) {
    debug("Destroying Mio\n");
    
    close(mio->epoll_fd);
    free(mio);
}

int mio_register(Mio* mio, int fd, uint32_t events, Waker waker)
{
    debug("Registering (in Mio = %p) fd = %d", mio, fd);

    struct epoll_event ev = {
        .events = events,
        .data.ptr = waker.future,
    };

    if (epoll_ctl(mio->epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl");
        return -1;
    }
    mio->registered_count++;

    return 0;
}

int mio_unregister(Mio* mio, int fd)
{
    debug("Unregistering (from Mio = %p) fd = %d", mio, fd);

    if (epoll_ctl(mio->epoll_fd, EPOLL_CTL_DEL, fd, NULL) == -1) {
        perror("epoll_ctl");
        return -1;
    }

    mio->registered_count--;

    return 0;
}

/* Poll for events on the registered file descriptors.
 * This function blocks until at least one event is available.
 * When an event is available, the corresponding future is woken up.
 */
void mio_poll(Mio* mio)
{
    debug("Mio (%p) polling\n", mio);

    if (mio->registered_count == 0) {
        debug("No registered events\n");
        return;
    }

    int n = epoll_wait(mio->epoll_fd, mio->events, MAX_EVENTS, -1);
    if (n == -1) {
        perror("epoll_wait");
        mio_destroy(mio);
        exit(1);
    }
    for (int i = 0; i < n; i++) {
        // Wake up the future associated with the event.
        Future* future = (Future*)mio->events[i].data.ptr;
        Waker waker = {mio->executor, future};
        debug_print_waker(&waker);
        waker_wake(&waker);
    }
}