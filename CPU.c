#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <string.h>
#include "systemcall.h"
#define NUM_SECONDS 20
#define ever ;;

#define WRITEDBG(x, s)          \
  ({                            \
      WRITESTRING(#x" is: ");   \
      WRITEINT(x, s);           \
      WRITESTRING("\n");        \
  })

#define WRITEDBG1(x)            \
  ({                            \
      WRITESTRING(#x" is: ");   \
      WRITESTRING(x);           \
      WRITESTRING("\n");        \
  })

int eye2eh(int i, char *buffer, int buffersize, int base);

enum STATE { NEW, RUNNING, WAITING, READY, TERMINATED, EMPTY };

struct PCB {
    enum STATE state;
    const char *name;   // name of the executable
    int pid;            // process id from fork();
    int ppid;           // parent process id
    int interrupts;     // number of times interrupted
    int switches;       // may be < interrupts
    int started;        // the time this process started
};

typedef enum { false, true } bool;

int position = 0;
int total = 0;

#define PROCESSTABLESIZE 10
struct PCB processes[PROCESSTABLESIZE];

struct PCB idle;
struct PCB *running;

int sys_time;
int timer;
struct sigaction alarm_handler;
struct sigaction child_handler;
void printer(int);
void scheduler(int);
void workerbee(struct PCB *);
int nxtproc(int, int);
void scheduler();
void create_idle();
int time();
int runningfinder();
struct PCB updtr(struct PCB);

void bad(int signum) {
    WRITESTRING("bad signal: ");
    WRITEINT(signum, 4);
    WRITESTRING("\n");
}

// cdecl> declare ISV as array 32 of pointer to function(int) returning void
void(*ISV[32])(int) = {
/*       00   01   02   03   04   05   06   07   08   09 */
/*  0 */ bad, bad, bad, bad, bad, bad, bad, bad, bad, bad,
/* 10 */ bad, bad, bad, bad, bad, bad, bad, bad, bad, bad,
/* 20 */ bad, bad, bad, bad, bad, bad, bad, bad, bad, bad,
/* 30 */ bad, bad
};

void ISR (int signum) {
    if (signum != SIGCHLD) {
        kill (running->pid, SIGSTOP);
        WRITESTRING("Stopping: ");
        WRITEINT(running->pid, 6);
        WRITESTRING("\n");
    }

    ISV[signum](signum);
}

void send_signals(int signal, int pid, int interval, int number) {
    for(int i = 1; i <= number; i++) {
        sleep(interval);
        WRITESTRING("Sending signal: ");
        WRITEINT(signal, 4);
        WRITESTRING(" to process: ");
        WRITEINT(pid, 6);
        WRITESTRING("\n");
        systemcall(kill(pid, signal));
    }
}

void create_handler(int signum, struct sigaction action, void(*handler)(int)) {
    action.sa_handler = handler;

    if (signum == SIGCHLD) {
        action.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    } else {
        action.sa_flags =  SA_RESTART;
    }

    systemcall(sigemptyset(&action.sa_mask));
    systemcall(sigaction(signum, &action, NULL));
}

void scheduler (int signum) {
    int nextproc;
    
    assert(signum == SIGALRM);

    if (running->state != TERMINATED) {
        running->state = READY;
        running->interrupts++;
    }
    

    nextproc = nxtproc(position, total);
    
    if (nextproc != position){
        running->switches++;
    }

    if (nextproc >= 0) {
        running = &processes[nextproc];
        position = nextproc;
    }
    else {
        running=&idle;
    }

    if (runningfinder < 0) {
        running->state = TERMINATED;
    }
    
    if (running->state == NEW) {
        running->pid = fork();

        if (running->pid < 0) {
            perror("Fork");
            exit(running->pid);
        }
        else if (running->pid == 0) {
            systemcall(execl(processes[nextproc].name, processes[nextproc].name, (char *)NULL));
        }
        running->state = RUNNING;
        running->ppid = getppid();
    }
    else if (running->state == READY) {
        if (strcmp(running->name, "IDLE") == 0) {
            WRITESTRING("No running process detected...\n Exiting...");
            systemcall(kill(0, SIGTERM));
        }
        running->state = RUNNING;
        systemcall(kill(running->pid, SIGCONT));
    }
    else {
        WRITESTRING("No running process detected...\n Exiting...");
        systemcall(kill(idle.pid, SIGCONT));
    }
    
}

void process_done (int signum) {
    int wstatus;

    WRITESTRING("---- entering process_done\n");
    assert (signum == SIGCHLD);

    wait(&wstatus);

    if (running->pid > 0) {
        printer(running->pid);
        running->state = TERMINATED;
    }
}

void boot()
{
    sys_time = time(NULL);

    ISV[SIGALRM] = scheduler;
    ISV[SIGCHLD] = process_done;
    create_handler(SIGALRM, alarm_handler, ISR);
    create_handler(SIGCHLD, child_handler, ISR);

    assert((timer = fork()) != -1);
    if (timer == 0) {
        send_signals(SIGALRM, getppid(), 1, NUM_SECONDS);
        WRITESTRING ("Timer died, cleaning up and killing everything\n");
        systemcall(kill(0, SIGTERM));

        WRITESTRING ("---- leaving process_done\n");
        exit(0);
    }
}

void create_idle() {
    idle.state = READY;
    idle.name = "IDLE";
    idle.ppid = getpid();
    idle.interrupts = 0;
    idle.switches = 0;
    idle.started = sys_time;

    assert((idle.pid = fork()) != -1);
    if (idle.pid == 0) {
        systemcall(pause());
    }
}

int main(int argc, char **argv) {

    if ((argc < 1) | (argc > 9)) {
        WRITESTRING("Invalid number of arguments, aborting runtime processes...\n terminating");
        exit(0);
    }
    else {
        total = argc-1;

        for (int i=0; i < 10; i++) {
            struct PCB *curr = &processes[i-1];
            *curr = (struct PCB){EMPTY, "", 0, 0, 0, 0, sys_time};
        }

        for(int i = 1; i < argc; i++) {
            processes[i-1].state = NEW;
            processes[i-1].name = argv[i];
            processes[i-1].started = time(NULL);         
        }

        create_idle();

        running = &idle;
        running->state = RUNNING;
        
        boot();

        for(ever) {
            pause();
        }
    }
}

int runningfinder() {
    for (int i=0; i < 10; i++) {
        if (processes[i].state == RUNNING) {
            return i;
        }
    }
    return -1;
}

void printer(int chdpid) {
    int pos, runtime;
    
    for (int i=0; i < 9; i++) {
        if (processes[i].pid == chdpid) {
            pos = i;
        }
    }

    runtime = time(NULL)-processes[pos].started;

    WRITESTRING("Process Name: ");
    WRITESTRING(processes[pos].name);
    WRITESTRING("\nProcess PID: ");
    WRITEINT(processes[pos].pid, 6);
    WRITESTRING("\nProcess PPID: ");
    WRITEINT(processes[pos].ppid, 6);
    WRITESTRING("\nProcess Started: ");
    WRITEINT(processes[pos].started, 12);
    WRITESTRING("\nProcess switches: ");
    WRITEINT(processes[pos].switches, 6);
    WRITESTRING("\nNumber of process interrupts: ");
    WRITEINT(processes[pos].interrupts, 4);
    WRITESTRING("\nProcess took: ");
    WRITEINT(runtime, 3);
    WRITESTRING(" seconds to terminate\n");
}

//rewritten nxtproc with Luke's help
int nxtproc(int prevproc, int total) {
    int newindex = -1;
    int readyindex = -1;
    int first = prevproc+1;

    for (int offset=0; offset < total; offset++) {
        int currindex = (first+offset) % total;
        if ((processes[currindex].state == NEW) && (newindex == -1)) {
            newindex = currindex;
        }
        if ((processes[currindex].state == READY) && (readyindex == -1)) {
            readyindex = currindex;
        }
    }

    if (newindex != -1) {
        return newindex;
    }
    return readyindex;
}