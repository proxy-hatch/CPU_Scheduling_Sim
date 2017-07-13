//// This project is created to fulfill CMPT300 Assignment 3 requirements
//// It consists of an OS Process Scheduling Simulation with Priority Queue and Round Robin scheme
////
//// Design decisions:
//// 1)
//// The Priority Queue has been chosen to implement "Multilevel Feedback Queue" as its Policy
//// Specifically, every job will enter the top priority queue at start
//// They will degrade a priority after each burst (staying at low after the second)
//// If a process was blocked either by semaphore or by waiting to recv/for reply, it will not suffer priority degradation.
//// Since the user is managing the concept 'time' (in the Q command), the age of a process is not easily determined
//// thus to prevent starvation, a process will never be upgrade back its priority
//// 2)
//// The send/receive/reply mechanism has been chosen to be implemented as such:
//// After a process has sent a message to another process, it will be waiting for REPLY from ANY process to unblock itself
//// During this time, other processes are allowed to send to it (and this other process will be blocked as a result),
//// but the message sent will eventually get overwritten by the last REPLY to this process, and the user will only see
//// the last message sent/replied to this process when it is running (however the user will be able to see it
//// before it runs with "totalinfo_T" command).
//// this leads to the slight flaw that the user will potentially not get a prompt to REPLY a process in order to unblock it
//// instead, the user will have to rely on using "totalinfo_T" command to see what is currently block and needs to be replied.
////
//// Additionally, sending and replying (when unblocked of course) to a process itself is allowed.
//// replying to a process that is not blocked and awaiting for a reply is allowed.
////
////
//// Created on: Jul 7, 2017
//// Last Modified: Jul 12, 2017
//// Author: Yu Xuan (Shawn) Wang
//// Email: yxwang@sfu.ca
//// Student #: 301227972

#include <stdio.h>
#include <string.h>
#include <sys/poll.h>       // poll() to check if there is data on stdin buffer
#include <errno.h>
#include <limits.h> // INT_MAX||INT_MAX
#include <ctype.h>	// toupper()


#define UNUSED 999  // for initializing unused semaphores

// DEBUG macro is used to turn on various debugging features
// Disable at the release version
// #define DEBUG
#define DEBUG2	//DEBUG2 macro is used for a less verbose debugging, also disabled at release ofc

#ifdef DEBUG2
#include "LIST.h"	// use my own list to compile with CLion IDE
#endif

#ifndef DEBUG2
#include "list.h"	// use instructor provided list for release
#endif

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
// used in LIST->node->data
typedef struct pcb {
    unsigned int pID;    // process ID
    // only 1 byte unsigned int is needed
    // https://stackoverflow.com/a/9966679
    unsigned int priority;   // 0: top; 1: mid; 2: low
    state state;
    unsigned int remotePID;      // the other procecss that (sent this process a msg)||(this process is sending msg to)
    char procMsg[41];   // as instructed: str to store the msg awaiting rcv. null terminated, 40 char max
} pcb;

// semaphore data struct to be provided to the user
typedef struct sem {
    int sem;
    LIST *procs; // a LIST of processes controlled by this semaphore
} sem;

unsigned int highestPID;
LIST *priorityQ[3];     // 3 LISTs for priority queue
sem sems[5] = {[0 ... 4].sem=UNUSED};        // 5 semaphores available for user controlling processes
LIST *waitingReply;    // used for sender blocked until reply
LIST *waitingRcv;      // used for rcvers blocked until received

// LIST procs will be initialized as needed
unsigned int run;      // global variable to control whether the simulation is shutting down
pcb *proc_init;     // special process to be put when nothing else is running
pcb *runningProc;       // ptr to the process that is currently running



// |-------------------------------------------------------------------------|
// |                          Helper Functions                               |
// |-------------------------------------------------------------------------|

