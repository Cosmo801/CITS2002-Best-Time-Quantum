#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>


/* CITS2002 Project 1 2019
   Name(s):             William Knight
   Student number(s):   21722128
 */


//  besttq (v1.0)
//  Written by Chris.McDonald@uwa.edu.au, 2019, free for all to copy and modify

//  Compile with:  cc -std=c99 -Wall -Werror -o besttq besttq.c


//  THESE CONSTANTS DEFINE THE MAXIMUM SIZE OF TRACEFILE CONTENTS (AND HENCE
//  JOB-MIX) THAT YOUR PROGRAM NEEDS TO SUPPORT.  YOU'LL REQUIRE THESE
//  CONSTANTS WHEN DEFINING THE MAXIMUM SIZES OF ANY REQUIRED DATA STRUCTURES.

#define MAX_DEVICES             4
#define MAX_DEVICE_NAME         20
#define MAX_PROCESSES           50
// DO NOT USE THIS - #define MAX_PROCESS_EVENTS      1000
#define MAX_EVENTS_PER_PROCESS	100

#define TIME_CONTEXT_SWITCH     5
#define TIME_ACQUIRE_BUS        5


//  NOTE THAT DEVICE DATA-TRANSFER-RATES ARE MEASURED IN BYTES/SECOND,
//  THAT ALL TIMES ARE MEASURED IN MICROSECONDS (usecs),
//  AND THAT THE TOTAL-PROCESS-COMPLETION-TIME WILL NOT EXCEED 2000 SECONDS
//  (SO YOU CAN SAFELY USE 'STANDARD' 32-BIT ints TO STORE TIMES).

int optimal_time_quantum                = 0;
int total_process_completion_time       = 0;

//  ----------------------------------------------------------------------

//General Comments
//I tried to use as few strings as possible because they seemed more difficult to work with
//The result is multidimensional arrays of ints 
//When read, each device is assigned a device id so we can refer to it as an int rather than a string


//Concept
//This program is drien by the simulate_job_mix loop and the iterateQuantum() function
//A process starts and will iterateQuantum() until io can begin, its time quantum expires or it exits
//iterateQuantum() will increment the internal process time of each process and is independent of totalProcessTime which represents the OS time
//After time increment that belongs to the CPU we search for new processes or io to finish (scanProcesses(), checkIo())
//When a process has executed for long enough and begins io it is moved from the processQueue (CPU) to the blockedQueue
//After enough iotime has passed the blocked process is moved back to the processQueue where it can continue more io or exit
//ioTime is simply time that has passed since the program acquired the databus
//Helper methods such as queueProcess(), blockCurrentProcess() help to shift processes between New > Ready > Running > Blocked > more

//My variables

//[deviceId speed]
int deviceInfo[MAX_DEVICES][2];

//[processNumber startTime exitTime processExecutionTime started]
//started 0 = false
//processExecutionTime is the time the process has been executing for
int processInfo[MAX_PROCESSES][5];

//[processNumber deviceId startTime bytesRequired started]
//processNumber is needed to associate with the process later
//started 0 = false;
int eventInfo[MAX_PROCESSES * MAX_EVENTS_PER_PROCESS][5];

//Maps a deviceName to a deviceId
//Device ids are simply decided in the order they are read at runtime
//The device id is the index where the string is
//eg device id 0 = index 0 = "usb2"
char deviceMapping[MAX_DEVICES][MAX_DEVICE_NAME];

//The ready queue
int processQueue[MAX_PROCESSES] = { 0 };

//The blocked queue
//[processNumber deviceSpeed ioTime timePassed]
//timePassed = time passed when at start of blocked queue - ie has access to databus
int blockedQueue[MAX_PROCESSES][4];

//Tracks counts
int deviceIterator = 0;
int processIterator = 0;
int eventIterator = 0;

int numActiveProcesses = 0;

int numBlockedProcesses = 0;

//The computation time for a job mix
int totalProcessTime = 0;

int currentIoTime = 0;

//My functions

//----------------------------------------------------------

//Find the index in processInfo for a given processNumber
int getProcessIndex(int processNum){

    for(int i = 0; i < processIterator; i++)
	{
	int currentProcessNum = processInfo[i][0];
	if(currentProcessNum == processNum) return i;
    }
    return -1;
}

