/* CSC 541, Project 2: bounded circular buffer.
 * Team: Shazan Ansar Mohammed and Saina Zardosht.
 * Based on the instructor's buffer_incomplete.c; see genai.txt for assistance.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 5
#define MAX_SLEEP 5
#define MIN 0
#define MAX 1000
#define MAX_THREADS 128

static int buffer[BUFFER_SIZE];
static int insertPointer, removePointer, count;
static unsigned long produced, consumed;
static pthread_mutex_t mutex;
static sem_t *empty, *full;
#if !defined(__APPLE__) && !defined(USE_NAMED_SEMAPHORES)
static sem_t emptyStorage, fullStorage;
#endif
static atomic_int stopping = 0;
static struct timespec startTime;

typedef struct {
    int id;                    /* Stable ID within producer/consumer role. */
    uint32_t seed;              /* Private random state: no shared rand(). */
    unsigned long operation;
    double attempted;
} Worker;

/* Keep the starter's insert_item(int) and remove_item(void) interfaces. */
static _Thread_local Worker *currentWorker;

static void check_thread(int error, const char *operation)
{
    if (error != 0) {
        fprintf(stderr, "%s: %s\n", operation, strerror(error));
        exit(EXIT_FAILURE);
    }
}

static void post_sem(sem_t *sem)
{
    if (sem_post(sem) == -1) {
        perror("sem_post");
        exit(EXIT_FAILURE);
    }
}

static void wait_sem(sem_t *sem)
{
    while (sem_wait(sem) == -1) {
        if (errno != EINTR) {
            perror("sem_wait");
            exit(EXIT_FAILURE);
        }
    }
}

static double elapsed(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (double)(now.tv_sec - startTime.tv_sec)
         + (double)(now.tv_nsec - startTime.tv_nsec) / 1000000000.0;
}

static uint32_t next_random(Worker *worker)
{
    uint32_t x = worker->seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    worker->seed = x;
    return x;
}

/* Small sleep slices let sleeping workers notice the stop request promptly. */
static void worker_sleep(unsigned int seconds)
{
    for (unsigned int i = 0; i < seconds * 10 && !atomic_load(&stopping); ++i) {
        struct timespec delay = {0, 100000000L};
        while (nanosleep(&delay, &delay) == -1 && errno == EINTR) {
            if (atomic_load(&stopping))
                return;
        }
    }
}

static void print_buffer(void)
{
    /* The caller holds mutex, so every snapshot matches its operation. */
    for (int i = 0; i < BUFFER_SIZE; ++i)
        printf("slot %d: %d\n", i, buffer[i]);
    printf("Buffer count: %d/%d\n\n", count, BUFFER_SIZE);
}

/* Return 0 on success, 1 when the run ends before this insertion. */
int insert_item(int item)
{
    Worker *worker = currentWorker;
    wait_sem(empty);                     /* Reserve space BEFORE locking. */
    check_thread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    if (atomic_load(&stopping)) {
        printf("Producer %d operation %lu stopped before insertion at time %.6f s\n",
               worker->id, worker->operation, elapsed());
        check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
        return 1;
    }

    buffer[insertPointer] = item;
    insertPointer = (insertPointer + 1) % BUFFER_SIZE;
    ++count;
    ++produced;
    double completed = elapsed();
    printf("Producer %d produced %d at time %.6f s (operation %lu, attempted %.6f s, waited %.6f s)\n",
           worker->id, item, completed, worker->operation,
           worker->attempted, completed - worker->attempted);
    print_buffer();
    check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
    post_sem(full);                      /* Publish one available item. */
    return 0;
}

/* Return status, not the removed value: item 0 is a valid buffer item. */
int remove_item(void)
{
    Worker *worker = currentWorker;
    wait_sem(full);                      /* Reserve an item BEFORE locking. */
    check_thread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    if (atomic_load(&stopping)) {
        printf("Consumer %d operation %lu stopped before removal at time %.6f s\n",
               worker->id, worker->operation, elapsed());
        check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
        return 1;
    }

    int item = buffer[removePointer];
    buffer[removePointer] = -1;
    removePointer = (removePointer + 1) % BUFFER_SIZE;
    --count;
    ++consumed;
    double completed = elapsed();
    printf("Consumer %d consumed %d at time %.6f s (operation %lu, attempted %.6f s, waited %.6f s)\n",
           worker->id, item, completed, worker->operation,
           worker->attempted, completed - worker->attempted);
    print_buffer();
    check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
    post_sem(empty);                     /* Publish one available slot. */
    return 0;
}

static void *producer(void *param)
{
    Worker *worker = param;
    currentWorker = worker;
    while (!atomic_load(&stopping)) {
        worker_sleep(next_random(worker) % MAX_SLEEP);
        int item = (int)(next_random(worker) % (MAX - MIN + 1)) + MIN;
        check_thread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
        if (atomic_load(&stopping)) {
            check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
            break;
        }
        ++worker->operation;
        worker->attempted = elapsed();
        printf("Producer %d tries to insert %d at time %.6f s (operation %lu)\n",
               worker->id, item, worker->attempted, worker->operation);
        check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
        if (insert_item(item))
            break;
    }
    return NULL;
}

static void *consumer(void *param)
{
    Worker *worker = param;
    currentWorker = worker;
    while (!atomic_load(&stopping)) {
        worker_sleep(next_random(worker) % MAX_SLEEP);
        check_thread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
        if (atomic_load(&stopping)) {
            check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
            break;
        }
        ++worker->operation;
        worker->attempted = elapsed();
        printf("Consumer %d tries to consume at time %.6f s (operation %lu)\n",
               worker->id, worker->attempted, worker->operation);
        check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
        if (remove_item())
            break;
    }
    return NULL;
}