// print the process specified
// return 0 upon success, 1 upon failure
int printProc(pcb *procFound) {
    if (procFound) {
        if (procFound == proc_init)
            printf("The special process \"init\" with pID=%u, has:\n", procFound->pID);
        else
            printf("The process with pID=%u, has:\n", procFound->pID);
        printf("\tPriority: %u (0 being top, 2 being lowest)\n", procFound->priority);
        printf("\tState: %s", enumStrings[procFound->state]);
        if(procFound->state==BLOCKED)
        	puts("\t(Note: A newly unblocked process that hasn't got a turn in the CPU will also be represented by a BLOCKING state)\n");
        else
        	puts("");
        if (procFound->remotePID != UNUSED)
            printf("\tThis process has message \"%s\" from %u waiting to be received.\n", procFound->procMsg,procFound->remotePID);
        else
            puts("\tThis process has no message waiting to be received.\n");
        return 0;
    } else
        return 1;
}

// Short version of printProc() for release ver.
int oneLinePrintProc(pcb * procPrint){
	if(!procPrint)
		return 1;
	else if(procPrint==proc_init)
		printf("Special \"Init\" Process with pID#%u and priority #%u\n",procPrint->pID, procPrint->priority);
	else
		printf("Process with pID#%u and priority #%u\n",procPrint->pID, procPrint->priority);
	return 0;
}


// Compare Process IDs (used for ListSearch() )
int findPID(void *proc1, void *pID) {
	unsigned int pIDToBeFound=*( (unsigned int *) pID);
#ifdef DEBUG2
	printf("\n###pIDToBeFound is %u\n",pIDToBeFound);
#endif
    return ((pcb *) proc1)->pID == pIDToBeFound ? 1 : 0;
}

// Compare Process IDs (used for ListSearch() )
void freePcbList(void *proc) {
    free((pcb *) proc);
}

// Calls ListSearch() on each priority queue
// returns the queue priority # if found
// returns -1 if not found
int priorityQSearch(int (*comparator)(), void *comparisonArg) {
    int i;
    for (i = 0; i < 3; i++) {
        if (ListSearch(priorityQ[i], comparator, comparisonArg)) {
#ifdef DEBUG2        	
        	printf("### priorityQSearch returning %d\n",i);
#endif
            return i;
        }
    }
    return -1;
}

// Calls ListSearch() on each enabled sem queue
// returns the sem sequence # if found
// returns -1 if not found
int semSearch(int (*comparator)(), void *comparisonArg) {
    pcb *procFound;
    for (unsigned int i = 0; i < 5; i++) {
        if (sems[i].sem != UNUSED) {
            procFound = ListSearch(sems[i].procs, comparator, comparisonArg);
            if (procFound) {
                return i;
            }
        }
    }
    return -1;
}

// returns 1 if there is no more process besides proc_init
// else return 0
int thereIsNoProc() {
    int i;
    // check all priority queues
    for (i = 0; i < 3; i++) {
        if (ListCount(priorityQ[i]))
            return 0;
    }
    // check all blocked queues
    if (ListCount(waitingRcv))
        return 0;
    if (ListCount(waitingReply))
        return 0;
    for (i = 0; i < 5; i++) {
        if (sems[i].sem != UNUSED && ListCount(priorityQ[i]))
            return 0;
    }
    // runningProc has to be proc_init if there are no other process
    if (runningProc && proc_init != runningProc)
        return 0;
    return 1;
}

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
    // if (highestPID > MAXNODECOUNT - 1) {
    //     printf("Process creation failed! All the queues are full!\n");
    //     return NULL;
    // }
    pcb *newProc = malloc(sizeof(pcb));
    // assume machine is 32 bit, unsigned int is 2 byte
    if (highestPID == 65535) {  //overflow occurred, loop back
        newProc->pID = highestPID;
        highestPID = 0;
    } else
        newProc->pID = highestPID++;
    newProc->priority = 0;
    newProc->state = READY;
    newProc->remotePID = UNUSED;
    memset(&(newProc->procMsg), 0, sizeof newProc->procMsg);
    return newProc;
}

