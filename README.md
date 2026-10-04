# CSC 541 — Programming Project 2

**Rounded Buffer Problem: bounded circular buffer, P-40**  
**Team:** Shazan Ansar Mohammed and Saina Zardosht  
**Submission:** `541_proj2_Mohammed_Zardosht.zip`  
**Due:** October 13, 2026, 11:59 p.m., as stated in the assignment

## Project overview

This program runs multiple producer and consumer threads against a shared
five-slot circular buffer. Producers generate integers from 0 through 1000;
consumers remove the oldest available item. Every successful operation prints
the worker identity, timestamps, all five slots, and the current occupancy.
The program ends after the requested duration and joins every worker before
releasing its synchronization resources.

The assignment's completed functions are `insert_item(int item)` and
`remove_item(void)`. They use both POSIX semaphores and a Pthreads mutex.
The submission also implements the two extra-credit features: specific
producer/consumer IDs and separate attempt and completion times.

The instructor supplied `buffer_incomplete.c` and
`output_buffer_producer_consumer.txt`. The submitted source is `buffer.c`.
Its output keeps the producer/consumer messages and five-slot snapshots from
the sample while adding identity, timing, occupancy, and a final summary.
Random values and scheduling differ between executions, so the output's
structure and correctness matter more than reproducing the sample's numbers.

## Team contributions

The following contributions reflect the team's report and supplied review
material. They describe execution, review, and integration work alongside the
AI assistance disclosed below.

| Member | Work completed or reviewed |
| --- | --- |
| Shazan Ansar Mohammed | Ran the five macOS configurations and captured execution screenshots; reviewed insertion lock ordering, macOS semaphore compatibility, and elapsed-time reporting; participated in result verification and final packaging. |
| Saina Zardosht | Initiated GitHub setup; reviewed consumer status handling, circular pointer advancement, shutdown signaling, and the log-verification approach; reviewed the README and GenAI record and participated in checking results and preparing the submission. |

Both members collaborated through their shared CSUDH ChatGPT workspace,
reviewed the documentation and execution results, and prepared the submission
together. ChatGPT generated the initial implementation and documentation and
provided testing, verification, packaging, and GitHub guidance. `genai.txt`
contains the preserved conversation and the additional team-supplied sessions.

## Requirements and evidence

| Requirement | Implementation or evidence |
| --- | --- |
| Complete insertion and removal | `insert_item()` and `remove_item()` in `buffer.c` |
| Use semaphores and a mutex | `empty`, `full`, and `pthread_mutex_t mutex` |
| Preserve a bounded circular buffer | Five slots, modulo pointer updates, FIFO replay verification |
| Run the required configuration | `evidence/mac_01_required.txt`, from `./a.out 20 5 5` |
| Identify each producer/consumer | Startup and operation messages use stable role-specific IDs |
| Print attempt and actual completion times | Each completed operation prints both timestamps and elapsed waiting duration |
| Include five execution screenshots | The five PNGs in `screenshots/`, displayed below |
| Explain compilation, execution, and contributions | This README |
| Disclose GenAI communication | `genai.txt`, with supplied sessions also retained separately |

## Compile and run

Open Terminal inside the project folder. A C11 compiler and POSIX thread
support are required. Python 3 is used only by the optional log checker.

```sh
cc -std=c11 -Wall -Wextra -Wpedantic -pthread buffer.c -o a.out
./a.out 20 5 5
```

The three arguments are the runtime in seconds, producer count, and consumer
count. The instructor's required redirected command is:

```sh
./a.out 20 5 5 > output.txt
cat output.txt
```

Terminal stays quiet during a redirected run because output goes to the file.
Wait for the shell prompt to return before reading it. Alternatively, run
`make` to compile and `make clean` to remove the executable. Compile on the
machine where the program will execute; the ZIP contains source, not a binary.

The duration must be 1–86400 seconds, and each role must have 1–128 threads.
Missing, malformed, zero, negative, and out-of-range arguments produce a usage
message and failure status. A synchronization or thread-creation error is
reported and ends the process with failure.

### macOS and Linux

On macOS the program opens named POSIX semaphores with `sem_open()`. Names
include the process ID and are unlinked immediately after each successful
open. Handles remain usable until closed after all workers have joined.
On Linux the default path uses `sem_init()` and `sem_destroy()`.
Both paths expose semaphore pointers to the same `sem_wait()` and `sem_post()`
operations. The five Mac logs demonstrate the macOS path; an additional Linux
test exercised the named-semaphore branch through `USE_NAMED_SEMAPHORES`.

