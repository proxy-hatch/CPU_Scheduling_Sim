//// This project is created to fulfill CMPT300 Assignment 3 requirements
//// It consists of an OS Process Scheduling Simulation with Priority Queue and Round Robin scheme
//// The Priority Queue has been chosen to implement "Multilevel Feedback Queue" as its Policy
//// Specifically, every job will enter the top priority queue at start
//// They will degrade a priority after each burst (staying at low after the second)
//// Since the user is managing the concept 'time' (in the Q command), the age of a process is not easily determined
//// thus to prevent starvation, a process will never be upgrade back its priority
////
////
//// Created on: Jul 7, 2017
//// Last Modified: Jul , 2017
//// Author: Yu Xuan (Shawn) Wang
//// Email: yxwang@sfu.ca
//// Student #: 301227972

#include <stdio.h>
#include <string.h>
#include <sys/poll.h>       // poll() to check if there is data on stdin buffer
#include <errno.h>
#include <limits.h> // INT_MAX||INT_MAX
#include "list.h"

#define UNUSED 999  // for initializing unused semaphores
// DEBUG macro is used to turn on various debugging features
// Disable at the release version
#define DEBUG

// |-------------------------------------------------------------------------|
// |                    Data Structures                                      |
// |-------------------------------------------------------------------------|
// keywords used for states in pcb
typedef enum {
    READY = 0,
    RUNNING,
    BLOCKED
} state;
// a translation table for the enums
// solution to print enums, as seen here: https://stackoverflow.com/a/2161797
char *enumStrings[] = {"READY", "RUNNING", "BLOCKED"};


// Process Control block. 1 for each process
// contains info about the process
// used in list->node->data
typedef struct pcb {
    unsigned int pID;    // process ID
    // only 1 byte unsigned int is needed
    // https://stackoverflow.com/a/9966679
    unsigned int priority;   // 0: top; 1: mid; 2: low
    state state;
    char procMsg[41];   // as instructed: null terminated, 40 char max
} pcb;

// semaphore data struct to be provided to the user
typedef struct sem {
    unsigned int sem;
    LIST *procs; // a list of processes controlled by this semaphore
} sem;

unsigned int highestPID;
LIST *priorityQ[3];     // 3 lists
unsigned int run;      // global variable to control whether the simulation is shutting down
pcb *proc_init;     // special process to be put when nothing else is running
pcb *runningProc;       // ptr to the process that is currently running



// |-------------------------------------------------------------------------|
// |                          Helper Functions                               |
// |-------------------------------------------------------------------------|

// attempts to enqueue the process into the priority queue.
// If a queue is full it will try to put it in the one below until queues are exhausted,
// which will then fail and return 1
// returns 0 upon success
int enqueueProc(pcb *aProc) {
    // reject proc_init but DO NOT toss an errors
    if (proc_init == aProc)
        return 0;
    // ListPrepend() return 1 upon failure, try the other queues
    if (ListPrepend(priorityQ[aProc->priority], aProc) == 1) {
        // find the next available queues
        int availablePriority[3] = {[0]=0, [1]=1, [2]=2};
        int i;
        availablePriority[aProc->priority] = -1;      //set it to invalid
        for (i = 0; i < 3; i++) {
            if (availablePriority[i] != -1) {
                availablePriority[i] = -1;
                break;
            }
        }
        // try the second queue
        if (i < 3 && ListPrepend(priorityQ[i], aProc) == 1) {
            // find the third queues
            for (i = 0; i < 3; i++) {
                if (availablePriority[i] != -1) {
                    break;
                }
            }
            // try the third queue
            if (i < 3 && ListPrepend(priorityQ[i], aProc) == 0) {   // success
                printf("enqueueProc(PID:%u) re-ordered process priority from %u to  %d\n", aProc->pID, aProc->priority,
                       i);
                return 0;
            } else {
                fprintf(stderr, "enqueueProc(PID:%u) failed: No available queue\n", aProc->pID);
                return 1;
            }
        } else if (i < 3) {      // success
            printf("enqueueProc(PID:%u) re-ordered process priority from %u to  %d\n", aProc->pID, aProc->priority,
                   i);
            return 0;
        } else {  // failed
            fprintf(stderr, "enqueueProc(PID:%u) failed: No available queue\n", aProc->pID);
            return 1;
        }
    } else return 0;
}