// Dequeue the next process from the top non-empty queue and set it to run
// returns the ptr of to the process upon success
// return NULL upon fail
void runNextProc() {
    pcb *returnPcb=NULL;
    if ((returnPcb = ListTrim(priorityQ[0])) == NULL) {
        if ((returnPcb = ListTrim(priorityQ[1])) == NULL)
            returnPcb = ListTrim(priorityQ[2]);   // could be NULL
    }

    if (returnPcb) {
        runningProc = returnPcb;
    } else {    //all three LISTs are empty, make proc_init the running process
        proc_init->state = RUNNING;
        runningProc = proc_init;
    }
    puts("\nThe process now running is:");
#ifdef DEBUG
    printProc(runningProc);
#endif
#ifndef DEBUG
    oneLinePrintProc(runningProc);
#endif
    // if the non-proc_init process has state BLOCKED, it was just awakened from waitingForReply or waitingForRcv
    // print and clear its inbox
    if(returnPcb && returnPcb->state==BLOCKED){
    	printf("The process received a new message from pID#%u:\n\"%s\"\n",returnPcb->remotePID, returnPcb->procMsg);
    	returnPcb->remotePID=UNUSED;
    	memset(&(returnPcb->procMsg), 0, sizeof returnPcb->procMsg);
	   	returnPcb->state=RUNNING;
    }
}


// this function handles the killing of a specific process
// This function assumes that process has already probably dequeued from whichever data structure
// This function will also load the next available process if the current running process is killed
void deleteProc(pcb *delProc) {
    if (delProc) {
        if (delProc == proc_init) {
            if (thereIsNoProc()) {  // time to terminate
                puts("The special \"init\" process has been killed!\nIts properties were:\n");
                printProc(delProc);
                free(delProc);
                puts("\nGoodbye\n");
                run = 0;
            } else {
                fprintf(stderr,
                        "You have attempted to kill the special \"init\" process!\nThis is not allowed when there are still other processes running!\n");
            }
        } else {
            if (delProc == runningProc) {
                puts("The currently running process has been killed. Its properties were:\n");
                printProc(delProc);
                runNextProc();
            } else {
                printf("The process with pID#%u has been killed. Its properties were:\n", delProc->pID);
                printProc(delProc);
                free(delProc);
            }
        }
    }
#ifdef DEBUG
    else
       fprintf(stderr,"Warning: Empty pcb *delProc passed int deleteProc!\n");
#endif
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

    // enqueue process to the top of the priority queue
    if (!newProc || enqueueProc(newProc) != 0) {    // failed
        // purge new process
        if (newProc)
            free(newProc);
        highestPID--;
        fprintf(stderr, "Process creation failed! Are all the queues full?\n");
    } else   // success
    {
        printf("Process successfully created! The Process ID assigned is %u\n", newProc->pID);
    }
}

// Copy the currently running process and put it on the ready Q corresponding to the original process' priority. 
// Attempting to Fork the "init" process (see below) should fail. 
// Report: success or failure, the pid of the resulting (new) process on success.
void fork_F() {
    if (runningProc == proc_init) {
        puts("Forking failed. Cannot fork the special process \"init\"\n");
        return;
    }

    // create new process (the pcb)
    pcb *newProc = createProc();
    if (!newProc) {
        fprintf(stderr, "Process creation failed in forking! Are all the queues full?\n");
        return;
    }
    // copy pcb
    newProc->priority = runningProc->priority;
    newProc->remotePID = runningProc->remotePID;
    strcpy(newProc->procMsg, runningProc->procMsg);
    // newProc->state should be kept as READY

    // enqueue process to the top of the priority queue
    if (enqueueProc(newProc) != 0) {    // failed
        // purge new process
        free(newProc);
        highestPID--;
        fprintf(stderr, "Process creation failed in forking! Are all the queues full?\n");
    } else   // success
    {
        printf("Process successfully forked! The Process ID assigned is %u\n", newProc->pID);
    }
}