static int parse_number(const char *text, long upper, int *value)
{
    char *end;
    errno = 0;
    if (text[0] < '0' || text[0] > '9')
        return -1;
    long number = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || number < 1 || number > upper)
        return -1;
    *value = (int)number;
    return 0;
}

static void init_semaphores(void)
{
#if defined(__APPLE__) || defined(USE_NAMED_SEMAPHORES)
    /* macOS supports named POSIX semaphores; unlink after opening. */
    char emptyName[32], fullName[32];
    snprintf(emptyName, sizeof emptyName, "/b2e_%ld", (long)getpid());
    snprintf(fullName, sizeof fullName, "/b2f_%ld", (long)getpid());
    empty = sem_open(emptyName, O_CREAT | O_EXCL, 0600, BUFFER_SIZE);
    if (empty == SEM_FAILED) {
        perror("sem_open empty");
        exit(EXIT_FAILURE);
    }
    if (sem_unlink(emptyName) == -1) {
        perror("sem_unlink empty");
        exit(EXIT_FAILURE);
    }
    full = sem_open(fullName, O_CREAT | O_EXCL, 0600, 0);
    if (full == SEM_FAILED) {
        perror("sem_open full");
        sem_close(empty);
        exit(EXIT_FAILURE);
    }
    if (sem_unlink(fullName) == -1) {
        perror("sem_unlink full");
        exit(EXIT_FAILURE);
    }
#else
    empty = &emptyStorage;
    full = &fullStorage;
    if (sem_init(empty, 0, BUFFER_SIZE) == -1) {
        perror("sem_init empty");
        exit(EXIT_FAILURE);
    }
    if (sem_init(full, 0, 0) == -1) {
        perror("sem_init full");
        sem_destroy(empty);
        exit(EXIT_FAILURE);
    }
#endif
}

int main(int argc, char *argv[])
{
    int duration, producerThreads, consumerThreads;
    if (argc != 4 || parse_number(argv[1], 86400, &duration)
        || parse_number(argv[2], MAX_THREADS, &producerThreads)
        || parse_number(argv[3], MAX_THREADS, &consumerThreads)) {
        fprintf(stderr, "Usage: %s <seconds 1..86400> <producers 1..128> <consumers 1..128>\n", argv[0]);
        return EXIT_FAILURE;
    }

    for (int i = 0; i < BUFFER_SIZE; ++i)
        buffer[i] = -1;
    check_thread(pthread_mutex_init(&mutex, NULL), "pthread_mutex_init");
    init_semaphores();
    if (clock_gettime(CLOCK_MONOTONIC, &startTime) == -1) {
        perror("clock_gettime");
        return EXIT_FAILURE;
    }
    /* Flush each line even when ./a.out 20 5 5 > output.txt is used. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("Bounded buffer: capacity=%d, runtime=%d s, producers=%d, consumers=%d\n",
           BUFFER_SIZE, duration, producerThreads, consumerThreads);
    printf("Synchronization initialized: mutex=0, empty=%d, full=0\n", BUFFER_SIZE);
    printf("Times are elapsed seconds since program start.\n");

    pthread_t threads[2 * MAX_THREADS];
    Worker workers[2 * MAX_THREADS];
    int total = producerThreads + consumerThreads;
    uint32_t base = (uint32_t)time(NULL) ^ (uint32_t)getpid();
    for (int i = 0; i < total; ++i) {
        int isProducer = i < producerThreads;
        workers[i].id = isProducer ? i + 1 : i - producerThreads + 1;
        workers[i].operation = 0;
        workers[i].seed = base ^ (0x9e3779b9u * (uint32_t)(i + 1));
        if (workers[i].seed == 0)
            workers[i].seed = 1;
        check_thread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
        printf("Created %s %d\n", isProducer ? "Producer" : "Consumer", workers[i].id);
        check_thread(pthread_create(&threads[i], NULL,
                     isProducer ? producer : consumer, &workers[i]), "pthread_create");
        check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");
    }

    while (elapsed() < duration) {
        struct timespec delay = {0, 10000000L};
        while (nanosleep(&delay, &delay) == -1 && errno == EINTR)
            ;
    }
    check_thread(pthread_mutex_lock(&mutex), "pthread_mutex_lock");
    atomic_store(&stopping, 1);
    printf("Stopping at time %.6f s; pending attempts will be stopped.\n", elapsed());
    check_thread(pthread_mutex_unlock(&mutex), "pthread_mutex_unlock");

    /* Wake every possible waiter. These are shutdown signals, not items.
     * Each worker checks stopping under mutex before accessing the buffer.
     */
    for (int i = 0; i < producerThreads; ++i)
        post_sem(empty);
    for (int i = 0; i < consumerThreads; ++i)
        post_sem(full);
    for (int i = 0; i < total; ++i)
        check_thread(pthread_join(threads[i], NULL), "pthread_join");

    printf("Summary: produced=%lu consumed=%lu remaining=%d\n", produced, consumed, count);
    print_buffer();
#if defined(__APPLE__) || defined(USE_NAMED_SEMAPHORES)
    if (sem_close(empty) == -1 || sem_close(full) == -1) {
        perror("sem_close");
        return EXIT_FAILURE;
    }
#else
    if (sem_destroy(empty) == -1 || sem_destroy(full) == -1) {
        perror("sem_destroy");
        return EXIT_FAILURE;
    }
#endif
    check_thread(pthread_mutex_destroy(&mutex), "pthread_mutex_destroy");
    puts("All threads joined; synchronization resources released.");
    return EXIT_SUCCESS;
}
