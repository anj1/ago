/* COMPILE: -Wall alib/go.c -lpthread
 */

/* a test of the lightweight thread implementation in alib_go */

/** Quick documentation:
 * Call alib_thread_init() before doing anything,
 * alib_go() to start a lightweight thread,
 * and alib_thread_end() when you're finished.
 */
 
#include <stdio.h>
#include "ago.h"

void worker(void *a)
{
	printf("Worker #%d\n", *((int *)a));
}

int main()
{
	int i;
	int r;
	int a[2048];
	
	/* the number of threads should generally be set to the number of
	 * cores in the machine. It's set to 4 here for simplicity */
	if((r=alib_thread_init(4))){
		fprintf(stderr,"Error %d in alib_thread_init\n", r);
		return r;
	}
	
	/* make lots of functions in parallel! */
	for(i=0;i<2048;i++){
		a[i]=i;
		if((r=alib_go(&worker,a+i))) fprintf(stderr,"ERROR:alib_go:%d\n",r);
	}
	
	if((r=alib_thread_wait()))fprintf(stderr,"ERROR:alib_thread_wait\n");
	if((r=alib_thread_end())) fprintf(stderr,"ERROR:alib_thread_end\n");

	printf("after wait.\n");
	
	return 0;
}