// kill the named process and remove it from the system.
// Report: action taken as well as success or failure.
void kill_K(unsigned int delPID) {
    pcb *procFound = NULL;
    int queueFound = -1;

    // search for the process ID
    if (runningProc->pID == delPID) {
        procFound = runningProc;
    } else if (proc_init->pID == delPID) {
        procFound = proc_init;
    } else if ((queueFound = priorityQSearch(findPID, &delPID)) >= 0) {
        // remove the node from LIST
        // NOTE: Data in node is NOT deleted but returned
        procFound = ListRemove(priorityQ[queueFound]);
    } else if ((queueFound = semSearch(findPID, &delPID)) >= 0) {
        procFound = ListRemove(sems[queueFound].procs);
    } else if (ListSearch(waitingReply, findPID, &delPID) != NULL) {
        procFound = ListRemove(waitingReply);
    } else if (ListSearch(waitingRcv, findPID, &delPID) != NULL) {
        procFound = ListRemove(waitingRcv);
    }

    // deletes if found
    if (procFound) {
        deleteProc(procFound);
    } else {
        printf("Did not find the process with pID# = %u.\nDeletion failed.\n", delPID);
    }
}

// kill the currently running process. 
// Report: process scheduling information (eg. which process now gets control of the CPU)
void exit_E() {
    // deletes if found (deleteProc() will filter if runningProc==proc_init
    if (runningProc) {
        deleteProc(runningProc);
    } else {
#ifdef DEBUG
        fprintf(stderr, "exit_E() failed! There is no runningProc!\n");
#endif
    }
}

// time quantum of running process expires.
// Report: action taken (eg. process scheduling information)
void quantum_Q() {
    puts("The currently running process ");
#ifdef DEBUG
    puts("\n");
    printProc(runningProc);
#endif
#ifndef DEBUG
    oneLinePrintProc(runningProc);
#endif
    puts("will now stop occupying the CPU.");
    runningProc->state = READY;
    if (runningProc->priority < 2)
        (runningProc->priority)++;

    // enqueueProc() returns 0 upon success (and rejects proc_init without tossing an error)
    if (enqueueProc(runningProc)) {
#ifdef DEBUG
        fprintf(stderr, "ListPrepend(priorityQ[%u],currRunning (PID=%u)) Failed!\n", runningProc->priority,
                runningProc->pID);
#endif
    }
    runNextProc();
}

// send a message to another process - block until reply. 
// Report: success or failure, scheduling information, and reply source and text (once reply arrives)
void send_S(unsigned int remotePID, char *msg) {
    pcb *procFound = NULL;
    int queueFound;
    int foundInWaiting_bool=0;	// used to print additional prompt

    // design change: allow overwriting messages received by not displayed
//    // check if sending is allowed for runningProc
//    if (runningProc->remotePID != UNUSED) {
//        printf("Sending message \"%s\" to pID#%u failed: the current running process is currently communicating with process pID#%u.\nDetails:\n",
//               msg, remotePID, remotePID);
//        printProc(runningProc);
//
//    }
    // search for the process ID to be sent (remotePID)
    if (runningProc->pID == remotePID) {
        // send to self is allowed
        procFound=runningProc;
    } else if (proc_init->pID == remotePID) {
        procFound = proc_init;
    } else if ((queueFound = priorityQSearch(findPID, &remotePID)) >= 0) {
        procFound = ListCurr(priorityQ[queueFound]);
    } else if ((queueFound = semSearch(findPID, &remotePID)) >= 0) {
        procFound = ListCurr(sems[queueFound].procs);
    } else if (ListSearch(waitingReply, findPID, &remotePID) != NULL) {
        procFound = ListCurr(waitingReply);
    } else if (ListSearch(waitingRcv, findPID, &remotePID) != NULL) {
        // unblock the process waiting to rcv
        // (ListSearch() already sets curr to be the one found, which is the one that will be removed)
        procFound = ListRemove(waitingRcv);
        enqueueProc(procFound);
        foundInWaiting_bool=1;
    } else
        printf("Sending message \"%s\" to pID#%u failed: Cannot find process with pID#%u\n", msg, remotePID, remotePID);

    if (procFound) {        
        // send msg
        procFound->remotePID = runningProc->pID;
        strcpy(procFound->procMsg, msg);
        printf("Successfully sent \"%s\" to process with pID#%u \n", msg, remotePID);

		printf("The current running process ");
#ifdef DEBUG
		puts("");
		printProc(runningProc);
#endif
#ifndef DEBUG
		printf("with pID#%u and priority #%u ",runningProc->pID,runningProc->priority);
#endif
        // block running process if its not the special process
        if (runningProc != proc_init) {
            puts("is now blocked waiting for reply.");
            runningProc->state = BLOCKED;
            ListPrepend(waitingReply, runningProc);
            runNextProc();
        }else
	        puts("is not blocked and still running as it is the speical \"init\" process.");

	    if(foundInWaiting_bool)
	    {
	    	printf("The recipient process with pID#%u is now unblocked.\n", msg);

	    }
    }
}