## Buffer organization and synchronization

The shared state includes `buffer[5]`, `insertPointer`, `removePointer`,
`count`, and the produced/consumed totals. A slot holding `-1` is empty;
`-1` is outside the generated item range and cannot be confused with data.
Every buffer change and its slot snapshot occur under the same mutex.

| Resource | Initial state | Role |
| --- | --- | --- |
| `empty` semaphore | 5 | Reserve available space before insertion |
| `full` semaphore | 0 | Reserve an available item before removal |
| `mutex` | Unlocked | Protect buffer state and complete output blocks |
| Circular pointers | Both 0 | Identify the next insertion and removal positions |
| Buffer slots | All `-1` | Mark the initial empty buffer |

### Insertion sequence

1. Wait on `empty` while holding no mutex.
2. Lock `mutex` and check whether shutdown has started.
3. Write the item at `insertPointer` and advance the pointer modulo five.
4. Increment occupancy and the produced total; record completion time.
5. Print the operation and its five-slot snapshot while holding the lock.
6. Unlock `mutex` and post `full` to make an item available to consumers.

### Removal sequence

1. Wait on `full` while holding no mutex.
2. Lock `mutex` and check whether shutdown has started.
3. Read the item at `removePointer`, clear that slot, and advance modulo five.
4. Decrement occupancy and increment the consumed total; record completion.
5. Print the operation and its snapshot while holding the lock.
6. Unlock `mutex` and post `empty` to make space available to producers.

Acquiring the mutex before a blocking semaphore wait can deadlock. For
example, a producer holding the lock while the buffer is full prevents the
consumer from acquiring that lock to free a slot. Waiting first allows the
other role to make progress. The mutex then serializes pointer, slot, and
counter changes. The separate insertion/removal pointers preserve FIFO order
and wrap from slot 4 to slot 0.

During normal execution, occupancy stays between zero and five and satisfies
`produced - consumed = count`. Semaphore counts are reservations: they need
not equal a displayed occupancy at every instant while an operation is in
flight. Shutdown also adds wakeup tokens, as described below.

### Function return values

The submitted starter-compatible interface is `int remove_item(void)`.
It prints the removed item and returns status: 0 for success or 1 if shutdown
prevents completion. `insert_item(int item)` uses the same status convention.
An item value of 0 is legitimate data and is not used as an operation status.
Synchronization failures end the process rather than returning an item value.

## Extra-credit output

### Individual worker IDs

Main assigns Producer 1 through Producer N and Consumer 1 through Consumer M.
A role and number identify a specific worker throughout a run. These logical
IDs are not operating-system thread numbers. The required five-producer,
five-consumer run prints all ten creation messages and carries the IDs in
attempt, completion, and stopped-operation messages.

### Attempt, completion, and waiting time

All workers use `CLOCK_MONOTONIC` relative to one program start timestamp.
Attempt time is recorded before entering the insertion/removal operation.
Completion time is recorded immediately after changing the buffer under the
mutex. A per-worker operation number matches each attempt with its outcome.

The printed waiting duration is `completion - attempt`. It includes semaphore
waiting, mutex contention, scheduling delay, and intervening logging overhead;
it is not an isolated measurement of semaphore waiting. Six decimal places
are formatting precision, not a claim about hardware clock accuracy.

These are actual lines from the Mac executions:

```text
Producer 2 produced 775 at time 10.369098 s (operation 4, attempted 6.240860 s, waited 4.128238 s)
Consumer 1 consumed 834 at time 10.335133 s (operation 1, attempted 4.131579 s, waited 6.203554 s)
```

The gaps demonstrate why attempt and completion must be recorded separately.
Some operations wait noticeably for space or an item, while others finish
almost immediately. The program does not promise fairness between workers.

## Shutdown and resource cleanup

When the requested duration expires, main sets the atomic stop flag while
holding the mutex. It posts enough wakeups for every potentially blocked
producer and consumer. Each awakened worker rechecks the stop flag under the
mutex before touching buffer state, so the additional tokens cannot cause an
insertion into a full buffer or removal from an empty buffer.