//Get device id for a device name (device id is decided at runtime)
//The device id corresponds to the index (see above where deviceMapping is declared)
int getDeviceId(char* deviceName) {

	for (int i = 0; i < MAX_DEVICES; i++)
	{
		if (strcmp(deviceName, deviceMapping[i]) == 0) return i;
	}

	//Error
	return -1;
}

//Read the event indexes for a process into a buffer[startIndex, count]
void getEventsForProcess(int processNumber, int buffer[2]) {

	int startIndex = -1;
	int count = 0;

	bool numFound = false;

	for (int i = 0; i < eventIterator; i++)
	{
		//Process has already started
		if (eventInfo[i][4] == 1) continue;

		int currentProcNumber = eventInfo[i][0];

		if (currentProcNumber == processNumber) {
			if (numFound) {
				count++;
				continue;
			}

			startIndex = i;
			count++;
			numFound = true;
			continue;
		}
		if (numFound) break;
	}

	buffer[0] = startIndex;
	buffer[1] = count;
}

//Refresh all variables so we can resimulate for a new time quantum
void refreshVariables(void) {

	totalProcessTime = 0;

	numActiveProcesses = 0;
	numBlockedProcesses = 0;
	

	for (int i = 0; i < processIterator; i++)
	{
		processInfo[i][3] = 0;
		processInfo[i][4] = 0;
		
		int buffer[2];
		getEventsForProcess(processInfo[i][0], buffer);
		
	}

	for(int i = 0; i < eventIterator; i++){
	    eventInfo[i][4] = 0;
	}
}

//Add process to end of process queue
void addProcessToQueue(int processNumber) {

	if (numActiveProcesses >= MAX_PROCESSES) return;

	processQueue[numActiveProcesses] = processNumber;

	numActiveProcesses++;

	//Set process state to started
	int i = getProcessIndex(processNumber);
	processInfo[i][4] = 1;
}

//The currently Running process is set to Ready
//Ie it has completed a time quantum but is not ready to exit
void requeueRunningProcess(void) {

	int processNum = processQueue[0];
	if (processNum == 0) return;

	//Shift indexes down
	for (int i = 0; i < numActiveProcesses - 1; i++)
	{
		processQueue[i] = processQueue[i + 1];
	}

	processQueue[numActiveProcesses - 1] = processNum;
}

//The currently Running process is exiting or blocking
void dequeueRunningProcess(void) {

	requeueRunningProcess();
	processQueue[numActiveProcesses - 1] = 0;

	numActiveProcesses--;
}

//See if any processes can start in the given time step
void scanProcesses(int maxTime) {

	for (int i = 0; i < processIterator; i++)
	{
		//Process has started already - ignore it
		int processStarted = processInfo[i][4];
		if (processStarted == 1) continue;

		int startTime = processInfo[i][1];
		int processNumber = processInfo[i][0];

		if (maxTime >= startTime) {
			printf("@%i: Process %i New > Ready \n", startTime, processNumber);
			addProcessToQueue(processNumber);
		}
	}

}

void dequeueIo(void) {

	printf("@%i: Process %i release databus \n", totalProcessTime, blockedQueue[0][0]);

	addProcessToQueue(blockedQueue[0][0]);

	for (int i = 0; i < numBlockedProcesses - 1; i++)
	{
		blockedQueue[i][0] = blockedQueue[i + 1][0];
		blockedQueue[i][1] = blockedQueue[i + 1][1];
		blockedQueue[i][2] = blockedQueue[i + 1][2];
		blockedQueue[i][3] = blockedQueue[i + 1][3];
	}

	blockedQueue[numBlockedProcesses][0] = 0;
	blockedQueue[numBlockedProcesses][1] = 0;
	blockedQueue[numBlockedProcesses][2] = 0;
	blockedQueue[numBlockedProcesses][3] = 0;

	
	numBlockedProcesses--;
	currentIoTime = 0;
}

void checkIo(void) {

	if (numBlockedProcesses == 0) return;

	if (blockedQueue[0][3] >= blockedQueue[0][2]) {
		dequeueIo();
		if(numBlockedProcesses > 0){
		    totalProcessTime += 5;
		    printf("@%i: Process %i acquire databus \n", totalProcessTime, blockedQueue[0][0]);
		}
	}

	



}

//Check to see if any io is ending or any new processes are being created
void incrementProcessTime(int increment) {
	totalProcessTime += increment;
	scanProcesses(totalProcessTime);
	

	//Io processes
	if (numBlockedProcesses > 0) {
		blockedQueue[0][3] += increment;
		checkIo();
	}
	


}

