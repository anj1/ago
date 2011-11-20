/* lightweight goroutine-like threads for C */
/* by Alireza Nejati */

/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA. 
 */

/**The way this is done is we have several idle threads that don't do
 * anything until alib_go() is called. We have a semaphore that counts
 * how many *functions* there are waiting to be called, not how many
 * *threads* that are running idle. Each time alib_go() is called, the
 * semaphore count is decreased which causes one thread to run, pop the
 * next function off the function stack, and run it. */

/* If all threads are currently busy, it waits until a thread becomes
 * free */
 
/* All accesses to the function stack are done in a mutex. Since they
 * are usually just reads or writes, they return very quickly */

/* We must provide a way for the main thread to wait until all functions
 * have been executed. This is alib_thread_wait().
 * The mechanism behind it is condition variables.
 * Now, the proper implementation of sem_getvalue() would return the
 * number of threads waiting on the semaphore. Unfortunately, in linux
 * just 0 is returned, necessitating that we keep our own variable.
 * This is nrunning. */
/* We (atomically) increment nrunning whenever a new function is
 * dispatched, and (again, atomically) whenever the function completes.
 * Thus, when it becomes 0 we are guaranteed that all threads are idle
 * and no functions are in the pipeline, waiting to be executed. */

/* TODO: make function list a queue, not a stack */
/* TODO: make alib_thread_init wait for *all* threads, not just one */

#include <pthread.h>
#include <semaphore.h>
#ifndef NO_UNISTD
#include <unistd.h>	/* usleep() */
#endif

/* absolute maximum number of threads, ever.
   Can obviously be changed later on. */
#define MAX_THREADS 1024

/* maximum number of functions. Can be changed. */
#define MAX_FUNCS 65536

/* number of currently-running threads */
int nthreads=0;

/* the quit message */
int alib_thread_quit=0;

/* list of thread pointers */
pthread_t thread_list[MAX_THREADS];

/* stack which is a list of free threads */
int free_thread[MAX_THREADS];
int nfree_thread=0;

/* queue of function pointers and associated data */
void (*func_list[MAX_FUNCS])(void*);
void *arg_list[MAX_FUNCS];
pthread_mutex_t func_mutex;
int qstart=0;
int qend=0;

/* our function list semaphore */
sem_t sem;

/* these help with alib_thread_wait */
int nrunning=0;
pthread_mutex_t nrunning_mutex;
pthread_cond_t idle_condition;

/** Idling function.
 * Designed to block (not do anything) until a function has been
 * assigned to it.
 * See description at top of file. */
void *thread_idle(void *a)
{
	void (*func)(void*);
	void *arg;
	
	/* idling loop */
	while(1){
		/* wait to be assigned to a function */
		sem_wait(&sem);
		
		/* are we running functions or quitting? */
		if(alib_thread_quit) return NULL;
		
		/* we have been assigned. Get the details of the function. */
		/* this must be done in a mutex to make sure two threads don't
		 * start to run the same function, if alib_go is called rapidly
		 * in succession */
		pthread_mutex_lock(&func_mutex);
		func = func_list[qstart];	/* get details from start of queue*/
		arg  = arg_list [qstart];	
		qstart++;
		if(qstart >= MAX_FUNCS) qstart=0;
		pthread_mutex_unlock(&func_mutex);
		
		/* now run the function */
		func(arg);
		
		/* decrement the number of running functions, and signal if the
		 * number is zero */
		pthread_mutex_lock(&nrunning_mutex);
		nrunning--;
		if(nrunning==0) pthread_cond_signal(&idle_condition);
		pthread_mutex_unlock(&nrunning_mutex);
	}
}