Sleeping workers check the flag between short sleep intervals. Main joins
all workers before closing or destroying the semaphores and destroying the
mutex. A pending attempt reports that it stopped before completing; it has no
successful completion time. The final snapshot may contain items because the
program stops at the time limit rather than draining the buffer. This is
consistent with the final conservation equation. Cleanup can take slightly
longer than the requested runtime.

## Five Mac experiments

The table records actual Mac execution results. Each row links to a complete
log in the screenshot section. Times and item values vary on subsequent runs.

| Configuration | Arguments | Produced | Consumed | Remaining | Peak occupancy | Longest producer wait | Longest consumer wait |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Required configuration | `20 5 5` | 38 | 38 | 0 | 3 | 0.000549 s | 2.043725 s |
| One producer, one consumer | `12 1 1` | 8 | 7 | 1 | 3 | 0.000214 s | 1.030160 s |
| Producer-heavy | `12 5 1` | 11 | 6 | 5 | 5 | 4.128238 s | 0.000211 s |
| Consumer-heavy | `12 1 5` | 4 | 4 | 0 | 2 | 0.000124 s | 6.203554 s |
| Balanced | `12 3 3` | 20 | 19 | 1 | 4 | 0.000275 s | 1.008346 s |

To repeat the five runs:

```sh
mkdir -p evidence screenshots
./a.out 20 5 5 > evidence/mac_01_required.txt
./a.out 12 1 1 > evidence/mac_02_single.txt
./a.out 12 5 1 > evidence/mac_03_producers.txt
./a.out 12 1 5 > evidence/mac_04_consumers.txt
./a.out 12 3 3 > evidence/mac_05_balanced.txt
```

`sh run_tests.sh` compiles and runs the same cases. Repeating the runs replaces
these logs; new screenshots should then be captured so the images and logs
describe the same executions.

### Log verification

Run the checker against the five program-output files:

```sh
python3 verify_output.py evidence/mac_01_required.txt evidence/mac_02_single.txt evidence/mac_03_producers.txt evidence/mac_04_consumers.txt evidence/mac_05_balanced.txt
```

The final package includes the successful results in
`evidence/mac_verification.txt`. That file is a checker report rather than
program output; do not pass it back to the checker. An indiscriminate
`evidence/mac_*.txt` wildcard would include the report after it has been saved.
Run Python normally, without `-O`, because the checker uses assertions.

The submitted checker replays a FIFO queue and the circular slot positions.
It checks capacity, exact snapshots, item range, unique creation messages,
matching attempts and outcomes, nondecreasing printed times, waiting-time
arithmetic, final totals, stopped attempts, and the final cleanup marker.
It requires both insertion and removal, so very short random runs may not be
useful test cases. Passing a finite set of traces supports correctness for
those executions; it does not prove every possible thread schedule.

## Five execution screenshots

These are the team's actual Mac Terminal screenshots. Some show labeled
excerpts from saved output so the commands and final results fit on screen;
the complete unedited logs are linked beneath each image.

### 1. Required: 20 seconds, five producers, five consumers

![Required Mac run and individual thread creation messages](screenshots/01_required.png)

[Full execution log](evidence/mac_01_required.txt).
This image shows the required configuration, all worker creation messages,
and the final empty buffer with 38 items produced and consumed.

### 2. One producer and one consumer

![Single producer and consumer Mac run](screenshots/02_single.png)

[Full execution log](evidence/mac_02_single.txt).
The timed run ends with one item remaining: 8 produced minus 7 consumed.

### 3. Five producers and one consumer

![Producer-heavy Mac run with delayed insertion](screenshots/03_producers.png)

[Full execution log](evidence/mac_03_producers.txt).
The image includes insertion completion lines with different attempt and
completion times. Final occupancy is five, and the longest producer wait is
4.128238 seconds.

### 4. One producer and five consumers

![Consumer-heavy Mac run with delayed removal](screenshots/04_consumers.png)

[Full execution log](evidence/mac_04_consumers.txt).
The image includes consumer completion lines and a 6.203554-second wait.
All four produced items were consumed, leaving an empty buffer.

### 5. Three producers and three consumers

![Balanced Mac run with timestamps and final snapshot](screenshots/05_balanced.png)

[Full execution log](evidence/mac_05_balanced.txt).
The run finishes with 20 produced, 19 consumed, and one item remaining.

## Observations