// creates a new process initialized the highest priority, as multi-level feedback queue dictates
pcb *createProc() {
    // for my own LIST.h implementation only
//    if (highestPID > MAXNODECOUNT - 1) {
//        printf("Process creation failed! All the queues are full!\n");
//        return NULL;
//    }
    pcb *newProc = malloc(sizeof(pcb));
    // assume machine is 32 bit, unsigned int is 2 byte
    if (highestPID == 65535) {  //overflow occurred, loop back
        newProc->pID = highestPID;
        highestPID = 0;
    } else
        newProc->pID = highestPID++;
    newProc->priority = 0;
    newProc->state = READY;
    memset(&(newProc->procMsg), 0, sizeof newProc->procMsg);
    return newProc;
}

// Dequeue the next process from the top non-empty queue and set it to run
// returns the ptr of to the process upon success
// return NULL upon fail
void runNextProc() {
    pcb *returnPcb;
    if ((returnPcb = ListTrim(priorityQ[0])) == NULL) {
        if ((returnPcb = ListTrim(priorityQ[1])) == NULL)
            returnPcb = ListTrim(priorityQ[2]);   // could be NULL
    }
    if (returnPcb) {
        returnPcb->state = RUNNING;
        runningProc = returnPcb;
    } else {    //all three lists are empty, make proc_init the running process
        proc_init->state = RUNNING;
        runningProc = proc_init;
    }
}

// Compare Process IDs (used for ListSearch() )
int findPID(void *proc1, void *pID) {
    return ((pcb *) proc1)->pID == *(unsigned int *) pID ? 1 : 0;
}

// Calls ListSearch() on each priority queue
// returns the queue priority # if found
// returns -1 if not found
int priorityQSearch(int (*comparator)(), void *comparisonArg) {
    pcb *procFound;
    for (unsigned int i = 0; i < 3; i++) {
        procFound = ListSearch(priorityQ[i], comparator, comparisonArg);
        if (procFound) {
            return i;
        }
    }
    return -1;
}

// print the process specified
// return 0 upon success, 1 upon failure
int printProc(pcb *procFound) {
    if (procFound) {
        printf("The process with pID=%u, has:\n", procFound->pID);
        printf("\tPriority: %u (0 being top, 2 being lowest)\n", procFound->priority);
        printf("\tState: %s\n", enumStrings[procFound->state]);
        printf("\tStored Message: \"%s\"\n\n", procFound->procMsg);
        return 0;
    } else
        return 1;
}


// check if stdin is empty
// return 1 if not empty, return 0 if empty
int stdinIsNotEmpty() {
    struct pollfd fds;
    fds.fd = 0;       // 0 for stdin
    fds.events = POLLIN;
    if (poll(&fds, 1, 0) != 1)
        // empty stdin buffer or error
        return 0;
    else return 1;
}

// best way to read from stdin: https://stackoverflow.com/a/9278353
// fills the passed in char[] with what's buffered at stdin
// stdin is drained after this call
// CAUTION: this func assumes the size pass in is correct. Ensure this is the case to prevent memory leak
// returns 0 on success, -1 on fail
// if stdin is empty, it waits for stdin (check if stdin is empty before use)
int getstdinStr(char *arr, int size) {
    char *p;
    //reset msg
    memset(arr, 0, sizeof(p));

    // fgets return NULL on failure
    if (fgets(arr, size, stdin)) {
        return 0;
    } else
        return -1;
}

// Designed for ONLY POSITIVE int
// best way to convert char[] to int: https://stackoverflow.com/a/22866001
// return int upon success. Return negative int upon failure
int strtoi(const char *str) {
    char *endptr;
    errno = 0;
    if (!str)
        return -1;
    long l = strtol(str, &endptr, 0);
    // we make the exception of allowing trailing \r \n here
    if (errno == ERANGE || (*endptr != '\0' && *endptr != '\n' && *endptr != '\r') || str == endptr) {
        return -1;
    }
    // Only needed if sizeof(int) < sizeof(long)
    if (l < INT_MIN || l > INT_MAX) {
        return -2;
    }
    return (int) l;
}

