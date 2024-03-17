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
thread_t nexttid = 1;
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
  p->tid = nexttid++;

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
  struct proc *t;
  struct proc *curproc = myproc();

  sz = curproc->sz;

  // Check the limit of the current process.
  if(curproc->limit != 0 && sz + n > curproc->limit)
    return -1;

  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;

  acquire(&ptable.lock);

  // Update sz variable of the other threads.
  for(t = ptable.proc; t < &ptable.proc[NPROC]; t++)
    if(t->pid == curproc->pid)
      t->sz = sz;

  release(&ptable.lock);

  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
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

  // Clean up all other threads.
  thread_clear1();

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent->pid == curproc->pid){
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
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
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

// Print a process listing to console.  For pmanager
// Runs when user types list on pmanager.
// No lock to avoid wedging a stuck machine further.
void
procdump2(void)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if((p->state != RUNNING && p->state != RUNNABLE && p->state != SLEEPING) 
        || p->pid == p->parent->pid)
      continue;
    cprintf("**************************************\n");
    cprintf("name                  : %s\n", p->name);
    cprintf("pid                   : %d\n", p->pid);
    cprintf("stack page number     : %d\n", p->spnum);
    cprintf("allocated memory size : %d\n", p->sz);
    if(p->limit == 0)
      cprintf("memory maximum limit  : no limit\n");
    else
      cprintf("memory maximum limit  : %d\n", p->limit);
    cprintf("**************************************\n");
  }
}

// Set the limit of process memory
int
setmemorylimit(int pid, int limit)
{
  struct proc *p;
  int pexist = 0;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      // If the limit is smaller than current process memory size.
      if(limit != 0 && limit < p->sz) { 
        release(&ptable.lock);
        return -1;
      }
      p->limit = limit;
      pexist = 1;
    }
  }
  release(&ptable.lock);

  // If the process that matches pid does not exist.
  if(pexist == 0)
    return -1;

  return 0;
}

// Create a new thread.
int
thread_create(thread_t *thread, void *(*start_routine)(void *), void* arg)
{
  int i;
  struct proc *t;
  struct proc *curproc = myproc();

  // Check the limit of the current process.
  if(curproc->limit != 0 && curproc->sz + PGSIZE > curproc->limit)
    return -1;

  // Allocate process.
  if((t = allocproc()) == 0){
    return -1;
  }
  nextpid--;

  // Allocate a new stack for this thread.
  if((curproc->sz = allocuvm(curproc->pgdir, curproc->sz, curproc->sz + PGSIZE)) == 0)  {
    kfree(t->kstack);
    t->kstack = 0;
    t->state = UNUSED;
    return -1;
  }

  // Share thread state with current process.
  t->pid = curproc->pid;
  t->sz = curproc->sz;
  t->pgdir = curproc->pgdir;
  t->limit = curproc->limit;

  // Copy thread trap frame state from current process.
  *t->tf = *curproc->tf;

  // Check if the curproc is main thread.
  if(curproc->parent->pid == curproc->pid)
    t->parent = curproc->parent;
  else
    t->parent = curproc;

  // Increase the stack page number of the main thread
  t->parent->spnum++;

  // Strat from the start routine and set sp to top of the page
  t->tf->eip = (uint)start_routine;
  t->tf->esp = (uint)t->sz;

  // Pass the argument to the stack
  t->tf->esp -= 4;
  *(uint*)t->tf->esp = (uint)arg;

  // Set the fake return address.
  t->tf->esp -= 4;
  *(uint*)t->tf->esp = 0xffffffff;

  // Copy the address of the file descriptor without fileup()
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      t->ofile[i] = curproc->ofile[i];

  // Copy the address of the current derectory widout idup().
  t->cwd = curproc->cwd;

  safestrcpy(t->name, curproc->name, sizeof(curproc->name));

  *thread = t->tid;

  acquire(&ptable.lock);
  
  t->state = RUNNABLE;

  // Update sz variable of the other threads.
  for(t = ptable.proc; t < &ptable.proc[NPROC]; t++)
    if(t->pid == curproc->pid)
      t->sz = curproc->sz;

  release(&ptable.lock);

  return 0;
}

void 
thread_exit(void *retval)
{
  struct proc *t;
  struct proc *curproc = myproc();
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Main thread exiting.
  if(curproc->pid != curproc->parent->pid)
    exit();

  for(fd = 0; fd < NOFILE; fd++)
      curproc->ofile[fd] = 0;
  curproc->cwd = 0;

  curproc->threadretval = retval;

  acquire(&ptable.lock);

  // Another thread might be sleeping in wait().
  for(t = ptable.proc; t < &ptable.proc[NPROC]; t++)
    if(t->pid == curproc->pid && t->tid != curproc->tid)
      wakeup1(t);

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

int thread_join(thread_t thread, void **retval)
{
  struct proc *t;
  int havekids;
  struct proc *curproc = myproc();

  // Wait itself.
  if(thread == curproc->tid)
    return -1;
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(t = ptable.proc; t < &ptable.proc[NPROC]; t++){
      if(t->pid != curproc->pid || t->tid != thread)
        continue;
      havekids = 1;
      if(t->state == ZOMBIE){
        // Found one.
        kfree(t->kstack);
        t->kstack = 0;
        t->pid = 0;
        t->tid = 0;
        t->parent = 0;
        t->name[0] = 0;
        t->killed = 0;
        t->limit = 0;
        t->state = UNUSED;
        
        *retval = t->threadretval;
        t->threadretval = 0;
        release(&ptable.lock);
        return 0;
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

// Clean up all other threads.
void 
thread_clear1(void)
{
  int fd;
  struct proc *t;
  struct proc *curproc = myproc();

  // Make the current thread to the main thread
  if(curproc->parent->pid == curproc->pid) {
    curproc->spnum = curproc->parent->spnum;
    curproc->parent = curproc->parent->parent;
  }


  for(t = ptable.proc; t < &ptable.proc[NPROC]; t++){
    // Pass abandoned children to init.
    if(t->parent->pid == curproc->pid){
      t->parent = initproc;
      if(t->state == ZOMBIE)
        wakeup1(initproc);
    }

    // Clear all other threads
    if(t->pid != curproc->pid || t->tid == curproc->tid)
      continue;
    for(fd = 0; fd < NOFILE; fd++)
      t->ofile[fd] = 0;
    t->cwd = 0;
    kfree(t->kstack);
    t->kstack = 0;
    t->pgdir = 0;
    t->pid = 0;
    t->tid = 0;
    t->parent = 0;
    t->name[0] = 0;
    t->killed = 0;
    t->limit = 0;
    t->spnum = 0;
    t->threadretval = 0;
    t->state = UNUSED;
  }
}

void
thread_clear(void)
{
  acquire(&ptable.lock);
  thread_clear1();
  release(&ptable.lock);
}