The producer-heavy Mac trace reached the five-item capacity and contains
delayed insertions. The consumer-heavy trace contains longer waits for items
and ends empty. These results illustrate capacity and availability blocking;
the comparisons are observations from these runs, not a general performance
benchmark.

The required configuration also ends empty, while the single-pair and balanced
runs leave one item each. A nonzero remainder is expected when a timed run
stops before all produced items are removed. Every final summary satisfies
`produced - consumed = remaining`, and every recorded slot snapshot matches
the replayed queue.

Workers can attempt operations in an order different from successful buffer
changes. FIFO applies to inserted items, not to fairness among waiting
producers or consumers. The common clock makes those scheduling differences
visible without adding random sleep intervals by hand.

## Additional reference tests

Linux preparation logs are retained separately as `evidence/01_...` through
`05_...`; they are not the Mac runs summarized above. Additional preparation
checks cover strict GCC compilation, the Makefile, a named-semaphore execution,
13 invalid argument cases, shell syntax, and an AddressSanitizer/UBSan run.
Details are in `evidence/TEST_REPORT.txt` and the accompanying logs.

The sanitizer run disabled leak detection because the preparation environment
could not run LeakSanitizer. Memory-leak detection and ThreadSanitizer are not
claimed as passed. These reference checks supplement the team's five Mac runs.

## Supplied review sessions and final implementation

The new team-supplied sessions are preserved verbatim in
`evidence/team_supplied_genai.txt` and included in `genai.txt` with their
provenance identified. They discuss insertion lock ordering, consumer status,
macOS semaphore support, timing, shutdown, and Python verification. Their
example snippets illustrate concepts; `buffer.c` and `verify_output.py` are
the executable submission.

Several differences matter when following those examples:

- The consumer example uses `remove_item(buffer_item *item)`. The submitted
  program retains `remove_item(void)`, prints the removed value, and returns
  status. An output pointer is an alternative interface, not a required change.
- The semaphore example uses fixed names and simplified opening logic. The
  submitted implementation uses process-specific names, checks errors, and
  unlinks each newly opened name; it does not remove another process's fixed
  semaphore names before opening.
- The submitted stop flag is atomic and is checked under the mutex before
  buffer mutation. A plain flag read outside the lock would need an appropriate
  synchronization strategy.
- The sample Python checker handles FIFO and capacity but does not validate
  final printed totals, slot snapshots, identities, timestamps, and shutdown
  as fully as the submitted checker. Its finite traces cannot guarantee all
  possible concurrent executions.

These distinctions keep the documentation aligned with the actual interfaces,
error handling, and verification behavior without rewriting historical notes.

## GitHub and package contents

The team reported completing the public repository setup under `SHAZAN01` and
adding Saina. Repository invitation acceptance and current remote contents are
account-side facts rather than properties of this ZIP. To inspect the local
checkout and repository when GitHub CLI is available:

```sh
git status
gh repo view --json url,visibility
```

The archive has one top-level folder, `541_proj2_Mohammed_Zardosht`, containing:

| Path | Purpose |
| --- | --- |
| `buffer.c` | Completed C source with synchronization and extra credit |
| `README.md` | Compilation, design, contributions, results, and five images |
| `genai.txt` | Preserved project communication and supplied sessions |
| `screenshots/` | Five actual Mac Terminal screenshots |
| `evidence/mac_01...mac_05...` | Complete Mac execution logs |
| `evidence/mac_verification.txt` | Checker results for those five logs |
| `evidence/team_supplied_genai.txt` | Verbatim supplied sessions |
| Other files in `evidence/` | Linux reference logs, argument tests, and reports |
| `Makefile`, `run_tests.sh`, `verify_output.py` | Build, execution, and verification helpers |

Compiled executables, Git metadata, reference PNGs, and OS metadata are omitted
from the submission. Submit the ZIP on Canvas using the required name;
publishing on GitHub does not replace the Canvas upload. If further GenAI
assistance is used before submission, add that communication to the record.

## References

- Instructor-provided `buffer_incomplete.c` and
  `output_buffer_producer_consumer.txt` supplied with the assignment.
- POSIX mutex specification:
  https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_mutex_lock.html
- Apple's named semaphore documentation:
  https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/sem_open.2.html
- Apple's semaphore declarations:
  https://github.com/apple/darwin-xnu/blob/main/bsd/sys/semaphore.h
