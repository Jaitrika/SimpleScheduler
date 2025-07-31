# SimpleScheduler

Our code implements a simple shell and scheduler system using shared memory for inter-process communication.

## Overview

This repository provides:
- A custom **shell** with job submission and history tracking
- A **priority-based process scheduler** running as a background daemon
- Efficient inter-process communication using POSIX shared memory and semaphores

## Features

- **Priority-based Scheduling:** Jobs are scheduled into CPU cores according to their priority.
- **Time Slicing:** Each process runs for a assigned time slice (`tslice`) before being preempted.
- **Min-Heap Waiting Queue:** Waiting processes are managed in a min-heap for quick access to the highest-priority job.
- **Running Queue:** Active jobs are managed in a circular queue sized to hardware concurrency (`ncpu`).
- **Robust Process Table:** Shared and protected access to process history, state, run/wait times, etc.
- **Job Submission:** Submit jobs with specified priority levels, track running and waiting jobs.
- **History Command:** View all submitted jobs, their status, execution, and wait times.

## Choosing tslice and Scheduler Polling Interval

> Short-lived processes (e.g., `fib.c`) can complete in just a few milliseconds. To accurately detect and account for such fast jobs, we recommend using **small tslice values** (e.g., 1–10ms).  
> The scheduler should **poll very frequently** (sleep 0–5ms, or not at all) to avoid missing quick process completions.  
>  
> Longer processes (e.g., `a.c`) benefit from **larger tslice values** (e.g., 100–1000ms) to reduce context-switching overhead. For these, the scheduler can sleep 50ms or more between cycles without significant accuracy loss.  
>  
> This balance ensures efficient CPU usage while maintaining accurate job accounting, especially for mixed workloads.

## Structure

| Component           | Description                                                                                                                     |
|---------------------|---------------------------------------------------------------------------------------------------------------------------------|
| **Shell**           | Initializes shared memory & semaphores, spawns the scheduler, offers CLI for submitting jobs/commands.                         |
| **Scheduler**       | Daemon process, schedules jobs by priority and timeslice, manages running and waiting queues.                                  |
| **Shared Memory**   | Stores full job table, synchronization primitives (semaphores), and inter-process communication structures.                    |

## Compilation & Usage

```
gcc shellsched.c -o shellsched
gcc scheduler.c -o scheduler
```

**Run the shell:**
```
./shellsched [ncpu] [tslice]
```
- `ncpu` = number of CPU slots for running jobs (`> 0`)
- `tslice` = size of time slice in ms (`> 0`), see advice above

**Example:**
```
./shellsched 2 100
```

## Example Commands (inside the Shell)

- Submit a process:
  ```
  submit ./a.out [priority]
  ```
- View process history:
  ```
  history
  ```
- Exit shell:
  ```
  exit
  ```

## Practical Usage Notes

- Use **small tslice** for many rapid, short-lived jobs; increase for long computations to reduce scheduling overhead.
- Default polling is fast to avoid missing process terminations in rapid workloads.

## See Also

- ELF Loader: Simple loader for ELF binaries —  
  `https://github.com/Jaitrika/SimpleLoader-in-C`

[1] https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/62101886/70a97cdf-cca4-432d-9ece-91ceb1fbf9ea/shellsched.c
[2] https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/62101886/4217b7a8-1e54-40c0-ae2d-31349b2a67b5/scheduler.c
[3] https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/62101886/299a7941-631d-4642-b09f-0a7f69ff054b/README.md
