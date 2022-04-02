#include "OperatingSystem.h"
#include "OperatingSystemBase.h"
#include "MMU.h"
#include "Processor.h"
#include "Buses.h"
#include "Heap.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

// Functions prototypes
void OperatingSystem_PCBInitialization(int, int, int, int, int);
void OperatingSystem_MoveToTheREADYState(int);
void OperatingSystem_Dispatch(int);
void OperatingSystem_RestoreContext(int);
void OperatingSystem_SaveContext(int);
void OperatingSystem_TerminateProcess();
int OperatingSystem_LongTermScheduler();
void OperatingSystem_PreemptRunningProcess();
int OperatingSystem_CreateProcess(int);
int OperatingSystem_ObtainMainMemory(int, int);
int OperatingSystem_ShortTermScheduler();
int OperatingSystem_ExtractFromReadyToRun();
void OperatingSystem_HandleException();
void OperatingSystem_HandleSystemCall();
void OperatingSystem_HandleClockInterrupt();

typedef enum {false, true} bool;

char * statesNames [5]={"NEW","READY","EXECUTING","BLOCKED","EXIT"}; 

// Ejercicio 4 V2
int numberOfClockInterrupts = 0;

// The process table
PCB processTable[PROCESSTABLEMAXSIZE];

// Address base for OS code in this version
int OS_address_base = PROCESSTABLEMAXSIZE * MAINMEMORYSECTIONSIZE;

// Identifier of the current executing process
int executingProcessID=NOPROCESS;

// Identifier of the System Idle Process
int sipID;

// Initial PID for assignation
int initialPID=PROCESSTABLEMAXSIZE-1;

// Begin indes for daemons in programList
int baseDaemonsInProgramList; 


/* // Array that contains the identifiers of the READY processes
heapItem readyToRunQueue[PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses=0; */
heapItem readyToRunQueue [NUMBEROFQUEUES][PROCESSTABLEMAXSIZE];
int numberOfReadyToRunProcesses[NUMBEROFQUEUES]={0,0};
char * queueNames [NUMBEROFQUEUES]={"USER","DAEMONS"}; 

// Variable containing the number of not terminated user processes
int numberOfNotTerminatedUserProcesses=0;

char DAEMONS_PROGRAMS_FILE[MAXIMUMLENGTH]="teachersDaemons";

// In OperatingSystem.c Exercise 5-b of V2
// Heap with blocked processes sort by when to wakeup
heapItem sleepingProcessesQueue[PROCESSTABLEMAXSIZE];
int numberOfSleepingProcesses=0; 


int nextProgram = NOPROCESS;

// Initial set of tasks of the OS
void OperatingSystem_Initialize(int daemonsIndex) {
	
	int i, selectedProcess;
	FILE *programFile; // For load Operating System Code
	programFile=fopen("OperatingSystemCode", "r");
	if (programFile==NULL){
		// Show red message "FATAL ERROR: Missing Operating System!\n"
		OperatingSystem_ShowTime(SHUTDOWN);
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing Operating System!\n");
		exit(1);		
	}

	// Obtain the memory requirements of the program
	int processSize=OperatingSystem_ObtainProgramSize(programFile);

	// Load Operating System Code
	OperatingSystem_LoadProgram(programFile, OS_address_base, processSize);
	
	// Process table initialization (all entries are free)
	for (i=0; i<PROCESSTABLEMAXSIZE;i++){
		processTable[i].busy=0;
	}
	// Initialization of the interrupt vector table of the processor
	Processor_InitializeInterruptVectorTable(OS_address_base+2);
		
	// Include in program list all system daemon processes
	OperatingSystem_PrepareDaemons(daemonsIndex);
	
	// Create all user processes from the information given in the command line
	OperatingSystem_LongTermScheduler();
	if(numberOfNotTerminatedUserProcesses==0){
		OperatingSystem_ReadyToShutdown();
	}

	
	if (strcmp(programList[processTable[sipID].programListIndex]->executableName,"SystemIdleProcess")) {
		// Show red message "FATAL ERROR: Missing SIP program!\n"
		OperatingSystem_ShowTime(SHUTDOWN);
		ComputerSystem_DebugMessage(99,SHUTDOWN,"FATAL ERROR: Missing SIP program!\n");
		exit(1);		
	}

	// At least, one user process has been created
	// Select the first process that is going to use the processor
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to the selected process
	OperatingSystem_Dispatch(selectedProcess);

	// Initial operation for Operating System
	Processor_SetPC(OS_address_base);
}

