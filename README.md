# Asynchronous Executor in C

## Motivation
Modern computer systems often need to handle multiple tasks simultaneously: managing numerous network connections, processing large amounts of data, or communicating with external devices. Traditional multithreading approaches can lead to significant overhead in thread management and scalability issues in resource-constrained systems.

Asynchronous executors provide a modern solution to this problem. Instead of running each task in a separate thread, the executor manages tasks cooperatively – tasks "yield" the processor to each other when waiting for resources, such as file data, device signals, or database responses. This allows handling thousands or even millions of concurrent operations using a limited number of threads.

## Use Cases
- **HTTP Servers**: Servers like Nginx or Tokio in Rust use asynchronous executors to handle thousands of network connections while minimizing memory and CPU usage.
- **Embedded Systems Programming**: In resource-constrained systems (e.g., microcontrollers), efficient management of multiple tasks, such as reading sensor data or controlling devices, is crucial. Asynchronous execution avoids costly multithreading.
- **Interactive Applications**: In graphical or mobile applications, asynchronicity ensures smooth operation – for example, while fetching data from the network, the user interface remains responsive.
- **Data Processing**: Asynchronous approaches efficiently manage multiple concurrent I/O operations, such as in data analysis systems where disk operations can be a bottleneck.

## Task Objective
In this assignment, you will write a simplified single-threaded asynchronous executor in C (inspired by the Tokio library in Rust). No threads are created: the executor and its tasks all run in the main thread (or yield to others while waiting for resources).

You will receive a comprehensive project skeleton and will need to implement key elements. The result will be a simple but functional asynchronous library. Examples of its usage and descriptions are in the source files in `tests/` (which also serve as basic example tests).

A key role in the task is played by the `epoll` mechanism, which allows waiting for a selected set of events. This mainly involves listening for the availability of descriptors (e.g., pipes or network sockets) for reading and writing. See:

- `epoll` in 3 easy steps
- `man epoll`, specifically:
    - `epoll_create1(0)` (i.e., `epoll_create` without a suggested size, no special flags);
    - `epoll_ctl(...)` only on pipe descriptors, only on events like `EPOLLIN`/`EPOLLOUT` (read/write readiness), no special flags (specifically in default level-triggered mode, i.e., without `EPOLLET` flag, not edge-triggered);
    - `epoll_wait(...)`;
    - `close()` to close the created `epoll_fd`.

## Specification
### Structures
The main structures needed for implementation are:

- **Executor**: Responsible for executing tasks on the processor – implements the main event loop `executor_run()` and contains a task queue (in general, it could contain multiple queues and threads for parallel processing);
- **Mio**: Responsible for communication with the operating system – implements waiting for data availability, i.e., calls `epoll_*` functions.
- **Future**: In this assignment, it is not only a place for a value to wait for. Future will also contain a coroutine, i.e., information about:
    - what task to perform (as a function pointer `progress`),
    - state to maintain between task stages (if any).
    In C, there is no inheritance, so instead of subclasses of `Future`, we will have `FutureFoo` structures containing `Future base` as the first field and casting `Future*` to `FutureFoo*`.
- **Waker**: A callback (here: a function pointer with arguments) defining how to notify the executor that a task can proceed (e.g., a task was waiting for data to read, and it became available).

In this assignment, you are given a skeleton implementation (header files and some structures) and example futures. You need to implement the executor, Mio, and other futures (details below).

### Operation Scheme
A task (Future) can be in one of four states:

- **COMPLETED**: The task has finished and has a result.
- **FAILURE**: The task has finished with an error or was interrupted.
- **PENDING (queued)**: Can proceed, i.e., is being executed by the executor or waiting for processor allocation in the executor queue.
- **PENDING (waker waiting)**: The task could not proceed in the executor, and someone (e.g., Mio or a helper thread) holds the Waker, which will be called when this changes.

The state diagram looks like this:

```
executor_spawn                     COMPLETED / FAILURE
         │                                       ▲
         ▼               executor calls           │
     PENDING  ───► fut->progress(fut, waker) ──+
(queued)                                      │
         ▲                                       │
         │                                       ▼
         └─── someone (e.g., mio_poll) calls ◄──── PENDING
                            waker_wake(waker)       (waker waiting)
```

(In the code, we will treat PENDING as one state, without distinguishing who holds the Future pointer.)

More detailed operation scheme:

1. The program creates an executor (which creates an instance of Mio for itself).
2. It adds tasks to the executor (`executor_spawn`).
3. Calls `executor_run`, which will execute tasks, which will delegate subtasks, until the program ends.
4. In this assignment, the executor does not create threads: `executor_run()` works in the thread it was called in.
5. The executor processes tasks in a loop:
     - If there are no more unfinished (PENDING) tasks, it ends the loop.
     - If there are no active tasks, it calls `mio_poll()` to put the executor thread to sleep until this changes.
     - For each active task future, it calls `future.progress(future, waker)` (creating a Waker that adds the task back to the queue if needed).

