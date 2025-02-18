#include "executor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "future.h"
#include "mio.h"
#include "waker.h"

/**
 * @brief Structure to represent the current-thread executor.
 */

/* FutQue: A queue to store futures 
 * A full implementation would include functions to push and pop futures.
 */
typedef struct FutQue FutQue;

struct FutQue {
    Future** futs;
    size_t size;
    size_t max_size;
    int front;
    int back;
};

FutQue* futque_create(size_t max_size) {
    FutQue* que = (FutQue*)malloc(sizeof(FutQue));
    que->futs = (Future**)malloc(max_size * sizeof(Future*));
    que->size = 0;
    que->max_size = max_size;
    que->front = 0;
    que->back = -1;
    return que;
}

bool isEmpty(FutQue* que) {
    return que->size == 0;
}

void push(FutQue* que, Future* fut) {
    if (que->size == que->max_size) {
        return;
    }
    que->back = (que->back + 1) % que->max_size;
    que->futs[que->back] = fut;
    que->size++;
}

Future* pop(FutQue* que) {
    if (isEmpty(que)) {
        return NULL;
    }
    Future* fut = que->futs[que->front];
    que->front = (que->front + 1) % que->max_size;
    que->size--;
    return fut;
}

void futque_destroy(FutQue* que) {
    free(que->futs);
    free(que);
}

struct Executor {
    Mio* mio;
    FutQue* que;
};

Executor* executor_create(size_t max_queue_size) {
    debug("Creating Executor\n");

    Executor* executor = (Executor*)malloc(sizeof(Executor));
    executor->que = futque_create(max_queue_size);
    executor->mio = mio_create(executor);
    return executor;
}

/* waker_wake: Wake up the future
 * This function will wake up the future by spawning it.
 */
void waker_wake(Waker* waker) {
    debug("Waking up the future\n");

    executor_spawn((Executor*)waker->executor, waker->future);
}

/* executor_spawn: Spawn a future
 * This function will spawn a future by pushing it to the executor's queue.
 */
void executor_spawn(Executor* executor, Future* fut) {
    debug("Spawning a future\n");

    fut->is_active = true;
    push(executor->que, fut);
}

/* executor_run: Run the executor until all futures are completed
 * This function will run the executor until all futures are completed.
 * It will poll for events using mio_poll.
 */
void executor_run(Executor* executor) {
    debug("Running the executor\n");

    while(!isEmpty(executor->que)){
        while(!isEmpty(executor->que)) {
            Future* fut = pop(executor->que);
            Waker waker = {executor, fut};
            FutureState state = fut->progress(fut, executor->mio, waker);
            if (state == FUTURE_COMPLETED || state == FUTURE_FAILURE)
                fut->is_active = false;
        }
        // Poll for events
        mio_poll(executor->mio);
    }
}

void executor_destroy(Executor* executor) {
    debug("Destroying Executor\n");

    mio_destroy(executor->mio);
    futque_destroy(executor->que);
    free(executor);
}
