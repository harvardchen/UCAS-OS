#include "string.h"
#include "mailbox.h"
#include "syscall.h"

// #define MAX_NUM_BOX 32

// static mailbox_t mboxs[MAX_NUM_BOX];
mailbox_t mboxs[MAX_NUM_BOX];

void mbox_init()
{
    int i = 0;
    for(; i < MAX_NUM_BOX; i++){
        mboxs[i].name[0] = 0;
        mboxs[i].count = 0;
        mutex_lock_init(&(mboxs[i].lock));
    }
}

mailbox_t *mbox_open(char *name)
{
    int i = 0;
    for(; i < MAX_NUM_BOX; i++){
        if(strcmp(name, mboxs[i].name)==0 && mboxs[i].count > 0){
            mboxs[i].count++;
            return &(mboxs[i]);
        }
    }
    for(i = 0; i < MAX_NUM_BOX; i++){
        if(mboxs[i].count == 0){
        //     int j = 0;
        //     while(Lock[j] != 0){
        //         j++;
        //     }
        //     Lock[j] = &(mboxs[i].lock);
            mutex_lock_init(&(mboxs[i].lock));
            memcpy(mboxs[i].name, name, strlen(name)+1);
            mboxs[i].count++;
            mboxs[i].ptr = 0;
            semaphore_init(&mboxs[i].send, MAX_MBOX_LENGTH);
            semaphore_init(&mboxs[i].recv, 0);
            return &(mboxs[i]);
        }
    }
}

void mbox_close(mailbox_t *mailbox)
{
    mailbox->count--;
}

void mbox_send(mailbox_t *mailbox, void *msg, int msg_length)
{
    //mutex_lock_acquire(&(mailbox->lock));
    semaphore_down(&(mailbox->send));
    mutex_lock_acquire(&(mailbox->lock));
    mailbox->ptr = (mailbox->ptr + 1) % MAX_MBOX_LENGTH;
    memcpy(&(mailbox->msg[mailbox->ptr]), msg, msg_length);
    mutex_lock_release(&(mailbox->lock));
    semaphore_up(&(mailbox->recv));
    //mutex_lock_release(&(mailbox->lock));
}

void mbox_recv(mailbox_t *mailbox, void *msg, int msg_length)
{
    //mutex_lock_acquire(&(mailbox->lock));
    semaphore_down(&(mailbox->recv));
    mutex_lock_acquire(&(mailbox->lock));
    memcpy(msg, &(mailbox->msg[mailbox->ptr]), msg_length);
    mailbox->ptr = (mailbox->ptr + MAX_MBOX_LENGTH - 1) % MAX_MBOX_LENGTH;   
    mutex_lock_release(&(mailbox->lock));
    semaphore_up(&(mailbox->send)); 
    //mutex_lock_release(&(mailbox->lock));
}