//Represents one cycle of time quantum where no i/o can be performed
void iterateQuantum(int timeQuantum) {

	int currentProcess = processQueue[0];
	if (currentProcess == 0) return;

	int currentProcessIndex = getProcessIndex(currentProcess);

	int processExitTime = processInfo[currentProcessIndex][2];
	int currentProcessExecutionTime = processInfo[currentProcessIndex][3];

	printf("@%i: Process %i new time quantum \n", totalProcessTime, currentProcess);

	//Exit before time quantum is over
	if (currentProcessExecutionTime + timeQuantum >= processExitTime) {

		int difference = processExitTime - currentProcessExecutionTime;

		//Running > Exit
		incrementProcessTime(difference);

		printf("@%i: Process %i Running > Exit \n", totalProcessTime, currentProcess);		
		dequeueRunningProcess();

		if (numActiveProcesses > 0) {

			currentProcess = processQueue[0];

			//Ready > Running
			incrementProcessTime(5);
			printf("@%i: Process %i Ready > Running \n", totalProcessTime, currentProcess);
			return;
		}

		printf("Finishing \n");
		return;

		

	}

	processInfo[currentProcessIndex][3] += timeQuantum;
	incrementProcessTime(timeQuantum);


	printf("@%i: Process %i time quantum expired \n", totalProcessTime, currentProcess);

	if (numActiveProcesses > 1) {

		requeueRunningProcess();
		printf("@%i: Process %i Running > Ready \n", totalProcessTime, currentProcess);

		//Running > Ready
		incrementProcessTime(5);

		currentProcess = processQueue[0];

		printf("@%i: Process %i ready > running \n", totalProcessTime, currentProcess);

	}
	



}

//Add i/o event to the blockedQueue
void queueIo(int eventIndex){

	if (numBlockedProcesses == MAX_PROCESSES) return;

	dequeueRunningProcess();

	int processNumber = eventInfo[eventIndex][0];
	int deviceId = eventInfo[eventIndex][1];
	int bytesRequired = eventInfo[eventIndex][3];

	int deviceSpeed = deviceInfo[deviceId][1];

	//Set io to started
	eventInfo[eventIndex][4] = 1;


	//ceiling
	int ioTime = ceil(bytesRequired / ((double)deviceSpeed / 1000000));

	if(numBlockedProcesses == 0){

	    incrementProcessTime(5);

	    printf("@%i: Process %i acquire databus \n", totalProcessTime, processNumber);

	    blockedQueue[0][0] = processNumber;
	    blockedQueue[0][1] = deviceSpeed;
	    blockedQueue[0][2] = ioTime;
	    blockedQueue[0][3] = 0;

	    numBlockedProcesses++;
	    return;

	}

	int newIndex = 1;
	bool found = false;

	for(; newIndex < numBlockedProcesses + 1; newIndex++){

	    if(deviceSpeed > blockedQueue[newIndex][1]){
		found = true;
		break;
	    }
	}
	
	//Add to very end
	if(found == false){

 	    blockedQueue[newIndex + 1][0] = processNumber;
	    blockedQueue[newIndex + 1][1] = deviceSpeed;
	    blockedQueue[newIndex + 1][2] = ioTime;
	    blockedQueue[newIndex + 1][3] = 0;

	    numBlockedProcesses++;
	    return;

	}

	//We will have to shift 
	else{

	    for(int i = numBlockedProcesses + 1; i > newIndex; i--){

		blockedQueue[i][0] = blockedQueue[i-1][0];
		blockedQueue[i][1] = blockedQueue[i-1][1];
		blockedQueue[i][2] = blockedQueue[i-1][2];
		blockedQueue[i][3] = blockedQueue[i-1][3];

	    }

	    	blockedQueue[newIndex][0] = processNumber;
		blockedQueue[newIndex][1] = deviceSpeed;
		blockedQueue[newIndex][2] = ioTime;
    		blockedQueue[newIndex][3] = 0;

		numBlockedProcesses++;
		return;

	}

}



//--------------------------------------------

#define CHAR_COMMENT            '#'
#define MAXWORD                 20