/**
 *  Initializes an alib_thread session. Need to call this before calling
 *  alib_go().
	max_conc: number of concurrent threads to run.
	Return value: number of actual threads created.
	Negative if error.
	Before returning, waits until all threads have been created.
	(If that isn't done, we risk posting to the semaphore before anyone
	 waits for it).
*/
int alib_thread_init(int max_conc)
{
	int i;
	int sval;
	
	/* this is for restarting */
	alib_thread_quit=0;
	
	/* alib_threads already started? In which case, must call _end() */
	if(nthreads) return -1;
	
	/* create the single semaphore that corresponds to un-run funcs */
	if(sem_init(&sem, 0, 0)) return -1;
	
	/* create mutex corresponding to access to function stack */
	if(pthread_mutex_init(&func_mutex,NULL)) return -3;
	
	/* create mutex corresponding to access to number of running funcs*/
	if(pthread_mutex_init(&nrunning_mutex,NULL)) return -4;
	if(pthread_cond_init(&idle_condition,NULL)) return -5;
	
	for(i=0;i<max_conc;i++){
		/* Create the idler threads */
		if(pthread_create(thread_list+i,NULL,&thread_idle,NULL))
			return -6;
		
		/* thread successfully created */
		nthreads++;
	}
	
	/* wait until all threads are in idling state, ready to be run. */
	/* TODO: I don't know if this code even works or not */
	while(1){
		if(sem_getvalue(&sem,&sval)) return -7;
		if(sval<=0) break;
#ifndef NO_UNISTD
		usleep(10);
#endif
	}
	
	return 0;
}

/* waits until all threads are idle */
int alib_thread_wait()
{
	while(1){
		pthread_mutex_lock(&nrunning_mutex);
		/* any jobs running? If so, wait until they are idle */
		if(nrunning>0)
			pthread_cond_wait(&idle_condition,&nrunning_mutex);
			
		/* check to make sure wake-up wasn't spurious */
		if(nrunning==0){
			pthread_mutex_unlock(&nrunning_mutex);
			return 0;
		}
		
		/* spurious wake-up. Wait again. */
		pthread_mutex_unlock(&nrunning_mutex);
#ifndef NO_UNISTD
		usleep(10);
#endif
	}
}

/** Closes up all running threads.
 * If was in the middle of running functions, wait till they end.
 * Subsequent calls to alib_go will fail.
 * Can restart again with alib_thread_init()
 * Returns 0 if successfull, otherwise error code.
 */
int alib_thread_end()
{
	int i;
	
	/* tell all threads to quit */
	alib_thread_quit=1;
	
	/* enable all threads to quit */
	for(i=0;i<nthreads;i++){
		if(sem_post(&sem)) return 1;
	}

	/* wait for all threads to quit */
	for(i=0;i<nthreads;i++){
		if(pthread_join(thread_list[i], NULL)) return 2;
	}
	
	/* clean up data structures */
	nrunning=0;
	if(sem_destroy(&sem)) return 3;
	if(pthread_mutex_destroy(&nrunning_mutex)) return 4;
	if(pthread_cond_destroy(&idle_condition)) return 5;
	
	return 0;
}

/** Execute function func in parallel.
 * Returns 0 if successfull, error otherwise
 * (maybe due to exceeding maximum thread count.)
 * */
int alib_go(void (*func)(void *), void *arg)
{
	/* have we been initialized? */
	if(!nthreads) return 1;
	
	/* too many waiting functions? */
	if(((qend+1)%MAX_FUNCS)==qstart) return 2;
	
	/* add function to queue */
	pthread_mutex_lock(&func_mutex);
	func_list[qend] = func;
	arg_list[qend]  = arg;
	qend++;
	if(qend >= MAX_FUNCS) qend=0;
	if(qend == qstart) return 3;
	pthread_mutex_unlock(&func_mutex);
	
	/* from this point on, the function is considered to be 'running',
	 * even if the thread scheduler hasn't run it yet. */
	/* increment the number of running functions */
	/* it is important to do this here and not in the thread, as this
	 * is guaranteed to complete before alib_go returns */
	pthread_mutex_lock(&nrunning_mutex);
	nrunning++;
	pthread_mutex_unlock(&nrunning_mutex);
		
	/* now allow one thread to be released and run it */
	if(sem_post(&sem)) return 4;
	
	return 0;
}
