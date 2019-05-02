#define _GNU_SOURCE
#include "process.h"
#include "scheduler.h"
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sched.h>
#include "error.h"

/* other direction */
#define HEAP_INITIAL_CAPACITY 32
struct heap {
        size_t data_size;

        void **data;
        size_t size, capacity;
};
static inline int heap_create(struct heap *heap, size_t data_size)
{
        heap->data_size = data_size;
        // heap->comp = comp;
        heap->size = 0;
        heap->capacity = HEAP_INITIAL_CAPACITY;

        /* heap->data[0] is not used */
        if ((heap->data = malloc((heap->capacity + 1) * sizeof(void *))) == NULL)
                return -1;

        return 0;
}
static inline void heap_destroy(struct heap *heap)
{
        size_t i;

        for (i = 1; i < heap->size + 1; ++i)
                free(heap->data[i]);
        free(heap->data);
}


//------------------------------------------------------------------------------

/* Last context switch time for RR scheduling */
static int RR_last_time;

/* Initial scheduler */
/* Current unit time */
static int current_time = 0;

/* Index of running process. -1 if no process running */
static int running = -1; // the process which is running in CPU

/* Number of finish Process */
static int finished_proc_num = 0;


////////////////////////////////////////////////////////////////////////////////////////
/* Return index of next process  */
int FIFO_next_proc(Process *proc, int process_num, int policy)
{
	/* Non-preemptive */
	if (running != -1)
		return running;

	int result = -1;

    if (policy == FIFO) {
		for(int i = 0; i < process_num; i++) {
			if(proc[i].pid == -1 || proc[i].exec_time == 0)
				continue;
			if(result == -1 || proc[i].ready_time < proc[result].ready_time)
				result = i;
		}
    }
	return result;
}
int RR_next_process(Process *proc, int process_num, int policy)
{
	int result = -1;
	int quantum = 500;
	if (policy == RR) 
    {
		if (running == -1) {
			for (int i = 0; i < process_num; i++) {
				if (proc[i].pid != -1 && proc[i].exec_time > 0){
					result = i;
					break;
				}
			}
		}
		else if ((current_time - RR_last_time) % quantum == 0)  {
			result = (running + 1) % process_num;
			while (proc[result].pid == -1 || proc[result].exec_time == 0)
				result = (result + 1) % process_num;
		}
		else
			result = running;
	}
	return result;
}
int SJF_next_process(Process *proc, int process_num, int policy)
{
	/* Non-preemptive */
	if (running != -1 && policy == SJF)
		return running;

	int result = -1;

	/* SJF: cpu idle , or PSJF: Preemptive */
	if (policy == PSJF || policy ==  SJF) 
    {
		for (int i = 0; i < process_num; i++) 
        {
			/* if process not ready or has finished , see next one*/
			if (proc[i].pid == -1 || proc[i].exec_time == 0)
				continue;
			/* if find process which has shorter execution time */
			if (result == -1 || proc[i].exec_time < proc[result].exec_time)
				result = i;
		}
	}
	return result;

}
/////////////////////////////////////////////////////////////////////////////////////////////////

int scheduling(Process *proc, int process_num, int policy)
{
	printf("Process id:\n");

	/* Initial pid = -1 imply not ready */
	for (int i = 0; i < process_num; i++)
		proc[i].pid = -1;
	/* Set Parent process to single core in order to prevent from preemption */
	assign_CPU(getpid(), PARENT_CPU);
	
	/* Set Parent process a high priority to scheduler. let it use cpu 
		and then we will send all child processes to cpu for exectuing */
	proc_wakeup(getpid());
	

	/* start to execute child process */

	
	while(1) 
	{
		//fprintf(stderr, "Current time: %d\n", current_time);

		/* Check if running process finish */

		/* one process is in CPU but has "finished" */
		if (running != -1 && proc[running].exec_time == 0) 
		{
		
#ifdef DEBUG
			printf( "%s finish at time %d.\n", proc[running].name, current_time);
#endif
			//add
			proc[running].finish_time = current_time;

			/* wait for child process */
			waitpid(proc[running].pid, NULL, 0); // wait the child process until finish.
			printf("%s %d\n", proc[running].name, proc[running].pid);
			running = -1;
			finished_proc_num += 1;

			/* if All process finish */
			if (finished_proc_num == process_num)
				break;
		}

		/* Check if process ready and then execute to print in command "dmesg" */
		for (int i = 0; i < process_num; i++) 
		{
			if (proc[i].ready_time == current_time) 
			{
				/* execute for a unit and then stop to see whether need to context switch */
				proc[i].pid = process_execute(proc[i]); //if ready, pid != -1
				proc_block(proc[i].pid);
#ifdef DEBUG
				printf( "%s ready at time %d.\n", proc[i].name, current_time);
#endif
			}

		}

		/* Select next running  process */
		// int next_proc = next_process(proc, process_num, policy);
		int next_proc = -1;
        if(policy == FIFO)
        {
            next_proc = FIFO_next_proc(proc, process_num, policy);
        }
        else if(policy == RR)
        {
            next_proc = RR_next_process(proc , process_num , policy);
        }
        else
        {
            next_proc = SJF_next_process(proc , process_num , policy);
        }

		/* if there is a better choice of process under this scheduling policy */
		/* if the policy is preemptive, do context switch here */
		if (next_proc != -1) 
		{
			/* Context switch */
			if (running != next_proc) 
			{
				proc_wakeup(proc[next_proc].pid);
				proc_block(proc[running].pid);
				running = next_proc;
				RR_last_time = current_time;
			}
		}

		/* Run an unit of time */
		exec_unit_time();
		if (running != -1)
		{
			proc[running].exec_time -= 1; //process execution time --
			//add. record start time.
			if(proc[running].Have_used_CPU == 0)
			{
				proc[running].Have_used_CPU = 1;
				proc[running].start_time = current_time;
			}

		}

		current_time += 1; /* current time ++ , go through whole schedule flow at each time. */
	}

	/* print schedule result */
	printf("\nresult:\n");
	printf("%9s %6s\n","start","end");
	for(int i = 0 ; i < process_num ; i++)
	{
		printf("%s %6d %6d\n",proc[i].name , proc[i].start_time , proc[i].finish_time);
	}
	printf("\n");

	return 0;
}
