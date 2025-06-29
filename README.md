# SimpleScheduler
Our code implements a simple shell and scheduler system using shared memory for inter-process communication.

1.⁠⁠Shell (main program):
- Initializes shared memory and semaphores for process table management.
- Creates a child process to run the scheduler.
- Implements a command-line interface for user input.
- Handles built-in commands and executes external commands.
- Manages process submission and adds processes to the shared process table.
  
2.⁠⁠Scheduler:
- Runs as a separate process in the background, created by the shell (daemon process).
- Uses the same shared memory to access the process table from the shell.
- Implements a priority queue min_heap (waiting queue) and simple queue( running queue) for process management.
- Waiting queue is a min heap that stores the processes based on priority.
- Running queue is a queue with size ncpu.
- Schedules processes based on their priority and available CPUs.
- Manages process execution using signals (SIGSTOP and SIGCONT).
  
3.⁠⁠Shared memory includes:
- Process: Stores information about each process (PID, priority, command, execution time, etc.).
- history_struct: Shared memory structure containing the process table and synchronization primitives.
- Uses semaphores to protect access to shared data structures.
  
4.⁠⁠Key Features:
- Priority-based scheduling: Uses a priority queue to select the next process to run.
- Time slicing: Processes run for a fixed time slice before being preempted.

link to ELF-loader:https://github.com/Jaitrika/SimpleLoader-in-C
