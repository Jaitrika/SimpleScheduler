#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <semaphore.h>
#include <errno.h>

// struct definitions
struct Process
{
    int pid, priority;
    bool submit, in_queue, completed;
    char command[100];
    struct timeval start;
    unsigned long exe_time, wait_time;
};

struct hist
{
    int history_count, ncpu, time_slice;
    sem_t mutex;
    struct Process history[100];
};

struct queue
{
    int head, tail, capacity, curr;
    struct Process **items;
};

struct pqueue
{
    int size, capacity;
    struct Process **min_heap;
};

int shm_fd;
bool ending = false;
struct hist *p_table;
struct queue *running;
struct pqueue *waiting;
bool processes_submitted = false;


// check if queue is empty
bool is_qempty(struct queue *q)
{
    return q->head == q->tail;
}

// check if queue is full
bool q_full(struct queue *q)
{
    return (q->tail + 1) % q->capacity == q->head;
    ;
}

// add a process to the running queue
void running_enqueue(struct queue *q, struct Process *proc)
{
    if (q_full(q))
    {
        printf("rqueue overflow\n");
        return;
    }
    q->items[q->tail] = proc;
    q->tail = (q->tail + 1) % q->capacity;
    q->curr++;
}

// remove a process from the running queue
void running_dequeue(struct queue *q)
{
    if (is_qempty(q))
    {
        printf("rqueue underflow\n");
        return;
    }
    q->head = (q->head + 1) % q->capacity;
    q->curr--;
}

// pqueue methods
// check if priority queue is empty
bool is_pqempty(struct pqueue *pq)
{
    return pq->size == 0;
}

// check if priority queue is full
bool pqueue_full(struct pqueue *pq)
{
    return pq->size == pq->capacity;
}

void swap_p(struct Process *a, struct Process *b)
{
    struct Process temp = *a;
    *a = *b;
    *b = temp;
}

// signal handler
static void sign_handler(int signum)
{

    if (signum == SIGINT)
    {
        ending = true;
    }
    else if (signum == SIGCHLD)
    {
        int saved_errno = errno;
        pid_t pid;
        int status;

        // Reap all terminated children
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            // Optional: debug print
            printf("🔥 SIGCHLD handler reaped PID %d\n", pid);
            fflush(stdout);
        }

        errno = saved_errno;
    }
}

// move a process up the heap to maintain heap property
void heapifyup(struct pqueue *pq, int index)
{
    while (index > 0)
    {
        int parent = (index - 1) / 2;
        if (pq->min_heap[index]->priority < pq->min_heap[parent]->priority)
        {
            swap_p(pq->min_heap[index], pq->min_heap[parent]);
            index = parent;
        }
        else
        {
            break;
        }
    }
}

// move a process down the heap to maintain heap property
void heapifydown(struct pqueue *pq, int index)
{
    int leftChild = 2 * index + 1;
    int rightChild = 2 * index + 2;
    int smallest = index;

    if (leftChild < pq->size && pq->min_heap[leftChild]->priority < pq->min_heap[smallest]->priority)
    {
        smallest = leftChild;
    }

    if (rightChild < pq->size && pq->min_heap[rightChild]->priority < pq->min_heap[smallest]->priority)
    {
        smallest = rightChild;
    }

    if (smallest != index)
    {
        swap_p(pq->min_heap[index], pq->min_heap[smallest]);
        heapifydown(pq, smallest);
    }
}

// add a process to the waiting queue
void waiting_enqueue(struct pqueue *pq, struct Process *proc)
{
    if (pq->size < pq->capacity)
    {
        pq->min_heap[pq->size] = proc;
        heapifyup(pq, pq->size);
        pq->size++;
    }
}

// remove the process with highest priority from waiting queue
struct Process *extract_min(struct pqueue *pq)
{
    if (pq->size > 0)
    {
        struct Process *removed = pq->min_heap[0];
        pq->min_heap[0] = pq->min_heap[pq->size - 1];
        pq->size--;
        heapifydown(pq, 0);
        return removed;
    }
    return NULL;
}

// to record start time
void start_time(struct timeval *start)
{
    gettimeofday(start, 0);
}

// function to calculate elapsed time
unsigned long end_time(struct timeval *start)
{
    struct timeval end;
    gettimeofday(&end, 0);

    unsigned long start_ms = start->tv_sec * 1000 + start->tv_usec / 1000;
    unsigned long end_ms = end.tv_sec * 1000 + end.tv_usec / 1000;

    return end_ms - start_ms;
}

