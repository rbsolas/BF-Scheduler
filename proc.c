#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

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

  // TODO: NEED TO UPDATE VDEADLINE WHEN QUANTUM IS FINISHED
  int prioratio = 1;
  if (prioratio != BFS_NICE_FIRST_LEVEL) prioratio = p->niceness;

  p->vdeadline = ticks + prioratio * BFS_DEFAULT_QUANTUM;

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
  int i;

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

  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  if (populateNewProc(np, curproc) == -1) {
    return -1;
  }

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// behaves like fork except that it also sets the nice value of the child process to the integer argument passed.
int nicefork(int nice) {
  int pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  if (populateNewProc(np, curproc) == -1) {
    return -1;
  }

  np->niceness = nice;

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


int min_vdeadline = MAX_INT;
struct SkipList *sl = 0;

void
scheduler(void)
{
  sl = initSkipList(sl); // not tested

  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
      /*
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
    */
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if (p->pid == 0) { // null (empty) processes
        continue;
      }
      
      if (p->state != RUNNABLE) {
        slDelete(sl, p->vdeadline, p->pid);
        continue;
      }
      
      if (slSearch(sl, p->vdeadline, p->pid) == 0) { // Process is runnable and not yet in skip list
        if (p->vdeadline < min_vdeadline) min_vdeadline = p->vdeadline;
        if (p->ticks_left == 0) p->vdeadline = ticks + ~~~prioRatio[p->niceness]~~~ * BFS_DEFAULT_QUANTUM; // not yet working; prioratio not yet seen
        slInsert(sl, p->vdeadline, p->pid, CHANCE);
      }
    }

    struct SkipNode procToSched = sl->nodeList[1]; // first node
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if (procToSched.pid == p->pid) {
        c->proc = p;
        break;
      }
    }

    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    // c->proc = p;
    switchuvm(p);
    p->state = RUNNING;

    p->ticks_left = BFS_DEFAULT_QUANTUM;

    if (schedlog_active) {
      if (ticks > schedlog_lasttick) {
        schedlog_active = 0;
      } else {
          cprintf("%d", ticks);

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
          cprintf(" | [%d] %s:%d: <n>(<l>)(<dl>)(<q>)", k, pp->name, pp->state); // NOTE: REMOVE SPACES

          /*
          if (pp->state == UNUSED) cprintf(" | [%d] ---:0", k);
          else if (pp->state == RUNNING) cprintf(" | [%d]*%s:%d", k, pp->name, pp->state);
          else cprintf(" | [%d] %s:%d", k, pp->name, pp->state);
          */
        }
        cprintf("\n");
      }
    }

    swtch(&(c->scheduler), p->context);
    switchkvm();

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
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


// !!! NEW FUNCTIONS INCOMING !!!


// Function to initialize a new sorted skip list
struct SkipList* initSkipList(struct SkipList *skipList) { // struct SkipList* skipList
  if (skipList == 0) {
    skipList = (struct SkipList*)kalloc();
    skipList->level = 0;
  
    // Initialize head node kept at index 0
    skipList->nodeList[0].value = -1;
    skipList->nodeList[0].pid = -1;
    skipList->nodeList[0].valid = 1;   // Valid bit for sentinel should always be true
  
    // Sentinel is alone and sad, no forward and backward neighbors
    for(int i = 0; i <= MAX_SKIPLIST_LEVEL - 1; i++) { 
      skipList->nodeList[0].forward[i] = -1;
      skipList->nodeList[0].backward[i] = -1;
    }
  
    // Set valid bit for all other nodes to 0 (empty list)
    for (int i = 1; i <= NPROC; i++) {
        skipList->nodeList[i].valid = 0;
    }
  
    return skipList;
  }

  return skipList;
}

unsigned int seed = SEED;
unsigned int random(int max) {
  seed ^= seed << 17;
  seed ^= seed >> 7;
  seed ^= seed << 5;
  return seed % max;
}

// Function to up a level by chance for a new element
int slUpLevel(float p) {
  int level = 0;
  while ((random(100) / 100.0) < p && level < BFS_NICE_LAST_LEVEL) {
      level++;
  }
  return level;
}

