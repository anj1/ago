/* COMPILE: -Wall alib/go.c -lpthread
 */

/* a test of the lightweight thread implementation in alib_go */

#include <stdio.h>
#include "ago.h"

void worker(void *a)
{
	printf("Worker #%d\n", *((int *)a));
}

int main()
{
	int r;
	int a[4];
	
	if((r=alib_thread_init(4))){
		fprintf(stderr,"Error %d in alib_thread_init\n", r);
		return r;
	}
	
	a[0]=1;
	if((r=alib_go(&worker,a+0))) fprintf(stderr,"ERROR:alib_go:%d\n",r);
	
	a[1]=2;
	alib_go(&worker,a+1);
	
	a[2]=3;
	alib_go(&worker,a+2);
	
	a[3]=4;
	alib_go(&worker,a+3);
	
	if((r=alib_thread_wait()))fprintf(stderr,"ERROR:alib_thread_wait\n");
	if((r=alib_thread_end())) fprintf(stderr,"ERROR:alib_thread_end\n");

	return 0;
}
