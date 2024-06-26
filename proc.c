#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

// SETTING ANY OF THESE LINES TO 1 WILL SHOW DEBUG PRINT STATEMENTS
#define SKIPLIST_DBG_LINES 0
#define SCHEDULER_DBG_LINES 0
#define YIELD_DBG_LINES 0
#define BFS_PRINT 1
#define NICEFORK_DBG_LINES 0

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // BFS NEW VALS
  p->niceness = 0;
  p->vdeadline = ticks + PRIO_RATIO(p->niceness) * BFS_DEFAULT_QUANTUM;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

int populateNewProc(struct proc *np, struct proc *curproc) {


  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  return nicefork(0);
}

// behaves like fork except that it also sets the nice value of the child process to the integer argument passed.
int nicefork(int nice) {
  if (nice < BFS_NICE_FIRST_LEVEL || nice > BFS_NICE_LAST_LEVEL) return -1;

  int pid, i;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  // Update Niceness & Virtual Deadline
  np->niceness = nice;
  np->vdeadline = ticks + PRIO_RATIO(np->niceness) * BFS_DEFAULT_QUANTUM;

  dbgprintf(NICEFORK_DBG_LINES, "PID %d; niceness: %d, prioratio: %d, vdl: %d\n", np->pid, np->niceness, PRIO_RATIO(np->niceness), np->vdeadline);

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

int schedlog_active = 0;
int schedlog_lasttick = 0;

void schedlog(int n) {
  schedlog_active = 1;
  schedlog_lasttick = ticks + n;
}

struct SkipList sl = {
    .level = -1
};

void
scheduler(void)
{
  initSkipList();
  
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    dbgprintf(SCHEDULER_DBG_LINES, "-----------------------------------------\n\n");

    // Enable interrupts on this processor.
    sti();

    acquire(&ptable.lock);

    // Populate/Update skip list with runnable processes
    // Choose process to schedule
    // Update vdeadline if a process exits or consumes its quantum

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      switch (p->state) {
        case UNUSED:
          continue;
        case RUNNABLE:
          dbgprintf(SCHEDULER_DBG_LINES, "[%d] %d\n", p->pid, p->state);
          // ADD RUNNABLE PROCS TO SKIPLIST
          // We assume that possible duplicate would already be invalid upon slInsert
          if (slSearch(p->vdeadline, p->pid) == 0) 
            slInsert(p->vdeadline, p->pid, CHANCE);
            // ! Add a case if slInsert fails?
          break;
        default:
          dbgprintf(SCHEDULER_DBG_LINES, "[%d] %d\n", p->pid, p->state);
          // DELETE NON RUNNABLE PROCS FROM SKIPLIST
          if (slSearch(p->vdeadline, p->pid) != 0) { // Non-runnable procs that are in skip list
            slDelete(p->vdeadline, p->pid);
          }
          break;
      }
    }

    if (SCHEDULER_DBG_LINES) printSkipList();
    
    struct SkipNode* head = &sl.nodeList[0];
    struct SkipNode* firstNode = &sl.nodeList[head->forward[0]]; // First node (pointed to after head node)

    if (firstNode->valid == 1 && (head->forward[0] > 0 && head->forward[0] < NPROC + 1)) {
      dbgprintf(SCHEDULER_DBG_LINES, "HEAD valid: %d, value: %d, forward: %d\n", head->valid, head->value, head->forward[0]);
      dbgprintf(SCHEDULER_DBG_LINES, "FIRSTNODE valid: %d, pid: %d, vdeadline: %d\n", firstNode->valid,  firstNode->pid, firstNode->value);

      struct proc* nextProc = &ptable.proc[firstNode->pid - 1]; // PID n corresponds to index n - 1

      // Delete the next proc from skiplist
      slDelete(firstNode->value, firstNode->pid);

      for (p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->pid == firstNode->pid) nextProc = p;
      }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = nextProc;
      switchuvm(nextProc);
      nextProc->state = RUNNING;
      if (nextProc->ticks_left <= 0) nextProc->ticks_left = BFS_DEFAULT_QUANTUM;
      nextProc->maxlevel = firstNode->maxlevel;

      dbgprintf(SCHEDULER_DBG_LINES, "NEXTPROC pid: %d, nice: %d, vdeadline: %d, ticks left: %d\n", nextProc->pid,  nextProc->niceness, nextProc->vdeadline, nextProc->ticks_left);

      if (schedlog_active) {
        if (ticks > schedlog_lasttick) {
          schedlog_active = 0;
        } else {
          cprintf("%d|", ticks);

          struct proc *pp;
          int highest_idx = -1;

          for (int k = 0; k < NPROC; k++) {
            pp = &ptable.proc[k];
            if (pp->state != UNUSED) {
              highest_idx = k;
            }
          }

          for (int k = 0; k <= highest_idx; k++) {
            pp = &ptable.proc[k];
            // Reference: <tick>|[<PID>]<process name>:<state>:<nice>(<maxlevel>)(<deadline>)(<quantum>)
            switch (pp->state) {
              case UNUSED:
                cprintf("[-]---:0:-(-)(-)(-)");
                break;
              default:
                int maxlevel = pp->maxlevel;
                if (slSearch(pp->vdeadline, pp->pid) == 0) {
                  maxlevel = -1;
                }
                cprintf("[%d]%s:%d:%d(%d)(%d)(%d)", pp->pid, pp->name, pp->state, pp->niceness, maxlevel, pp->vdeadline, pp->ticks_left);
                break;
            }
            if (k != highest_idx) cprintf(",");
          }
          cprintf("\n");
        }
      }

      swtch(&(c->scheduler), nextProc->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  dbgprintf(YIELD_DBG_LINES, "[%d] Ticks Left: %d\n", myproc()->pid, myproc()->ticks_left);
  // Update vdeadline
  if (myproc()->ticks_left <= 0) {
    dbgprintf(YIELD_DBG_LINES, "[%d] QUANTUM CONSUMED, UPDATE VDEADLINE\n", myproc()->pid);
    myproc()->vdeadline = ticks + PRIO_RATIO(myproc()->niceness) * BFS_DEFAULT_QUANTUM;
  }

  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

// Function to initialize a new sorted skip list
void initSkipList() { // struct SkipList* skipList
  sl.level = 0;

  struct SkipNode* head = &sl.nodeList[0];

  // Initialize head node kept at index 0
  head->valid = 1;   // Valid bit for sentinel should always be true
  head->value = -1;
  head->pid = -1; // All actual processes have positive PIDs
  head->maxlevel = MAX_SKIPLIST_LEVEL;

  // Sentinel is alone and sad, no forward neighbors
  for(int i = 0; i < MAX_SKIPLIST_LEVEL; i++) { 
    head->forward[i] = -1;
    head->backward[i] = -1;
  }

  // Set valid bit for all other nodes to 0 (empty list)
  for (int i = 1; i <= NPROC; i++) {
    sl.nodeList[i].valid = 0;
    sl.nodeList[i].value = -1;
    sl.nodeList[i].pid = -1;
    for (int j = 0; j < MAX_SKIPLIST_LEVEL; j++){
      sl.nodeList[i].forward[j] = -1;
      sl.nodeList[i].backward[j] = -1;
    }
  }
}

unsigned int seed = SEED;
unsigned int random(unsigned int max) {
  seed ^= seed << 17;
  seed ^= seed >> 7;
  seed ^= seed << 5;
  return seed % max;
}

// Function to up a level by chance for a new element
int slUpLevel(float p) {
  int level = 0;
  while ((random(100) / 100.0) < p && level < MAX_SKIPLIST_LEVEL - 1) {
      level++;
  }
  return level;
}

int slFindFreeNode() {
  int i;

  for (i = 1; i < NPROC + 1; i++) {
    if (sl.nodeList[i].valid != 1) {
      break;
    }

    if (i == NPROC) return -1;
  }

  return i;
}

// Function to insert a value into the sorted skip list
int slInsert(int value, int pid, float p) {
  if (sl.level == -1) return -1;

  struct SkipNode* backNodesToUpdate[4]; // Temp array to store copies of a node
  int backNodesIdxToUpdate[4];
  dbgprintf(SKIPLIST_DBG_LINES, "[INSERT] Inserting PID %d with vdeadline %d.\n", pid, value);

  //* 1 - FIND THE NODE TO INSERT NEW NODE AT
  // ----------------------------------------------------
  int nodeIdxToInsert = 0;
  struct SkipNode* nodeToInsert = &sl.nodeList[nodeIdxToInsert];

  for (int i = sl.level; i >= 0; i--) {
    while (nodeToInsert->forward[i] != -1
          && sl.nodeList[nodeToInsert->forward[i]].valid != 0
          && sl.nodeList[nodeToInsert->forward[i]].value < value) {
      nodeIdxToInsert = nodeToInsert->forward[i];
      nodeToInsert = &sl.nodeList[nodeIdxToInsert];
    }
    
    backNodesIdxToUpdate[i] = nodeIdxToInsert;
    backNodesToUpdate[i] = nodeToInsert;
    dbgprintf(SKIPLIST_DBG_LINES, "[INSERT] Reached the rightmost node at level %d (Current node: PID %d with vdeadline %d)\n", i, nodeToInsert->pid, nodeToInsert->value);
  }

  //* 2 - CHANCE FOR NODE TO BE INSERTED TO NEXT LEVEL
  // ----------------------------------------------------

  int newLevel = slUpLevel(p);

  dbgprintf(SKIPLIST_DBG_LINES, "[INSERT] slUpLevel = %d\n", newLevel);

  if (newLevel > sl.level) { // If new level is greater than the max level of the whole skip list, update max level
      for (int i = sl.level + 1; i <= newLevel; i++) {
          backNodesIdxToUpdate[i] = 0;
          backNodesToUpdate[i] = &sl.nodeList[backNodesIdxToUpdate[i]];
      }
      sl.level = newLevel;
      dbgprintf(SKIPLIST_DBG_LINES, "[INSERT] Increased skip list level to %d\n", sl.level);
  }

  //* 3 - LOOK FOR NEAREST ARRAY ELEMENT TO PLACE NODE
  // ----------------------------------------------------
  int newNodeIdx = slFindFreeNode();

  if (newNodeIdx == -1) { 
    dbgprintf(SKIPLIST_DBG_LINES, "[INSERT] Insert Failed. Not enough array space.\n");
    return -1;
  }

  struct SkipNode* newNode = &sl.nodeList[newNodeIdx];

  //* 4 - CREATE NEW NODE
  // ----------------------------------------------------

  newNode->value = value;
  newNode->pid = pid;
  newNode->valid = 1;
  newNode->maxlevel = newLevel;

  //* 5 - UPDATE LINKS (OF NEW NODE, NEW BACKWARD, AND NEW FORWARD)
  // ----------------------------------------------------

  for (int i = 0; i <= newNode->maxlevel; i++) {
    struct SkipNode* frontNodeToUpdate;

    if (backNodesToUpdate[i]->forward[i] != -1 && sl.nodeList[backNodesToUpdate[i]->forward[i]].valid == 1) {
      frontNodeToUpdate = &sl.nodeList[backNodesToUpdate[i]->forward[i]];

      // newNode's Forward
      newNode->forward[i] = backNodesToUpdate[i]->forward[i];

      // frontNodes' Backwards should point to newNode
      frontNodeToUpdate->backward[i] = newNodeIdx;
    } else {
      newNode->forward[i] = -1;
    }
    
    // newNode's Backward
    newNode->backward[i] = backNodesIdxToUpdate[i];

    // backNodes' Forwards should point to newNode
    backNodesToUpdate[i]->forward[i] = newNodeIdx;

    dbgprintf(SKIPLIST_DBG_LINES, "[INSERT] Inserted at level %d (FrontNode vdeadline: %d. BackNode vdeadline: %d)\n", i, sl.nodeList[newNode->forward[i]].value, sl.nodeList[newNode->backward[i]].value);
  }

  dbgprintf(SKIPLIST_DBG_LINES, "[INSERT] Insert PID %d with vdeadline %d Successful.\n", pid, value);
  
  // BFSPRINT
  dbgprintf(BFS_PRINT, "inserted|[%d]%d\n", newNode->pid, newNode->maxlevel);
  return 0;
}

// Function to search for a value in the sorted skip list
struct SkipNode* slSearch(int value, int pid) {
  if (sl.level == -1) return 0;

  dbgprintf(SKIPLIST_DBG_LINES, "[SEARCH] Searching for PID %d with vdeadline %d\n", pid, value);
  struct SkipNode* currentNode = &sl.nodeList[0];

  //* 1 - LOOP THROUGH SKIPLIST UNTIL WE FIND VALUE RIGHT BEFORE TARGET VALUE
  // ----------------------------------------------------

  for (int i = sl.level; i >= 0; i--) {
    dbgprintf(SKIPLIST_DBG_LINES, "[SEARCH] Checking level %d (Current node: PID %d with vdeadline)\n", i, currentNode->pid, currentNode->value);

      // Keep Looping WHILE:
      //    link to next forward exists (!= -1)
      //    linked forward node is valid (!= 0)
      //    value for forward is less than the searched value
    while (currentNode->forward[i] != -1
          && sl.nodeList[currentNode->forward[i]].valid != 0
          && sl.nodeList[currentNode->forward[i]].value < value) {
      currentNode = &sl.nodeList[currentNode->forward[i]];
      dbgprintf(SKIPLIST_DBG_LINES, "[SEARCH] Moving right at level %d (Current node: PID %d with vdeadline %d)\n", i, sl.nodeList[currentNode->forward[i]].pid, sl.nodeList[currentNode->forward[i]].value);
    }
  }

  //* 2 - CHECK THROUGH BOTTOMMOST LEVEL FOR ANY DUPLICATES
  // ----------------------------------------------------

  // Check bottommost level
  while (currentNode->forward[0] != -1
        && sl.nodeList[currentNode->forward[0]].valid != 0
        && sl.nodeList[currentNode->forward[0]].value == value) {
    currentNode = &sl.nodeList[currentNode->forward[0]];
    dbgprintf(SKIPLIST_DBG_LINES, "[SEARCH] Moving right at bottom level (Current node: PID %d with vdeadline %d)\n", sl.nodeList[currentNode->forward[0]].pid, sl.nodeList[currentNode->forward[0]].value);
    

    if (currentNode->pid == pid) {
      dbgprintf(SKIPLIST_DBG_LINES, "[SEARCH] PID %d with vdeadline %d found.\n", pid, value);
      return currentNode;
    }
  }

  dbgprintf(SKIPLIST_DBG_LINES, "[SEARCH] PID %d with vdeadline %d not found.\n", pid, value);
  return 0;
}

// Function to delete a node in the skip list
struct SkipNode* slDelete(int value, int pid) {
  if (sl.level == -1) return 0;

  //* 1 - LOOK FOR TARGET VALUE
  // ----------------------------------------------------

  dbgprintf(SKIPLIST_DBG_LINES, "[DELETE] Deleting PID %d with vdeadline %d:\n", pid, value);
  struct SkipNode* nodeToDelete = slSearch(value, pid);

  if (nodeToDelete == 0) {
    dbgprintf(SKIPLIST_DBG_LINES, "[DELETE] PID %d with vdeadline %d to delete not found\n", pid, value);
    return 0;
  } 

  //* 2 - UPDATE LINKS (OF BACKWARD AND FRONT NODES)
  // ----------------------------------------------------

  for (int i = nodeToDelete->maxlevel; i >= 0; i--) {
    struct SkipNode* backNode = &sl.nodeList[nodeToDelete->backward[i]];

    if (nodeToDelete->forward[i] != -1) {
      backNode->forward[i] = nodeToDelete->forward[i]; // BackNode's Forward should point to Forward

      struct SkipNode* frontNode = &sl.nodeList[nodeToDelete->forward[i]];
      frontNode->backward[i] = nodeToDelete->backward[i]; // FrontNode's Backward should point to Backward
    } else {
      backNode->forward[i] = -1;
    }

    nodeToDelete->forward[i] = -1;
    nodeToDelete->backward[i] = -1;
  }

  dbgprintf(SKIPLIST_DBG_LINES, "[DELETE] Deletion of PID %d with vdeadline %d Successful.\n", pid, value);
  nodeToDelete->valid = 0;

  // BFSPRINT
  dbgprintf(BFS_PRINT, "removed|[%d]%d\n", nodeToDelete->pid, nodeToDelete->maxlevel);
  return nodeToDelete;
}

// Function to print the entire skip list
void printSkipList() {
  if (sl.level == -1) return;

  cprintf("Skip List:\n");
  for (int i = sl.level; i >= 0; i--) {
      struct SkipNode* head = &sl.nodeList[0];
      int currentIdx = head->forward[i];
      struct SkipNode* current = &sl.nodeList[currentIdx];
      cprintf("Level %d: ", i);
      while (current->valid == 1 && current->value != 0) {
          cprintf("(%d) %d [idx: %d]-> ", current->pid, current->value, currentIdx);
          currentIdx = current->forward[i];
          current = &sl.nodeList[currentIdx];
      }
      cprintf("0\n");
  }
}