// receive a message - block until one arrives 
// Report: scheduling information and (once msg is received) the message text and source of message
int receive_R() {
    // msg awaiting rcv exist
    if (strlen(runningProc->procMsg)) {
        printf("You have a new message from sender pID#%u:\n", runningProc->remotePID);
        printf("\"%s\"\n", runningProc->procMsg);
        // clear inbox
        memset(&(runningProc->procMsg), 0, sizeof runningProc->procMsg);
        runningProc->remotePID = UNUSED;
    } else {
        puts("No new messages.");
        if (runningProc != proc_init) {
            printf("The current running process ");
#ifdef DEBUG
        	puts("");
			printProc(runningProc);
#endif
#ifndef DEBUG
			printf("with pID#%u and priority #%u ",runningProc->pID,runningProc->priority);
#endif
            puts("has been blocked to wait for reply");
            runningProc->state=BLOCKED;
            ListPrepend(waitingRcv, runningProc);
            runNextProc();
        }
    }
}

// unblocks sender and delivers reply
// Report: success or failure
void reply_Y(unsigned int remotePID, char *msg) {
    pcb *procFound = NULL;

    // search for the process ID to be sent (remotePID)
    // if its not waiting for reply, do not allow the message to be sent
    if (ListSearch(waitingReply, findPID, &remotePID) != NULL) {
        procFound = ListRemove(waitingReply);
        enqueueProc(procFound);
    } else
        printf("Replying message \"%s\" to pID#%u failed: It is not waiting for a reply at this time (or it doesn't even exist)\n",
               msg, remotePID);

    if (procFound) {
        // send msg
        procFound->remotePID = runningProc->pID;
        strcpy(procFound->procMsg, msg);
        printf("Successfully replied \"%s\" to process with pID#%u \n", msg, remotePID);
        puts("The recipient process is now unblocked");
    }
}

// Initialize the named semaphore with the value given. 
// ID's can take a value from 0 to 4. 
// This can only be done once for a semaphore - subsequent attempts result in error.
// Report: action taken as well as success or failure.
int sem_N(unsigned int semID, int initVal) {
    if (semID > 4)
        printf("Semaphore #%u is too large.\nOnly value 0-4 is acceptable. Please try again.\n", semID);
    else if (sems[semID].sem == UNUSED) {
        sems[semID].sem = initVal;
        sems[semID].procs = ListCreate();
        printf("Semaphore #%u is successfully initialized to %d.\n", semID, initVal);
    } else
        printf("Semaphore #%u is already in use.\n", semID);
}