// Function to insert a value into the sorted skip list
void slInsert(struct SkipList* skipList, int value, int pid, float p) {
  struct SkipNode* update[4];
  cprintf("Inserting PID %d with vdeadline %d:\n", pid, value);

  //* 1 - FIND THE NODE TO INSERT NEW NODE AT
  // ----------------------------------------------------

  int currentIdx = 0;
  struct SkipNode *current = &skipList->nodeList[currentIdx]; // element 0 is the head node

  for (int i = skipList->level; i >= 0; i--) {
      while (current->forward[i] != -1 
        && skipList->nodeList[current->forward[i]].value < value
        && skipList->nodeList[current->forward[i]].pid != pid) {
          cprintf("  Moving right at level %d (Current node: PID %d with vdeadline %d)\n", i, skipList->nodeList[current->forward[i]].pid, skipList->nodeList[current->forward[i]].value);
          currentIdx = current->forward[i];
          current = &skipList->nodeList[currentIdx];
      }

      // PID already has an associated valid entry in the list
      if (skipList->nodeList[current->forward[i]].valid  == 1 && skipList->nodeList[current->forward[i]].pid == pid){
        cprintf("  Duplicate PID %d found, insert failed\n", pid);
        return;
      }

      update[i] = current;
      cprintf("  Reached the rightmost node at level %d (Current node: PID %d with vdeadline %d)\n", i, current->pid, current->value);
  }

  //* 2 - CHANCE FOR NODE TO BE INSERTED TO NEXT LEVEL
  // ----------------------------------------------------

  int newLevel = slUpLevel(p);

  if (newLevel > skipList->level) {
      for (int i = skipList->level + 1; i <= newLevel; i++) {
          update[i] = &skipList->nodeList[0];
      }
      skipList->level = newLevel;
      cprintf("Increased skip list level to %d\n", skipList->level);
  }

  //* 3 - LOOK FOR NEAREST ARRAY ELEMENT TO PLACE NODE
  // ----------------------------------------------------

  int newIdx = -1;

  // Look for nearest free node
  for (int i = 1; i <= NPROC; i++) {
    if (skipList->nodeList[i].valid == 0) newIdx = i;
  }

  if (newIdx == -1) {
    cprintf("   Skip list is full, insert failed\n");
    return; //! NodeList Array is Full!
  }
  //* 4 - CREATE NEW NODE
  // ----------------------------------------------------

  struct SkipNode* newNode = &skipList->nodeList[newIdx];

  newNode->value = value;
  newNode->pid = pid;
  newNode->valid = 1;

  //* 5 - UPDATE LINKS (OF NEW NODE, NEW BACKWARD, AND NEW FORWARD)
  // ----------------------------------------------------

  for (int i = 0; i <= newLevel; i++) {      
      if (update[i]->forward[i] != -1 && skipList->nodeList[update[i]->forward[i]].valid == 1) {
        // Update Forward of new node
        newNode->forward[i] = update[i]->forward[i];

        // Update backward of new forward to equal new node, if it exists
        int newForwardIdx = update[i]->forward[i];
        skipList->nodeList[newForwardIdx].backward[i] = newIdx;
      } else {
        newNode->forward[i] = -1;
      }
      
      // update forward of backward
      update[i]->forward[i] = newIdx;

      // update backward of new node
      newNode->backward[i] = currentIdx;
      
      cprintf("  Inserted at level %d (Forward node vdeadline: %d, Backward node vdeadline: %d)\n", i, skipList->nodeList[newNode->forward[i]].value, skipList->nodeList[newNode->backward[i]].value);
  }
}

// Function to search for a value in the sorted skip list
struct SkipNode* slSearch(struct SkipList* skipList, int value, int pid) {
  struct SkipNode* current = &skipList->nodeList[0];

  cprintf("Searching for PID %d with vdeadline %d:\n", pid, value);

