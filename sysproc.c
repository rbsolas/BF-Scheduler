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

    /// TEST INSERT ///
    cprintf("TEST INSERT\n");

    slInsert(skipList, 30, 0, CHANCE);
    //printSkipList(skipList);

    slInsert(skipList, 60, 1, CHANCE);
    //printSkipList(skipList);

    slInsert(skipList, 90, 2, CHANCE);
    //printSkipList(skipList);

    slInsert(skipList, 40, 3, CHANCE);
    //printSkipList(skipList);

    slInsert(skipList, 20, 4, CHANCE);
    //printSkipList(skipList);

    slInsert(skipList, 80, 5, CHANCE);
    //printSkipList(skipList);

    slInsert(skipList, 70, 6, CHANCE);
    //printSkipList(skipList);

    slInsert(skipList, 50, 7, CHANCE);
    //printSkipList(skipList);

    slInsert(skipList, 10, 8, CHANCE);
    slInsert(skipList, 45, 9, CHANCE);
    slInsert(skipList, 85, 10, CHANCE);
    slInsert(skipList, 95, 11, CHANCE);

    printSkipList(skipList);

    // Search for values
    /*
    struct SkipNode* result1 = slSearch(skipList, 0, 0); // Not in list
    struct SkipNode* result2 = slSearch(skipList, 20, 4);
    struct SkipNode* result3 = slSearch(skipList, 85, 10);
    struct SkipNode* result4 = slSearch(skipList, 100, 11); // Not in list

    struct SkipNode* results[] = {result1, result2, result3, result4};
    int values[] = {0, 20, 85, 100};

    for (int i = 0; i < sizeof(results) / sizeof(results[0]); i++) {
        if (results[i] != 0) {
            cprintf("Value %d found\n", results[i]->value);
        } else {
            cprintf("Value %d not found\n", values[i]);
        }
    }
    */

    /// TEST INSERT DUPLICATE PID ///
    cprintf("\n");
    cprintf("TEST INSERT DUPLICATE PID\n");

    slInsert(skipList, 69, 0, CHANCE); // Should fail; Can't catch yet, possibility to skip over for diff values
    slInsert(skipList, 69, 11, CHANCE); // Should fail; Can't catch yet, possibility to skip over for diff values

    /// TEST DELETE ///
    cprintf("\n");
    cprintf("TEST DELETE\n");
    slDelete(skipList, 90, 2);
    printSkipList(skipList);

    slDelete(skipList, 20, 4);
    printSkipList(skipList);

    slDelete(skipList, 10, 8);
    printSkipList(skipList);

    /// TEST DELETE NONEXISTENT NODE ///
    cprintf("\n");
    cprintf("TEST DELETE NONEXISTENT NODE\n");
    slDelete(skipList, 20, 4); // Should fail

    /// TEST SEARCH ///
    cprintf("\n");
    cprintf("TEST SEARCH\n");
    cprintf("Searching node: %d\n", slSearch(skipList, 85, 10));

    /// TEST SEARCH DELETED NODE ///
    cprintf("\n");
    cprintf("TEST SEARCH DELETED NODE\n");
    cprintf("Searching node: %d\n", slSearch(skipList, 20, 4)); // Not in list; Should return 0

    /// TEST SEARCH NONEXISTENT NODE /// 
    cprintf("\n");
    cprintf("TEST SEARCH NONEXISTENT NODE\n");
    cprintf("Searching node: %d\n", slSearch(skipList, 1, 3)); // Not in list (different vdeadline); Should return 0
    cprintf("Searching node: %d\n", slSearch(skipList, 100, 11)); // Not in list; Should return 0


    /// TEST REINSERT PREVIOUSLY DELETED PID ///
    cprintf("\n");
    cprintf("TEST REINSERT\n");
    slInsert(skipList, 90, 2, CHANCE);
    printSkipList(skipList);

    return 0;
}