// to terminate schduler
void terminate()
{
    printf("terminating scheduler\n");
    // cleanups for malloc
    free(running->items);
    free(running);
    free(waiting->min_heap);
    free(waiting);
    // destroying the semaphore
    if (sem_destroy(&p_table->mutex) == -1)
    {
        perror("shm_destroy");
        exit(1);
    }
    // unmapping shared memory segment followed by a "close" call
    if (munmap(p_table, sizeof(struct hist)) < 0)
    {
        printf("Error unmapping\n");
        perror("munmap");
        exit(1);
    }
    if (close(shm_fd) == -1)
    {
        perror("close");
        exit(1);
    }
    exit(0);
}

struct Process *running_remove_pid(struct queue *q, pid_t pid)
{
    int size = q->curr;
    struct Process *found = NULL;

    for (int i = 0; i < size; i++)
    {
        int index = (q->head + i) % q->capacity;
        struct Process *proc = q->items[index];

        if (proc->pid == pid && found == NULL)
        {
            found = proc;
            continue; // Skip adding this back
        }

        // Move everything forward, preserving order but skipping 'found'
        int new_index = (q->head + i - (found != NULL ? 1 : 0) + q->capacity) % q->capacity;
        q->items[new_index] = proc;
    }

    if (found != NULL)
    {
        q->curr--;
        q->tail = (q->tail - 1 + q->capacity) % q->capacity;
    }

    return found;
}

void scheduler(int ncpu, int time_slice)
{
    while (true)
    {
        if (sem_wait(&p_table->mutex) == -1)
        {
            perror("sem_wait");
            exit(1);
        }

        if (ending && is_qempty(running) && is_pqempty(waiting))
        {
            terminate();
        }

        // Check for completed processes FIRST - before any other operations
        int status;
        pid_t pid;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
        {
            printf("WAITPID saw PID %d exiting\n", pid);
            fflush(stdout);

            struct Process *finished = running_remove_pid(running, pid);

            if (!finished)
            {
                for (int i = 0; i < p_table->history_count; i++)
                {
                    if (p_table->history[i].pid == pid)
                    {
                        finished = &p_table->history[i];
                        break;
                    }
                }
            }

            if (finished && !finished->completed)
            {
                finished->completed = true;
                // Calculate final execution time
                if (finished->exe_time == 0)
                    finished->exe_time = time_slice; // Minimum 1 time slice
                
                printf("Process %s with PID %d completedehh\n",
                       finished->command, finished->pid);
                fflush(stdout);
            }
        }

        // Enqueue new submitted processes
        for (int i = 0; i < p_table->history_count; i++)
        {
            if (p_table->history[i].submit && !p_table->history[i].completed && !p_table->history[i].in_queue)
            {
                if (waiting->size + ncpu < waiting->capacity - 1)
                {
                    p_table->history[i].in_queue = true;
                    waiting_enqueue(waiting, &p_table->history[i]);
                }
                else
                {
                    break;
                }
            }
        }

        // Handle time slice expiration for running processes
        if (!is_qempty(running))
        {
            for (int i = 0; i < ncpu && !is_qempty(running); i++)
            {
                struct Process *proc = running->items[running->head];
                
                // Calculate how long this process has been running in current slice
                int current_slice_time = end_time(&proc->start);

                // Check if process is still alive
                int status;
                pid_t result = waitpid(proc->pid, &status, WNOHANG);
                
                if (result == proc->pid)
                {
                    // Process completed during this time slice
                    if (!proc->completed)
                    {
                        proc->completed = true;
                        proc->exe_time += current_slice_time;
                        // Ensure minimum 1 time slice if process finished quickly
                        if (proc->exe_time < time_slice)
                            proc->exe_time = time_slice;
                        printf("Process %s with PID %d completedehh\n",
                               proc->command, proc->pid);
                        fflush(stdout);
                    }
                }
                else if (result == 0 && current_slice_time >= time_slice)
                {
                    // Process still running but time slice expired
                    proc->exe_time += time_slice;
                    
                    // Stop the process and move to waiting queue
                    if (kill(proc->pid, SIGSTOP) == -1)
                    {
                        perror("kill SIGSTOP");
                        // Process might have just finished, check again
                        if (waitpid(proc->pid, &status, WNOHANG) == proc->pid)
                        {
                            proc->completed = true;
                            printf("Process %s with PID %d completedehh\n",
                                   proc->command, proc->pid);
                            fflush(stdout);
                        }
                    }
                    else
                    {
                        // Successfully stopped, add to waiting queue
                        waiting_enqueue(waiting, proc);
                        start_time(&proc->start); // Reset start time for wait calculation
                    }
                    running_dequeue(running);
                }
                else if (result == 0)
                {
                    // Process still running, time slice not expired yet
                    // Just continue to next process, don't dequeue
                    break;
                }
                else if (result == -1)
                {
                    // waitpid error - process might have been reaped already
                    if (errno == ECHILD)
                    {
                        // Process already reaped, mark as completed
                        if (!proc->completed)
                        {
                            proc->completed = true;
                            proc->exe_time += current_slice_time;
                            if (proc->exe_time < time_slice)
                                proc->exe_time = time_slice;
                            printf("Process %s with PID %d completedehh\n",
                                   proc->command, proc->pid);
                            fflush(stdout);
                        }
                    }
                    else
                    {
                        perror("waitpid in time slice handling");
                    }
                    running_dequeue(running);
                }
            }
        }

        // Resume next eligible processes from waiting queue
        if (!is_pqempty(waiting))
        {
            for (int i = 0; i < ncpu && !is_pqempty(waiting); i++)
            {
                struct Process *proc = extract_min(waiting);
                
                // Calculate wait time
                proc->wait_time += end_time(&proc->start);
                start_time(&proc->start); // Start new execution time measurement
                
                // Resume the process
                if (kill(proc->pid, SIGCONT) == -1)
                {
                    perror("kill SIGCONT");
                    // Process might have finished while stopped
                    int status;
                    if (waitpid(proc->pid, &status, WNOHANG) == proc->pid)
                    {
                        proc->completed = true;
                        if (proc->exe_time == 0)
                            proc->exe_time = time_slice;
                        printf("Process %s with PID %d completedehh\n",
                               proc->command, proc->pid);
                        fflush(stdout);
                    }
                }
                else
                {
                    printf("Scheduler: resuming PID %d\n", proc->pid);
                    fflush(stdout);
                    running_enqueue(running, proc);
                }
            }
        }

        if (sem_post(&p_table->mutex) == -1)
        {
            perror("sem_post");
            exit(1);
        }

        // Sleep for a reasonable polling interval
        // But track actual time slice expiration per process
        usleep(50000); // 50ms polling for responsiveness
    }
}

