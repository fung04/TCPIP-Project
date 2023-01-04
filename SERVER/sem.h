#include<sys/types.h>
#include<sys/ipc.h>
#include<sys/sem.h>
#include<unistd.h>
#include<errno.h>
#include<semaphore.h>

#define SEMPERM 0600
#define TRUE 1
#define FALSE 0


#ifndef sem_H
#define sem_H

int initsem (key_t semkey);
int p(int semaphore);
int v (int semaphore);

#endif

typedef union _semun
{
	int val;
	struct semid_ds *buf;
	ushort *array;
 } semun;

int initsem (key_t semkey){
    int status = 0, semid;
    semun arg;
    
    // Create the semaphore
    if((semid = semget(semkey,1,SEMPERM|IPC_CREAT|IPC_EXCL))==-1){
        if(errno == EEXIST)
        semid = semget (semkey, 1,0);
    }
    else{
        arg.val = 1;
        status = semctl (semid, 0, SETVAL, arg);
    }
    
    // Check for errors
    if (semid == -1 || status == -1){
        perror("Initsem() fails");
        return (-1);
    }
    
    return (semid);
}

int p(int sem_id){
    struct sembuf p_buf;
    p_buf.sem_num = 0;  /* this set only contains 1 sem_id */
    p_buf.sem_op = -1;  /* wait for the sem_id value to become greater than or equal to |-1| that is 1. When sem_id val=1, decrement it */
    p_buf.sem_flg = SEM_UNDO;
    
    printf("Log: p() called (sem_id=%d), waiting for sem_id to become 1\n", sem_id);
	if ( semop (sem_id, &p_buf, 1) == -1){
        perror("p () fails");
        exit(1);
    }
    return(0);
}

int v (int sem_id){
    struct sembuf v_buf;
    v_buf.sem_num = 0; ; /* this set only contains 1 sem_id */
    v_buf.sem_op = 1;   /* the sem_id value is incremented by 1, and release the resource */
    v_buf.sem_flg = SEM_UNDO; /* undo all the operations on sem_id when process exits*/
    
	printf("Log: v() called (sem_id=%d), releasing the resource\n", sem_id);
    if ( semop (sem_id, &v_buf, 1) == -1){
        perror("v (semid) fails");
        exit(1);
    }
    return (0);
}