void parse_tracefile(char program[], char tracefile[])
{
//  ATTEMPT TO OPEN OUR TRACEFILE, REPORTING AN ERROR IF WE CAN'T
    FILE *fp    = fopen(tracefile, "r");

    if(fp == NULL) {
        printf("%s: unable to open '%s'\n", program, tracefile);
        exit(EXIT_FAILURE);
    }

    char line[BUFSIZ];
    int  lc     = 0;

//  READ EACH LINE FROM THE TRACEFILE, UNTIL WE REACH THE END-OF-FILE
    while(fgets(line, sizeof line, fp) != NULL) {
        ++lc;

//  COMMENT LINES ARE SIMPLY SKIPPED
        if(line[0] == CHAR_COMMENT) {
            continue;
        }

//  ATTEMPT TO BREAK EACH LINE INTO A NUMBER OF WORDS, USING sscanf()
        char    word0[MAXWORD], word1[MAXWORD], word2[MAXWORD], word3[MAXWORD];
        int nwords = sscanf(line, "%s %s %s %s", word0, word1, word2, word3);

//      printf("%i = %s", nwords, line);

//  WE WILL SIMPLY IGNORE ANY LINE WITHOUT ANY WORDS
        if(nwords <= 0) {
            continue;
        }
//  LOOK FOR LINES DEFINING DEVICES, PROCESSES, AND PROCESS EVENTS
        if(nwords == 4 && strcmp(word0, "device") == 0) {
            ;   // FOUND A DEVICE DEFINITION, WE'LL NEED TO STORE THIS SOMEWHERE

			//deviceId
			deviceInfo[deviceIterator][0] = deviceIterator;
			//speed
			deviceInfo[deviceIterator][1] = atoi(word2);

			strcpy(deviceMapping[deviceIterator], word1);

			deviceIterator++;
        }

        else if(nwords == 1 && strcmp(word0, "reboot") == 0) {
            ;   // NOTHING REALLY REQUIRED, DEVICE DEFINITIONS HAVE FINISHED
        }

        else if(nwords == 4 && strcmp(word0, "process") == 0) {
            ;   // FOUND THE START OF A PROCESS'S EVENTS, STORE THIS SOMEWHERE

			//processNumber
			processInfo[processIterator][0] = atoi(word1);
			//startTime
			processInfo[processIterator][1] = atoi(word2);
        }

        else if(nwords == 4 && strcmp(word0, "i/o") == 0) {
            ;   //  AN I/O EVENT FOR THE CURRENT PROCESS, STORE THIS SOMEWHERE

			//processNumber
			eventInfo[eventIterator][0] = processInfo[processIterator][0];
			//deviceId
			eventInfo[eventIterator][1] = getDeviceId(word2);
			//startTime
			eventInfo[eventIterator][2] = atoi(word1);
			//bytesRequired
			eventInfo[eventIterator][3] = atoi(word3);

			eventIterator++;
		

        }

        else if(nwords == 2 && strcmp(word0, "exit") == 0) {
            ;   //  PRESUMABLY THE LAST EVENT WE'LL SEE FOR THE CURRENT PROCESS

			//exitTime
			processInfo[processIterator][2] = atoi(word1);

        }

        else if(nwords == 1 && strcmp(word0, "}") == 0) {
            ;   //  JUST THE END OF THE CURRENT PROCESS'S EVENTS
			processIterator++;
        }
        else {
            printf("%s: line %i of '%s' is unrecognized",
                        program, lc, tracefile);
            exit(EXIT_FAILURE);
        }
    }
    fclose(fp);
}

#undef  MAXWORD
#undef  CHAR_COMMENT

//  ----------------------------------------------------------------------