  for (int i = skipList->level; i >= 0; i--) {
      cprintf("  Checking level %d (Current node: PID %d with vdeadline)\n", i, current->pid, current->value);

      // Keep Looping WHILE:
      //    link to next forward exists (!= -1)
      //    linked forward node is valid (!= 0)
      //    value for forward is less than the searched value
      //    associated pid is not the same
      while (current->forward[i] != -1 && skipList->nodeList[current->forward[i]].valid != 0 
          && skipList->nodeList[current->forward[i]].value < value) {

          cprintf("    Moving right at level %d (Current node: PID %d with vdeadline %d)\n", i, skipList->nodeList[current->forward[i]].pid, skipList->nodeList[current->forward[i]].value);
          current = &skipList->nodeList[current->forward[i]];
      }

      if (current->forward[i] != -1 
          && skipList->nodeList[current->forward[i]].valid == 1 
          && skipList->nodeList[current->forward[i]].value == value 
          && skipList->nodeList[current->forward[i]].pid == pid) {

          cprintf("PID %d with vdeadline %d found at level %d\n", pid, value, i);
          return &skipList->nodeList[current->forward[i]];
      }

      // if (i > 0) { // Just to verify if the downward functionality of the skip list works.
      //     cprintf("  Downward next value at level %d: %d\n", i-1, &skipList->nodeList[current->forward[i-1]].value);
      // }
  }

  cprintf("PID %d with vdeadline %d not found\n", pid, value);
  return 0;
}

// Function to delete a node in the skip list
struct SkipNode* slDelete(struct SkipList* skipList, int value, int pid) {
  struct SkipNode* current = &skipList->nodeList[0];

  cprintf("Searching for PID %d with vdeadline %d:\n", pid, value);

  for (int i = skipList->level; i >= 0; i--) {
      cprintf("  Checking level %d (Current node: PID %d with vdeadline %d)\n", i, current->pid, current->value);

      // Keep Looping WHILE:
      //    link to next forward exists (!= -1)
      //    linked forward node is valid (!= 0)
      //    value for forward is less than the searched value
      //    associated pid is not the same
      while (current->forward[i] != -1 && skipList->nodeList[current->forward[i]].valid != 0 
          && skipList->nodeList[current->forward[i]].value < value) {

          cprintf("    Moving right at level %d (Current node: PID %d with vdeadline %d)\n", i, skipList->nodeList[current->forward[i]].pid, skipList->nodeList[current->forward[i]].value);
          current = &skipList->nodeList[current->forward[i]];
      }

      if (current->forward[i] != -1 
          && skipList->nodeList[current->forward[i]].valid == 1 
          && skipList->nodeList[current->forward[i]].value == value 
          && skipList->nodeList[current->forward[i]].pid == pid) {

          cprintf("PID %d with vdeadline %d to delete found at level %d\n", pid, value, i); 
          current = &skipList->nodeList[current->forward[i]];

          // Delete for each remaining levels
          for (int j = i; j >= 0; j--){
              struct SkipNode *tempForward = &skipList->nodeList[current->forward[j]];
              struct SkipNode *tempBackward = &skipList->nodeList[current->backward[j]];
              tempForward->backward[j] = current->backward[j];
              tempBackward->forward[j] = current->forward[j];

              current->forward[j] = -1;
              current->backward[j] = -1;
              current->valid = 0;

              cprintf("Deleted from level %d (Back node node now pointing to: %d, Forward node now pointing back to: %d)\n", j, skipList->nodeList[tempBackward->forward[j]].value, skipList->nodeList[tempForward->backward[j]].value);
          }

           cprintf("PID %d with vdeadline %d deleted successfully\n", pid, value);

           return current;
      }

      // if (i > 0) { // Just to verify if the downward functionality of the skip list works.
      //     cprintf("  Downward next value at level %d: %d\n", i-1, &skipList->nodeList[current->forward[i-1]].value);
      // }
  }

  cprintf("PID %d with vdeadline %d to delete not found\n", pid, value);
  return 0;
}

// Function to print the entire skip list
void printSkipList(struct SkipList* skipList) {
  cprintf("Skip List:\n");
  for (int i = skipList->level; i >= 0; i--) {
      struct SkipNode* head = &skipList->nodeList[0];
      struct SkipNode* current = &skipList->nodeList[head->forward[i]];
      cprintf("Level %d: ", i);
      while (current->valid == 1 && current->value != 0) {
          cprintf("(%d) %d -> ", current->pid, current->value);
          current = &skipList->nodeList[current->forward[i]];
      }
      cprintf("0\n");
  }
}