// |-------------------------------------------------------------------------|
// |                      User Commands Implementations                      |
// |-------------------------------------------------------------------------|

// create a process and put it on the appropriate ready Q.
void create_C() {
    // create new process (the pcb)
    pcb *newProc = createProc();
    if (!newProc)
        return;

    // enqueue process to the top of the priority queue
    if (enqueueProc(newProc) != 0) {    // failed
        // purge new process
        free(newProc);
        highestPID--;
        printf("Process creation failed! Are all the queues full?\n");
    } else   // success
    {
        printf("Process successfully created! The Process ID assigned is %u\n", newProc->pID);
    }
}

// Copy the currently running process and put it on the ready Q corresponding to the original process' priority. 
// Attempting to Fork the "init" process (see below) should fail. 
// Report: success or failure, the pid of the resulting (new) process on success.
int fork_F() {}

// kill the named process and remove it from the system.
// Report: action taken as well as success or failure.
// return 0 on success, 1 on failure
int kill_K(pcb *currProc, unsigned int delPID) {
    pcb *procFound;
    int queueFound;
    // search for the process ID
    if (currProc->pID == delPID)
        procFound = currProc;
    else {
        if ((queueFound = priorityQSearch(findPID, &delPID)) < 0)
            return 1;  // did not find
        else {
            // remove the node from list
            // NOTE: Data in node is NOT deleted but returned
            procFound = ListRemove(priorityQ[queueFound]);
        }
    }
    // delete if found
    if (procFound) {
        free(procFound);
        return 0;
    } else
        return 1;
}

// kill the currently running process. 
// Report: process scheduling information (eg. which process now gets control of the CPU)
int exit_E() {


// GUARD AGAINST DELETING INIT WHEN NOT EMPTY
//    RUN=0 WHEN NOTHING IS RUNNINGPROCESS=null
}

// time quantum of running process expires.
// Report: action taken (eg. process scheduling information)
// Returns ptr to the next running process, return NULL if no process is left (time to terminate).
quantum_Q() {
    runningProc->state = READY;
    if (runningProc->priority < 2)
        (runningProc->priority)++;

    // enqueueProc() returns 0 upon success (and rejects proc_init without tossing an error)
    if (enqueueProc(runningProc)) {
#ifdef DEBUG
        fprintf(stderr, "ListPrepend(priorityQ[%u],currRunning (PID=%u)) Failed!\n", runningProc->priority,
                runningProc->pID);
        return NULL;
#endif
    }
    runNextProc();
}

// send a message to another process - block until reply. 
// Report: success or failure, scheduling information, and reply source and text (once reply arrives)
int send_S() {}

// receive a message - block until one arrives 
// Report: scheduling information and (once msg is received) the message text and source of message
int receive_R() {}

// unblocks sender and delivers reply
// Report: success or failure
int reply_Y() {}

// Initialize the named semaphore with the value given. 
// ID's can take a value from 0 to 4. 
// This can only be done once for a semaphore - subsequent attempts result in error.
// Report: action taken as well as success or failure.
int sem_N() {}

// execute the semaphore P operation on behalf of the running process. 
// You can assume sempahores IDs numbered 0 through 4.
// Report: action taken (blocked or not) as well as success or failure.
int sem_P() {}

// execute the semaphore V operation on behalf of the running process. 
// You can assume sempahores IDs numbered 0 through 4. 
// Report: action taken (whether/ which process was readied) as well as success or failure.
int sem_V() {}