//  SIMULATE THE JOB-MIX FROM THE TRACEFILE, FOR THE GIVEN TIME-QUANTUM
void simulate_job_mix(int time_quantum)
{
	refreshVariables();

	//This should never happen but it means no processes were found
	if (processIterator == 0) return;

	printf("running simulate_job_mix( time_quantum = %i usecs )\n",
		time_quantum);

	//Loading first process
	int initialStartTime = processInfo[0][1];
	addProcessToQueue(processInfo[0][0]);
	totalProcessTime += initialStartTime;

	printf("@%i: Process %i New > Ready \n", totalProcessTime, processQueue[0]);
	incrementProcessTime(5);
	printf("@%i: Process %i Ready > Running \n", totalProcessTime, processQueue[0]);

	while (true)
	{
		printf("\n");

		int currentProcessNumber = processQueue[0];
		int currentProcessIndex = getProcessIndex(currentProcessNumber);

		//buffer[0] = nextIoIndex for process
		//buffer[1] = ioEventCount for process
		int buffer[2];
		getEventsForProcess(currentProcessNumber, buffer);
		
		//No io to perform
		if (buffer[0] == -1) {
			iterateQuantum(time_quantum);		 
		}

		//io
		else
		{
			int ioStartTime = eventInfo[buffer[0]][2];
			int currentExecutionTime = processInfo[currentProcessIndex][3];
			
			if (currentExecutionTime + time_quantum < ioStartTime) {
				iterateQuantum(time_quantum);
				continue;
			}

			int difference = ioStartTime - currentExecutionTime;

			processInfo[currentProcessIndex][3] += difference;
			incrementProcessTime(difference);

			printf("@%i: Process %i request databus \n", totalProcessTime, currentProcessNumber);
			printf("@%i: Process %i running > blocked \n", totalProcessTime, currentProcessNumber);

			queueIo(buffer[0]);	

		}
	
		//No procesess are running - search for next new process
		if (numActiveProcesses == 0) {

			bool newProcessFound = false;
			int processIndex = 0;

			for (int i = 0; i < processIterator; i++)
			{
				if (processInfo[i][4] == 1) continue;
				newProcessFound = true;
				processIndex = i;
				break;
			}

			//No new processes found
			if (newProcessFound == false) {

				//Check if we are waiting on io
				if (numBlockedProcesses > 0) {
					
					int nextNum = blockedQueue[0][0];
					int nextTime = blockedQueue[0][2] - blockedQueue[0][3];

					totalProcessTime += nextTime;

					
					printf("@%i: Process %i Blocked > Ready \n", totalProcessTime, nextNum);
					dequeueIo();
					

					incrementProcessTime(5);
					
					printf("@%i: Process %i Ready > Running \n", totalProcessTime, nextNum);
					continue;
				}

				break;
			}

			int processStartTime = processInfo[processIndex][1];



			totalProcessTime = processStartTime;


			incrementProcessTime(5);

			printf("@%i: Process %i Ready > Running \n", totalProcessTime, processQueue[0]);
			continue;


		}

			
	}

	total_process_completion_time = totalProcessTime - initialStartTime;
	
    
}

//  ----------------------------------------------------------------------

void usage(char program[])
{
    printf("Usage: %s tracefile TQ-first [TQ-final TQ-increment]\n", program);
    exit(EXIT_FAILURE);
}

int main(int argcount, char *argvalue[])
{
    int TQ0 = 0, TQfinal = 0, TQinc = 0;

//  CALLED WITH THE PROVIDED TRACEFILE (NAME) AND THREE TIME VALUES
    if(argcount == 5) {
        TQ0     = atoi(argvalue[2]);
        TQfinal = atoi(argvalue[3]);
        TQinc   = atoi(argvalue[4]);

        if(TQ0 < 1 || TQfinal < TQ0 || TQinc < 1) {
            usage(argvalue[0]);
        }
    }
//  CALLED WITH THE PROVIDED TRACEFILE (NAME) AND ONE TIME VALUE
    else if(argcount == 3) {
        TQ0     = atoi(argvalue[2]);
        if(TQ0 < 1) {
            usage(argvalue[0]);
        }
        TQfinal = TQ0;
        TQinc   = 1;
    }
//  CALLED INCORRECTLY, REPORT THE ERROR AND TERMINATE
    else {
        usage(argvalue[0]);
    }

//  READ THE JOB-MIX FROM THE TRACEFILE, STORING INFORMATION IN DATA-STRUCTURES
    parse_tracefile(argvalue[0], argvalue[1]);

//  SIMULATE THE JOB-MIX FROM THE TRACEFILE, VARYING THE TIME-QUANTUM EACH TIME.
//  WE NEED TO FIND THE BEST (SHORTEST) TOTAL-PROCESS-COMPLETION-TIME
//  ACROSS EACH OF THE TIME-QUANTA BEING CONSIDERED

	int lowestCompletionTime = INT_MAX;

    for(int time_quantum=TQ0 ; time_quantum<=TQfinal ; time_quantum += TQinc) {
        simulate_job_mix(time_quantum);

		if (lowestCompletionTime >= total_process_completion_time) {
			lowestCompletionTime = total_process_completion_time;
			optimal_time_quantum = time_quantum;
		}
    }

//  PRINT THE PROGRAM'S RESULT
	printf("\n");
    printf("best %i %i\n", optimal_time_quantum, total_process_completion_time);

    exit(EXIT_SUCCESS);
}

//  vim: ts=8 sw=4