// execute the semaphore P operation on behalf of the running process. 
// You can assume sempahores IDs numbered 0 through 4.
// Report: action taken (blocked or not) as well as success or failure.
void sem_P(unsigned int semID) {
    if (sems[semID].sem == UNUSED) {
        printf("The semaphore #%u you have attempted to use is not yet initialized.\n Use command \"N %u [Initial Value]\" first.\n",
               semID, semID);
        return;
    } else if (runningProc == proc_init) {
        printf("The P operated on semaphore #%u failed because blocking the special process \"init\" is prohibited.\n",
               semID);
        return;
    } else
        printf("The P operated on semaphore #%u was successfully executed.\n", semID);

    printf("The current running process ");
#ifdef DEBUG
    	puts("");
		printProc(runningProc);
#endif
#ifndef DEBUG
		printf("with pID#%u and priority #%u ",runningProc->pID,runningProc->priority);
#endif
    if (sems[semID].sem <= 0) {    // implement blocking
        runningProc->state = BLOCKED;
        ListPrepend(sems[semID].procs, runningProc);
        puts("is now blocked.\n");
        (sems[semID].sem)--;
        printf("The value of this semaphore is now %d\n", sems[semID].sem);
        runNextProc();
    } else{
        puts("is not blocked and still running.");
        (sems[semID].sem)--;
        printf("The value of this semaphore is now %d\n", sems[semID].sem);
    }
}

// execute the semaphore V operation on behalf of the running process. 
// You can assume sempahores IDs numbered 0 through 4. 
// Report: action taken (whether/ which process was readied) as well as success or failure.
void sem_V(unsigned int semID) {
    pcb *poppedProc;

    if (sems[semID].sem == UNUSED) {
        printf("The semaphore #%u you have attempted to use is not yet initialized.\n Use command \"N %u\" first.\n",
               semID, semID);
        return;
    } else
        printf("The V operated on semaphore #%u was successfully executed.\n", semID);

    if ((poppedProc = ListTrim(sems[semID].procs)) != NULL) {
        printf("The process ");
#ifdef DEBUG
    	puts("");
		printProc(poppedProc);
#endif
#ifndef DEBUG
		printf("with pID#%u and priority #%u ",poppedProc->pID,poppedProc->priority);
#endif
        puts("is now readied");
        poppedProc->state=READY;
        enqueueProc(poppedProc);
    } else
        printf("No process was readied as no process was blocked by semaphore #%u\n", semID);

    (sems[semID].sem)++;
    printf("The value of this semaphore is now %d\n", sems[semID].sem);
}

// dump complete state information of process to screen 
// (this includes process state and anything else you can think of)
void procinfo_I(unsigned int pID) {
    pcb *procFound = NULL;
    int queueFound = -1;
    // search for the process ID
    if (runningProc->pID == pID) {
        procFound = runningProc;
    } else if (proc_init->pID == pID) {
        procFound = proc_init;
    } else if ((queueFound = priorityQSearch(findPID, &pID)) >= 0) {
        procFound = ListCurr(priorityQ[queueFound]);
    } else if ((queueFound = semSearch(findPID, &pID)) >= 0) {
        procFound = ListCurr(sems[queueFound].procs);
	} else if (ListSearch(waitingReply, findPID, &pID) != NULL) {
        procFound = ListCurr(waitingReply);
    } else if (ListSearch(waitingRcv, findPID, &pID) != NULL) {
        procFound = ListCurr(waitingRcv);
    }

    if (procFound)
        printProc(procFound);
    else
        printf("The Process ID you requested does not belong to any created process. Please try again.\n");
}