// The LTS is responsible of the admission of new processes in the system.
// Initially, it creates a process from each program specified in the 
// 			command line and daemons programs
int OperatingSystem_LongTermScheduler() {
  
	int PID, i,
		numberOfSuccessfullyCreatedProcesses=0;
	
	for (i=0; programList[i]!=NULL && i<PROGRAMSMAXNUMBER ; i++) {
		PID=OperatingSystem_CreateProcess(i);
		if(PID==NOFREEENTRY){
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(103,ERROR, programList[i]->executableName);
		}
		else if(PID==PROGRAMNOTVALID){
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104,ERROR, 
						programList[i]->executableName, "--- invalid priority or size ---]");
		}
		else if(PID==PROGRAMDOESNOTEXIST){
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(104,ERROR, 
						programList[i]->executableName, "--- it does not exist ---");
		}
		else if(PID==TOOBIGPROCESS){
			OperatingSystem_ShowTime(ERROR);
			ComputerSystem_DebugMessage(105,ERROR, programList[i]->executableName);
		}
		else{
			numberOfSuccessfullyCreatedProcesses++;
			if (programList[i]->type==USERPROGRAM) 
				numberOfNotTerminatedUserProcesses++;
			// Move process to the ready state
			OperatingSystem_MoveToTheREADYState(PID);
		}
	}
	// Exercise 7-d V2
	if(numberOfSuccessfullyCreatedProcesses>0){
		OperatingSystem_PrintStatus();
	}


	// Return the number of succesfully created processes
	return numberOfSuccessfullyCreatedProcesses;
}


// This function creates a process from an executable program
int OperatingSystem_CreateProcess(int indexOfExecutableProgram) {
  
	int PID;
	int processSize;
	int loadingPhysicalAddress;
	int priority;
	FILE *programFile;
	PROGRAMS_DATA *executableProgram=programList[indexOfExecutableProgram];

	// Obtain a process ID
	PID=OperatingSystem_ObtainAnEntryInTheProcessTable();
	if(PID==NOFREEENTRY)
	{ 
		return NOFREEENTRY;
	}

	// Check if programFile exists
	programFile=fopen(executableProgram->executableName, "r");
	if(programFile == NULL){
		return PROGRAMDOESNOTEXIST;
	}

	// Obtain the memory requirements of the program
	processSize=OperatingSystem_ObtainProgramSize(programFile);
	if(processSize == PROGRAMNOTVALID){
		return PROGRAMNOTVALID;
	}

	// Obtain the priority for the process
	priority=OperatingSystem_ObtainPriority(programFile);
	if(priority == PROGRAMNOTVALID){
		return PROGRAMNOTVALID;
	}
	
	// Obtain enough memory space
 	loadingPhysicalAddress=OperatingSystem_ObtainMainMemory(processSize, PID);
	if(loadingPhysicalAddress == TOOBIGPROCESS){
		return TOOBIGPROCESS;
	}

	// Load program in the allocated memory
	if(OperatingSystem_LoadProgram(programFile, loadingPhysicalAddress, processSize)==TOOBIGPROCESS){
		return TOOBIGPROCESS;
	}
	
	// Asigna si es un demonio del sistema o un programa de usuario
	/* if(executableProgram->type == DAEMONPROGRAM){
		queueID=DAEMONSQUEUE;
	}else{
		queueID=USERPROCESSQUEUE;
	} */
	
	// PCB initialization
	OperatingSystem_PCBInitialization(PID, loadingPhysicalAddress, processSize, priority, indexOfExecutableProgram);

	//OperatingSystem_ShowTime(INIT);
	//ComputerSystem_DebugMessage(111,INIT,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[processTable[PID].state]);
	
	// Show message "Process [PID] created from program [executableName]\n"
	OperatingSystem_ShowTime(INIT);
	ComputerSystem_DebugMessage(70,INIT,PID,executableProgram->executableName);
	
	
	return PID;
}


// Main memory is assigned in chunks. All chunks are the same size. A process
// always obtains the chunk whose position in memory is equal to the processor identifier
int OperatingSystem_ObtainMainMemory(int processSize, int PID) {

 	if (processSize>MAINMEMORYSECTIONSIZE)
		return TOOBIGPROCESS;
	
 	return PID*MAINMEMORYSECTIONSIZE;
}