// dump complete state information of process to screen 
// (this includes process status and anything else you can think of)
void procinfo_I(unsigned int pID) {
    // check if its the special process proc_init
    if (pID == proc_init->pID) {
        printProc(proc_init);
    }
        // check if its the currently running process
    else if (pID = runningProc->pID) {
        printProc(proc_init);
    } else {
        // search for this pID in the priority queues
        int queueFound;
        pcb *procFound;
        if ((queueFound = priorityQSearch(findPID, &pID)) < 0) {
            printf("The Process ID you requested does not belong to any created process. Please try again.\n");
        } else {
            procFound = ListCurr(priorityQ[queueFound]);
            if (printProc(procFound)) {
                printf("The Process ID you requested does not belong to any created process. Please try again.\n");
#ifdef DEBUG
                fprintf(stderr, "priorityQSearch() reported finding pID%u but ListCurr() was unable to retrieve it!\n",
                        pID);
#endif
            }
        }
    }
}

// display all process queues and their contents
void totalinfo_T() {
    pcb *currItem;
    for (unsigned int i = 0; i < 3; i++) {
        printf("Displaying items in Priority [%u] queue:\n\n", i);
        currItem = ListLast(priorityQ[i]);
        while (currItem) {
            printProc(currItem);
            currItem = ListPrev(priorityQ[i]);
        }
        // List->curr is out of bounds at this point, reset it to tail for consistency
        ListLast(priorityQ[i]);
    }
}


int main() {
    // initialize queues to be used
    priorityQ[0] = ListCreate();
    priorityQ[1] = ListCreate();
    priorityQ[2] = ListCreate();
    LIST *waitingReply = ListCreate();    // used for sender blocked until reply
    LIST *waitingRcv = ListCreate();      // used for rcvers blocked until received
    // initialize all semaphores  to -32766, the 4 byte int max on the negative side
    // the program will use this number to check if semaphore is initialized
    sem sems[5] = {[0 ... 4].sem=UNUSED};
    // list procs will be initialized as needed

    // global variables initialization
    run = 1;
    highestPID = 0;
    proc_init = createProc();     // proc_init is set to run at the beginning
    runningProc = proc_init;       // ptr to the process that is currently running
    // local variables initialization
    char usrInput[64];      // since the expected input is single char, buffer overflow is ok

    int pIDRequest;
    char *arg1;       // return variable for strtok()
    char *arg2;       // return variable for strtok()
    char flag;

    while (run) {
        if (stdinIsNotEmpty()) {
            if (!getstdinStr(usrInput, 64)) {
                // second call with NULL returns the second token:
                // http://www.cplusplus.com/reference/cstring/strtok/
                arg1 = strtok(usrInput, " \t\r\n\v\f");     // trim any whitespaces
                if (arg1)
                    arg2 = strtok(NULL, " \t\r\n\v\f");

                // if flag is invalid, set it to invalid and await 'default' case
                if (!arg1 || strlen(arg1) > 1)       // arg1 might be NULL or longer than 1 char
                    flag = 'Z';
                else
                    flag = arg1[0];

                switch (flag) {
                    case 'C'  :
                        create_C();
                        break;
                    case 'F'  :
                        break;
                    case 'K'  :
                        break;
                    case 'E'  :
                        break;
                    case 'Q'  :
                        quantum_Q();
                        break;
                    case 'S'  :
                        break;
                    case 'R'  :
                        break;
                    case 'Y'  :
                        break;
                    case 'N'  :
                        break;
                    case 'P'  :
                        break;
                    case 'V'  :
                        break;
                    case 'I'  :
                        // interpret the second usr inputted argument as pID
                        if (arg2 && (pIDRequest = strtoi(arg2)) >= 0)
                            procinfo_I((unsigned int) pIDRequest);
                        else
                            puts("Process ID not recognized. Please try again.\n(Process ID can only be positive integers)\n");
                        break;
                    case 'T'  :
                        totalinfo_T();
                        break;
                    default:
                        puts("Invalid Input. Please input command according to the manual\n");
                        puts("[C] [F] [K pID] [E] [Q] [S pID MSG(40 char max)] [R]\n");
                        puts("[Y pID MSG(40 char max)] [N semID Init_Value] [P semID] [V semID] [I pID] [T]\n");
                        puts("Any subsequent chars after the expected are ignored.\n");
                }
            } else
                puts("Your input was not recognized! Please try again.\n");

            puts("\nThe process currently running is:\n");
            printProc(runningProc);
        }

    }


    return 0;
}