// display all process queues and their contents
void totalinfo_T() {
    pcb *currItem;
    int i;
    puts("--Displaying processes in readied queues by semaphores\n");
    for (i = 0; i < 3; i++) {
        printf("----Displaying processes in Priority [%u] ready queue:\n", i);
        currItem = ListLast(priorityQ[i]);
        while (currItem) {
            printProc(currItem);
            currItem = ListPrev(priorityQ[i]);
        }
        // List->curr is out of bounds at this point, reset it to tail for consistency
        ListLast(priorityQ[i]);
    }

    puts("\n--Displaying processes controlled by semaphores\n");
    for (unsigned int i = 0; i < 5; i++) {
        if (sems[i].sem != UNUSED) {
            printf("----Displaying processes controlled by active semaphore #%u :\n", i);
            currItem = ListLast(sems[i].procs);
            while (currItem) {
                printProc(currItem);
                currItem = ListPrev(sems[i].procs);
            }
            // List->curr is out of bounds at this point, reset it to tail for consistency
            ListLast(sems[i].procs);
        }

    }

    printf("\n--Displaying processes blocked waiting to receive a message:\n");
    currItem = ListLast(waitingRcv);
    while (currItem) {
        printProc(currItem);
        currItem = ListPrev(waitingRcv);
    }
    // List->curr is out of bounds at this point, reset it to tail for consistency
    ListLast(waitingRcv);

    printf("\n--Displaying processes that has sent a message and blocked waiting for a reply:\n");
    currItem = ListLast(waitingReply);
    while (currItem) {
        printProc(currItem);
        currItem = ListPrev(waitingReply);
    }
    // List->curr is out of bounds at this point, reset it to tail for consistency
    ListLast(waitingReply);
    puts("\n--The process that is currently running is:");
    printProc(runningProc);

}