// Assign initial values to all fields inside the PCB
void OperatingSystem_PCBInitialization(int PID, int initialPhysicalAddress, int processSize, int priority, int processPLIndex) {

	processTable[PID].busy=1;
	processTable[PID].initialPhysicalAddress=initialPhysicalAddress;
	processTable[PID].processSize=processSize;
	processTable[PID].state=NEW;	
	processTable[PID].priority=priority;
	processTable[PID].programListIndex=processPLIndex;


	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(111,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName,statesNames[0]);
	// Daemons run in protected mode and MMU use real address
	if (programList[processPLIndex]->type == DAEMONPROGRAM) {

		processTable[PID].queueID=DAEMONSQUEUE;

		processTable[PID].copyOfPCRegister=initialPhysicalAddress;
		processTable[PID].copyOfPSWRegister= ((unsigned int) 1) << EXECUTION_MODE_BIT;
	} 
	else {

		processTable[PID].queueID=USERPROCESSQUEUE;

		processTable[PID].copyOfPCRegister=0;
		processTable[PID].copyOfPSWRegister=0;
	}

}


// Move a process to the READY state: it will be inserted, depending on its priority, in
// a queue of identifiers of READY processes
void OperatingSystem_MoveToTheREADYState(int PID) {
	int prevState = processTable[PID].state;

	int queue;
	if(programList[processTable[PID].programListIndex]->type==USERPROGRAM){
		queue=USERPROCESSQUEUE;
	}else{
		queue=DAEMONSQUEUE;
	}

	
	if (Heap_add(PID, readyToRunQueue[queue], queue , &numberOfReadyToRunProcesses[queue] ,PROCESSTABLEMAXSIZE)>=0) {
		processTable[PID].state=READY;
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName, statesNames[prevState],statesNames[processTable[PID].state]);
		//OperatingSystem_PrintReadyToRunQueue();
	} 

}


// The STS is responsible of deciding which process to execute when specific events occur.
// It uses processes priorities to make the decission. Given that the READY queue is ordered
// depending on processes priority, the STS just selects the process in front of the READY queue
int OperatingSystem_ShortTermScheduler() {
	
	int selectedProcess;

	selectedProcess=OperatingSystem_ExtractFromReadyToRun();
	
	return selectedProcess;
}


// Return PID of more priority process in the READY queue
int OperatingSystem_ExtractFromReadyToRun() {
  
	int selectedProcess=NOPROCESS;

	
	selectedProcess=Heap_poll(readyToRunQueue[0],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[0]);
	if(selectedProcess<0){
		selectedProcess=Heap_poll(readyToRunQueue[1],QUEUE_PRIORITY ,&numberOfReadyToRunProcesses[1]);
	}
	
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}

// Return PID of more priority process in the SLEEPING queue
int OperatingSystem_ExtractFromBLOCKEDToREADY() {
  
	int selectedProcess=NOPROCESS;

	
	selectedProcess=Heap_poll(sleepingProcessesQueue,QUEUE_WAKEUP ,&numberOfSleepingProcesses);
	
	// Return most priority process or NOPROCESS if empty queue
	return selectedProcess; 
}


// Function that assigns the processor to a process
void OperatingSystem_Dispatch(int PID) {
	// Previous State
	int prevState = processTable[PID].state;

	// The process identified by PID becomes the current executing process
	executingProcessID=PID;
	// Change the process' state
	processTable[PID].state=EXECUTING;

	// Print the process state change
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110,SYSPROC,PID,programList[processTable[PID].programListIndex]->executableName, statesNames[prevState],statesNames[processTable[PID].state]);

	// Modify hardware registers with appropriate values for the process identified by PID
	OperatingSystem_RestoreContext(PID);
}


// Modify hardware registers with appropriate values for the process identified by PID
void OperatingSystem_RestoreContext(int PID) {
  
	// New values for the CPU registers are obtained from the PCB
	Processor_CopyInSystemStack(MAINMEMORYSIZE-1,processTable[PID].copyOfPCRegister);
	Processor_CopyInSystemStack(MAINMEMORYSIZE-2,processTable[PID].copyOfPSWRegister);
	Processor_SetAccumulator(processTable[PID].copyOfAccumulator);	
	
	// Same thing for the MMU registers
	MMU_SetBase(processTable[PID].initialPhysicalAddress);
	MMU_SetLimit(processTable[PID].processSize);
}