int main()
{
    printf("hey sched here\n");
    fflush(stdout);

    struct sigaction sig;
    memset(&sig, 0, sizeof(sig)); // ✅ correct: no `== 0` check
    sig.sa_handler = sign_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    // Handle SIGINT
    if (sigaction(SIGINT, &sig, NULL) == -1)
    {
        perror("sigaction SIGINT");
        exit(1);
    }

    // Handle SIGCHLD
    if (sigaction(SIGCHLD, &sig, NULL) == -1)
    {
        perror("sigaction SIGCHLD");
        exit(1);
    }

    // accessing the shm in read-write mode
    shm_fd = shm_open("shm", O_RDWR, 0666);
    if (shm_fd == -1)
    {
        perror("shm_open");
        exit(1);
    }
    p_table = mmap(NULL, sizeof(struct hist), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (p_table == MAP_FAILED)
    {
        perror("mmap");
        exit(1);
    }
    int ncpu = p_table->ncpu;
    int time_slice = p_table->time_slice;

    waiting = (struct pqueue *)(malloc(sizeof(struct pqueue)));
    if (waiting == NULL)
    {
        perror("malloc");
        exit(1);
    }
    waiting->size = 0;
    waiting->capacity = 100;
    waiting->min_heap = (struct Process **)malloc(waiting->capacity * sizeof(struct Process));
    if (waiting->min_heap == NULL)
    {
        perror("malloc");
        exit(1);
    }
    for (int i = 0; i < waiting->capacity; i++)
    {
        waiting->min_heap[i] = (struct Process *)malloc(sizeof(struct Process));
        if (waiting->min_heap[i] == NULL)
        {
            perror("malloc");
            exit(1);
        }
    }

    running = (struct queue *)(malloc(sizeof(struct queue)));
    if (running == NULL)
    {
        perror("malloc");
        exit(1);
    }
    running->head = running->tail = running->curr = 0;
    running->capacity = ncpu + 1;
    running->items = (struct Process **)malloc(running->capacity * sizeof(struct Process));
    if (running->items == NULL)
    {
        perror("malloc");
        exit(1);
    }
    for (int i = 0; i < running->capacity; i++)
    {
        running->items[i] = (struct Process *)malloc(sizeof(struct Process));
        if (running->items[i] == NULL)
        {
            perror("malloc");
            exit(1);
        }
    }

    // initialising a semaphore
    if (sem_init(&p_table->mutex, 1, 1) == -1)
    {
        perror("sem_init");
        exit(1);
    }
    // daemon process
    if (daemon(1, 1))
    {
        perror("daemon");
        exit(1);
    }
    scheduler(ncpu, time_slice);
    terminate();
    return 0;
}