#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_yield(void)
{
  yield();
  return 0;
}

int sys_shutdown(void)
{
  shutdown();
  return 0;
}

int sys_schedlog(void) {
  int n;

  if(argint(0, &n) < 0)
    return -1;
    
  schedlog(n);
  return 0;
}

int sys_nicefork(void) {
  int nice;

  if(argint(0, &nice) < 0)
    return -1;
  return nicefork(nice);
}

//! Temporary Sycall
int sys_skippers(void)
{
    struct SkipList* skipList = initSkipList();

    slInsert(skipList, 30, CHANCE);
    printSkipList(skipList);

    slInsert(skipList, 60, CHANCE);
    printSkipList(skipList);

    slInsert(skipList, 90, CHANCE);
    printSkipList(skipList);

    slInsert(skipList, 40, CHANCE);
    printSkipList(skipList);

    slInsert(skipList, 20, CHANCE);
    printSkipList(skipList);

    slInsert(skipList, 80, CHANCE);
    printSkipList(skipList);

    slInsert(skipList, 70, CHANCE);
    printSkipList(skipList);

    slInsert(skipList, 50, CHANCE);
    printSkipList(skipList);

    slInsert(skipList, 10, CHANCE);
    slInsert(skipList, 45, CHANCE);
    slInsert(skipList, 85, CHANCE);
    slInsert(skipList, 95, CHANCE);

    printSkipList(skipList);

    // Search for values
    struct SkipNode* result1 = slSearch(skipList, 60);
    struct SkipNode* result2 = slSearch(skipList, 20);
    struct SkipNode* result3 = slSearch(skipList, 85);
    struct SkipNode* result4 = slSearch(skipList, 100);

    struct SkipNode* results[] = {result1, result2, result3, result4};
    int values[] = {60, 20, 85, 100};

    for (int i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        if (results[i] != 0) {
            cprintf("Value %d found\n", results[i]->value);
        } else {
            cprintf("Value %d not found\n", values[i]);
        }
    }

    
    printSkipList(skipList);

    slDelete(skipList, 90);
    printSkipList(skipList);


    slDelete(skipList, 40);
    printSkipList(skipList);

    cprintf("Searching deleted node: %d\n", slSearch(skipList, 90));
    cprintf("Searching deleted node: %d\n", slSearch(skipList, 40));

    return 0;
}