### What `future.progress()` Can Do
Low-level tasks within their `progress()` can call, for example:

- `mio_register(..., waker)`, to return PENDING and be called again by the executor when something becomes ready, then proceed to the next stage of calculations;
- `waker_wake(waker)`, to directly ask the executor to immediately queue – this makes sense if you want to return PENDING and allow other tasks to execute before proceeding to the next stage of longer calculations (analogous to `Thread.yield()`);
- `other_future->progress(other_future, waker)`, to try to execute a subtask within its processor allocation;
- `executor_spawn`, to delegate a subtask to the executor, which can execute independently.

### Mio
Mio allows:

- In `mio_register`: Registering that a given waker should be called when the appropriate event occurs; these events are the readiness of a descriptor for reading or writing (in this assignment). Mio ensures that it calls the waker only when an operation can be performed on the resource (descriptor) in a non-blocking manner.
- In `mio_poll`: Putting the calling thread to sleep until at least one of the registered events occurs and calling the appropriate waker for them.

### Future to Implement: Combinators
The last part of this assignment is to implement three combinators, i.e., Futures that combine other Futures:

- **ThenFuture**: Created from `fut1` and `fut2`, executes `fut1`, then `fut2`, passing the result of `fut1` as an input argument to `fut2`.
- **JoinFuture**: Executes `fut1` and `fut2` concurrently, finishing when both complete.
- **SelectFuture**: Executes `fut1` and `fut2` concurrently, finishing when either successfully completes; if, for example, `fut1` successfully completes (with COMPLETED state), the other is abandoned, i.e., `fut2.progress()` will not be called further, even if the last state returned by `fut2.progress()` was PENDING.

The detailed interface and behavior (including error handling) are specified in `future_combinators.h`.

## Formal Requirements
### Data
NOTE: Modifying data files is prohibited, except for those listed as to be completed by the student.

The data includes header files:

- `executor.h` - asynchronous executor,
- `future.h` - asynchronous computation/task (coroutine),
- `waker.h` - mechanism for waking up an asynchronous task after the expected event occurs,
- `mio.h` - abstraction layer over the `epoll` system call, used to register interest in I/O operations availability,
- `future_combinators.h` - operators logically linking the execution of two asynchronous computations, e.g., by succession (Then) or concurrent execution (Join).

The data includes source files to be completed by the student: `executor.c`, `mio.c`, `future_combinators.c`.

Additionally, the headers:

- `err.h` - a small library for convenient error handling known from labs.
- `debug.h` - simple `debug()` macro for writing to standard output, enabled by the `DEBUG_PRINTS` flag.

Other files: `future_examples.{h,c}` and the contents of the `tests` subdirectory are for testing the library and illustrating its functionality.

### Evaluation
Three components are to be completed, in order: `executor.c`, `mio.c`, and (the largest) `future_combinators.c`.

Components will be tested individually (unit tests) and integratively (as a whole). Points will be awarded for each component individually and for the overall functionality.

### Guarantees
In this assignment, no threads will be created; everything happens in one main thread (specifically `executor_run`, `future.progress`, `waker_wake`).
- `executor_run` and `executor_destroy` will be called exactly once for each `executor_create`.
- `executor_destroy` will always be called after the corresponding `executor_run` has been called and completed.
- `executor_spawn` calls can occur both outside the context of `executor_run` and during the execution of `executor_run`. Both cases must be handled correctly.
- It can be assumed that the number of Futures in the PENDING state will not exceed `max_queue_size` provided to `executor_create`.

### Requirements
Only the three files to be completed by the student will be extracted from the solution; new and modified files (especially changes to `CMakeLists.txt`) will be ignored.
Active or semi-active waiting cannot be used.
Thus, in the completed solution files, do not use any sleep functions (sleep, usleep, nanosleep) or provide timeouts (e.g., to `epoll_wait`).
Solutions will be tested for memory and/or other resource leaks (unclosed files, etc.).
Implementations should be reasonably efficient, i.e., not exposed to extreme load (e.g., handling hundreds of thousands of computations) should not add overhead of tens of milliseconds (or more) beyond the expected execution time (resulting from the cost of computation or waiting on a descriptor).
We require the use of the C language in the gnu11 version (or c11).
You can use the standard C library (libc) and system-provided functionality (declared in `unistd.h`, etc.).
Using other external libraries is not allowed.
You can borrow any code from the labs. Any other code borrowings should be appropriately commented with the source provided.

### Build commands
- Preparing the build: `mkdir build; cd build; cmake ..`
- Building tests: `cd build/tests; cmake --build ..`
- Running tests collectively: `cd build/tests; ctest --output-on-failure`
- Individually: `./<test_name>`
- Valgrind: especially flags `--track-origins=yes --track-fds=yes`
- ASAN (Address Sanitizer): run your program after compiling with the `-fsanitize=address` flag to gcc (default set in the attached `CMakeLists.txt`).