// Function invoked when the executing process leaves the CPU 
void OperatingSystem_PreemptRunningProcess() {

	// Save in the process' PCB essential values stored in hardware registers and the system stack
	OperatingSystem_SaveContext(executingProcessID);
	// Change the process' state
	OperatingSystem_MoveToTheREADYState(executingProcessID);
	// The processor is not assigned until the OS selects another process
	executingProcessID=NOPROCESS;
}


// Save in the process' PCB essential values stored in hardware registers and the system stack
void OperatingSystem_SaveContext(int PID) {
	
	// Load PC saved for interrupt manager
	processTable[PID].copyOfPCRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-1);
	
	// Load PSW saved for interrupt manager
	processTable[PID].copyOfPSWRegister=Processor_CopyFromSystemStack(MAINMEMORYSIZE-2);

	// Saves the accumulator
	processTable[PID].copyOfAccumulator = Processor_GetAccumulator();
	
}


// Exception management routine
void OperatingSystem_HandleException() {
  
	// Show message "Process [executingProcessID] has generated an exception and is terminating\n"
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(71,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
	
	OperatingSystem_TerminateProcess();
	// Exercise 7-c V2
	OperatingSystem_PrintStatus();
}


// All tasks regarding the removal of the process
void OperatingSystem_TerminateProcess() {
  
	int selectedProcess;
  	
	// Previous State
	int prevState = processTable[executingProcessID].state;

	processTable[executingProcessID].state=EXIT;

	// Print the process state change
	OperatingSystem_ShowTime(SYSPROC);
	ComputerSystem_DebugMessage(110,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName, 
								statesNames[prevState],statesNames[processTable[executingProcessID].state]);
	
	if (programList[processTable[executingProcessID].programListIndex]->type==USERPROGRAM) 
		// One more user process that has terminated
		numberOfNotTerminatedUserProcesses--;
	
	if (numberOfNotTerminatedUserProcesses==0) {
		if (executingProcessID==sipID) {
			// finishing sipID, change PC to address of OS HALT instruction
			OperatingSystem_TerminatingSIP();
			OperatingSystem_ShowTime(SHUTDOWN);
			ComputerSystem_DebugMessage(99,SHUTDOWN,"The system will shut down now...\n");
			return; // Don't dispatch any process
		}
		// Simulation must finish, telling sipID to finish
		OperatingSystem_ReadyToShutdown();
	}
	// Select the next process to execute (sipID if no more user processes)
	selectedProcess=OperatingSystem_ShortTermScheduler();

	// Assign the processor to that process
	OperatingSystem_Dispatch(selectedProcess);
}

// System call management routine
void OperatingSystem_HandleSystemCall() {
  
	int systemCallID;

	// Register A contains the identifier of the issued system call
	systemCallID=Processor_GetRegisterA();
	
	switch (systemCallID) {
		case SYSCALL_PRINTEXECPID:
			// Show message: "Process [executingProcessID] has the processor assigned\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(72,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			break;

		case SYSCALL_END:
			// Show message: "Process [executingProcessID] has requested to terminate\n"
			OperatingSystem_ShowTime(SYSPROC);
			ComputerSystem_DebugMessage(73,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName);
			OperatingSystem_TerminateProcess();
			// Exercise 7-b V2
			OperatingSystem_PrintStatus();
			break;
		case SYSCALL_YIELD:
			nextProgram = Heap_getFirst(readyToRunQueue[processTable[executingProcessID].queueID],numberOfReadyToRunProcesses[processTable[executingProcessID].queueID]);
			if((processTable[executingProcessID].priority) == (processTable[nextProgram].priority)){
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(115,SHORTTERMSCHEDULE,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,
										programList[processTable[executingProcessID + 1].programListIndex]->executableName);
				OperatingSystem_PreemptRunningProcess();
				OperatingSystem_Dispatch(OperatingSystem_ShortTermScheduler());
				// Exercise 7-a V2
				OperatingSystem_PrintStatus();
			} 
			break;
		// Exercise 5 V2
		case SYSCALL_SLEEP:
			processTable[executingProcessID].whenToWakeUp = (abs(Processor_GetAccumulator())+numberOfClockInterrupts+1);
			OperatingSystem_MoveToTheBLOCKEDState();
			OperatingSystem_Dispatch(OperatingSystem_ShortTermScheduler());
			OperatingSystem_PrintStatus();
		
	}
}
	
//	Implement interrupt logic calling appropriate interrupt handle
void OperatingSystem_InterruptLogic(int entryPoint){
	switch (entryPoint){
		case SYSCALL_BIT: // SYSCALL_BIT=2
			OperatingSystem_HandleSystemCall();
			break;
		case EXCEPTION_BIT: // EXCEPTION_BIT=6
			OperatingSystem_HandleException();
			break;
		case CLOCKINT_BIT: // CLOCKINT_BIT=9
			OperatingSystem_HandleClockInterrupt();
			break;
	}

}


void OperatingSystem_PrintReadyToRunQueue(){
	int i;
	OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
	ComputerSystem_DebugMessage(106, SHORTTERMSCHEDULE);

	for(int qCounter=0; qCounter<NUMBEROFQUEUES; qCounter++){
		ComputerSystem_DebugMessage(113, SHORTTERMSCHEDULE);
		// Cuando es la segunda cola mete una tabulación más
		if(qCounter==1){
			ComputerSystem_DebugMessage(113, SHORTTERMSCHEDULE);
		}
		ComputerSystem_DebugMessage(112, SHORTTERMSCHEDULE, queueNames[qCounter]);
	
	
	for(i=0; i<numberOfReadyToRunProcesses[qCounter];i++){
		ComputerSystem_DebugMessage(107, SHORTTERMSCHEDULE, readyToRunQueue[qCounter][i].info, processTable[readyToRunQueue[qCounter][i].info].priority);
		if(i!=numberOfReadyToRunProcesses[qCounter]-1){
			ComputerSystem_DebugMessage(108, SHORTTERMSCHEDULE);
		}
	}
	ComputerSystem_DebugMessage(109, SHORTTERMSCHEDULE);
	
	}

	
} 

//Exercise 2-b of V2
void OperatingSystem_HandleClockInterrupt(){
	numberOfClockInterrupts++;
	OperatingSystem_ShowTime(INTERRUPT);
	ComputerSystem_DebugMessage(120,INTERRUPT,numberOfClockInterrupts);

	// Modificacion ejercicio 6 V2
	OperatingSystem_CheckSleepingProcessQueue();
	

	return;
} 


void OperatingSystem_CheckSleepingProcessQueue(){
	// Comprueba mal cual es el proceso mas prioritario de la cola de procesos listos
	bool ProcessHasWokeUp = false;
	for(int i=0; i <= processTable[executingProcessID].queueID; i++){
		int AsleepProcess = Heap_poll(sleepingProcessesQueue, QUEUE_WAKEUP,  &numberOfSleepingProcesses);
		if(AsleepProcess == numberOfClockInterrupts){ 
			OperatingSystem_MoveToTheREADYState(sleepingProcessesQueue[i].info);
			//OperatingSystem_PrintStatus();
			ProcessHasWokeUp = true;
		}
	}

	if(ProcessHasWokeUp == true){
		for(int i=0; i < numberOfReadyToRunProcesses[0]; i++){
			int proceso = Heap_getFirst(readyToRunQueue[i], numberOfReadyToRunProcesses[i]);
			if(processTable[executingProcessID].priority > processTable[proceso].priority){
				OperatingSystem_ShowTime(SHORTTERMSCHEDULE);
				ComputerSystem_DebugMessage(121, SHORTTERMSCHEDULE, processTable[executingProcessID].priority, processTable[executingProcessID],
																	processTable[i].priority, processTable[i]);
				OperatingSystem_PreemptRunningProcess();
				OperatingSystem_Dispatch(i);
				OperatingSystem_PrintStatus();
			}
		}
	}
	ProcessHasWokeUp = false;
}

// Move a process to the BLOCKED state: it will be inserted, depending on its priority, in
// a queue of identifiers of BLOCKED processes
void OperatingSystem_MoveToTheBLOCKEDState() {
	if (Heap_add(executingProcessID, sleepingProcessesQueue,QUEUE_WAKEUP , &numberOfSleepingProcesses,PROCESSTABLEMAXSIZE)>=0) {
		int wtwu = abs(Processor_GetAccumulator()) + numberOfClockInterrupts + 1;
		processTable[executingProcessID].state=BLOCKED;
		processTable[executingProcessID].whenToWakeUp = wtwu;
		OperatingSystem_ShowTime(SYSPROC);
		ComputerSystem_DebugMessage(110,SYSPROC,executingProcessID,programList[processTable[executingProcessID].programListIndex]->executableName,statesNames[2],statesNames[3]);
		OperatingSystem_SaveContext(executingProcessID);
	} 
	
}