int main() {
    // local variables initialization
    char usrInput[64];      // since the expected input is single char + a 40char max msg + some whitespace, buffer overflow is ok
    int IDRequest;
    char *arg1;       // parsed first argument (flag)
    char *arg2;       // parsed second argument (pID)
    char *arg3;       // parsed third argument (msg)
    char flag;
    int i;

    // initialize queues to be used
    for (i = 0; i < 3; i++)
        priorityQ[i] = ListCreate();
    waitingReply = ListCreate();    // used for sender blocked until reply
    waitingRcv = ListCreate();      // used for rcvers blocked until received

    // global variables initialization
    run = 1;
    highestPID = 0;
    proc_init = createProc();     // proc_init is set to run at the beginning
    proc_init->state = READY;
    runningProc = proc_init;       // ptr to the process that is currently running


    while (run) {
        if (stdinIsNotEmpty()) {
            if (!getstdinStr(usrInput, 64)) {
                // clear argument variables
                arg2 = NULL;
                arg3 = NULL;
                // second call with NULL returns the second token:
                // http://www.cplusplus.com/reference/cstring/strtok/
                arg1 = strtok(usrInput, " \t\r\n\v\f");     // trim any whitespaces
                if (arg1) {
                    arg2 = strtok(NULL, " \t\r\n\v\f");
                    if (arg2)	// interpret all the rest as arg3
                        arg3 = strtok(NULL, "\n");
                }
                // if flag is invalid, set it to invalid and await 'default' case
                if (!arg1 || strlen(arg1) > 1)       // arg1 might be NULL or longer than 1 char
                    flag = 'Z';
                else
                    flag = toupper(arg1[0]);

                switch (flag) {
                    case 'C'  :
                        create_C();
                        break;
                    case 'F'  :
                        fork_F();
                        break;
                    case 'K'  :
                        // interpret the second usr inputted argument as pID
                        if (arg2 && (IDRequest = strtoi(arg2)) >= 0)
                            kill_K((unsigned int) IDRequest);
                        else
                            puts("Process ID not recognized. Please try again.\n(Process ID can only be positive integers)\nFormat: Format: [K] [pID]\n");
                        break;
                    case 'E'  :
                        exit_E();
                        break;
                    case 'Q'  :
                        quantum_Q();
                        break;
                    case 'S'  :
                        // interpret the second usr inputted argument as pID, third as msg
                        if (!arg2 || (IDRequest = strtoi(arg2)) < 0)
                            puts("Process ID not recognized. Please try again.\n(Process ID can only be positive integers)\nFormat: [S] [pID] [msg]\n");
                        else if (!arg3 || !strlen(arg3))
                            puts("No message was detected. Please try again.\nFormat: [S] [pID] [msg]\n");

                        else
                            send_S((unsigned int) IDRequest, arg3);
                        break;
                    case 'R'  :
                        receive_R();
                        break;
                    case 'Y'  :
                        // interpret the second usr inputted argument as pID, third as msg
                        if (!arg2 || (IDRequest = strtoi(arg2)) < 0)
                            puts("Process ID not recognized. Please try again.\n(Process ID can only be positive integers)\nFormat: [Y] [pID] [msg]");
                        else if (!arg3 || !strlen(arg3))
                            puts("No message was detected. Please try again.\nFormat: [Y] [pID] [msg]");
                        else
                            reply_Y((unsigned int) IDRequest, arg3);
                        break;
                    case 'N'  :
                        // interpret the second usr inputted argument as semID, third as semaphore initial value
                        if (!arg2 || (IDRequest = strtoi(arg2)) < 0)
                            puts("Semaphore ID not recognized. Please try again.\n(Process ID can only be positive integers)\nFormat: N [semID] [Initial Value]");
                        else {   // stroi() was designed to support positive int only, so we must break it down here to parse arg3
                            if (!arg3)
                                puts("Sem initial value not recognized. Please try again.\n(initial value can only be integers)\nFormat: N [semID] [Initial Value]");
                            else {
                                char *endptr;
                                long l = strtol(arg3, &endptr, 0);
                                // we make the exception of allowing trailing \r \n here
                                if (errno == ERANGE || (*endptr != '\0' && *endptr != '\n' && *endptr != '\r') ||
                                    arg3 == endptr || l < INT_MIN || l > INT_MAX) {
                                    puts("Semaphore initial value not recognized. Please try again.\n(initial value can only be integers)\nFormat: N [semID] [Initial Value]");
                                } else {  // safe to use
                                    sem_N((unsigned int) IDRequest, (unsigned int) l);
                                }
                            }
                        }
                        break;
                    case 'P'  :
                        // interpret the second usr inputted argument as sem ID
                        if (arg2 && (IDRequest = strtoi(arg2)) >= 0)
                            sem_P((unsigned int) IDRequest);
                        else{
                            puts("Semaphore ID not recognized. Please try again.\n(Semaphore ID can only be integers between 0-4)\n");
                        	puts("Format: P [semID]");
                        }
                        break;
                    case 'V'  :
                        // interpret the second usr inputted argument as sem ID
                        if (arg2 && (IDRequest = strtoi(arg2)) >= 0)
                            sem_V((unsigned int) IDRequest);
                        else
                            puts("Semaphore ID not recognized. Please try again.\n(Semaphore ID can only be integers between 0-4)\n");
                        break;
                    case 'I'  :
                        // interpret the second usr inputted argument as pID
                        if (arg2 && (IDRequest = strtoi(arg2)) >= 0)
                            procinfo_I((unsigned int) IDRequest);
                        else
                            puts("Process ID not recognized. Please try again.\n(Process ID can only be positive integers)\n");
                        break;
                    case 'T'  :
                        totalinfo_T();
                        break;
                    default:
                        puts("Invalid Input. Please input command according to the manual");
                        puts("[C] [F] [K pID] [E] [Q] [S pID MSG(40 char max)] [R]");
                        puts("[Y pID MSG(40 char max)] [N semID Init_Value] [P semID] [V semID] [I pID] [T]");
                        puts("Any subsequent chars after the expected are ignored.");
                }
            } else
                puts("Your input was not recognized! Please try again.");
#ifdef DEBUG
            puts("\nThe process currently running is:");
            printProc(runningProc);
#endif
            puts("\n--------------------------------------------------------\n");
        }

    }

    // cleanup
    for (i = 0; i < 3; i++)
        ListFree(priorityQ[i], freePcbList);
    for (i = 0; i < 5; i++) {
        if (sems[i].sem != UNUSED)
            ListFree(sems[i].procs, freePcbList);
    }
    ListFree(waitingReply, freePcbList);
    ListFree(waitingRcv, freePcbList);

    return 0;
}