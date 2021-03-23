//--------------------------------------------------------------------------------------------------
// System Programming                         Shell Lab                                    Fall 2020
//
/// @author Woorim Shin
/// @studid 2018-13947
//--------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXPIPES      8   /* max MAXPIPES */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */
#define ERROR_ -1

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* PIPE MACRO */
#define READ 0
#define WRITE 1

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
  pid_t pid;              /* job PID */
  int jid;                /* job ID [1, 2, ...] */
  int state;              /* UNDEF, BG, FG, or ST */
  char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/*----------------------------------------------------------------------------
 * Functions that you will implement
 */

void eval(char *cmdline);
int builtin_cmd(char *(*argv)[MAXARGS]  );
void do_bgfg(char *(*argv)[MAXARGS]  );
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/*----------------------------------------------------------------------------*/

/* These functions are already implemented for your convenience */
int parseline(const char *cmdline, char *(*argv)[MAXARGS],  int* pipec);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*----------------------------------------------------------------------------*/

/* These functions are implemented additionaly for more convenience */
int isnumber(char* number);
int Kill(pid_t pid, int sig);
int Sigaddset(sigset_t *set, int signum);
int Sigemptyset(sigset_t *set);
int Sigfillset(sigset_t *set);
int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
  char c;
  char cmdline[MAXLINE];
  int emit_prompt = 1; /* emit prompt (default) */

  /* Redirect stderr to stdout (so that driver will get all output
   * on the pipe connected to stdout) */
  // Redirect stderr outputs to stdout
  dup2(1, 2);

  /* Parse the command line */
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
      case 'h':             /* print help message */
        usage();
        break;
      case 'v':             /* emit additional diagnostic info */
        verbose = 1;
        break;
      case 'p':             /* don't print a prompt */
        emit_prompt = 0;  /* handy for automatic testing */
        break;
      default:
        usage();
    }
  }

  /* Install the signal handlers */

  /* These are the ones you will need to implement */
  Signal(SIGINT,  sigint_handler);   /* ctrl-c */
  Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
  Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

  /* This one provides a clean way to kill the shell */
  Signal(SIGQUIT, sigquit_handler);

  /* Initialize the job list */
  initjobs(jobs);

  /* Execute the shell's read/eval loop */
  while (1) {

    /* Read command line */
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }
    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
      app_error("fgets error");
    if (feof(stdin)) { /* End of file (ctrl-d) */
      fflush(stdout);
      exit(0);
    }
    	
    eval(cmdline);
    
    fflush(stdout);
    fflush(stdout);
  }

  exit(0); /* control never reaches here */
}


/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 * When there is redirection(>), it return file name to char** file
 */
void eval(char *cmdline)
{  

  // argv array
 char *argv[MAXPIPES][MAXARGS];

 pid_t pid;
 int is_bg, filedescriptor;
 int is_redirect = 0;
 char *redic, *temp;
 sigset_t mask_all, mask_sigchld, prev_one;
 int rpipec = 0;

 Sigfillset(&mask_all);
 Sigemptyset(&mask_sigchld);
 Sigaddset(&mask_sigchld, SIGCHLD);


 redic = strchr(cmdline, '>');
 // if command has redirection mark(<), we redirect output with dup2.
 // now redic is ptr for file name, cmdline only contain command.
 if ((redic != NULL) && (*(redic-1)==' ') && (*(redic+1)==' ') ){
 *redic = '\0'; redic++;
 while( (*redic) && (*redic ==' ')) redic++;
 temp = strchr(redic, '\n');
 *temp = '\0';
 is_redirect = 1;
 }


 is_bg = parseline(cmdline, argv, &rpipec);
 //printf("rpipec is %d\n", rpipec);
 int pipefd[rpipec-1][2];

 for (int i =0; i < rpipec; i++){
   if (pipe(pipefd[i]) < 0) unix_error("pipe open faild");
 }

 // not built-in commands
 // fork a child process and run the job in the context of the child.
 // If foreground, wait for it to terminate and then return.
 if (builtin_cmd(argv) == 0) {

   for(int i=0; i<rpipec; i++){


   pid = fork();

   if (pid < 0){
     unix_error("fork faild.");
     exit(1);
  }

   // block SIGCHLD
   Sigprocmask(SIG_BLOCK, &mask_sigchld, &prev_one);

   // child
   if (pid == 0){

     if(is_redirect == 1){
     // open file and redirect
     filedescriptor = open(redic, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR |S_IROTH | S_IWOTH);
     if(filedescriptor < 0) unix_error("failed to open file\n");
     if(dup2(filedescriptor, STDOUT_FILENO) < 0) unix_error("We failed to redirect file\n");
     close(filedescriptor);
     }

    // put child to new pg with pgid : pid[child]
    if(setpgid(0, 0) < 0) {
    unix_error("setpgid failed");
    }

    // restore signal bit block vector
    Sigprocmask(SIG_SETMASK, &prev_one, NULL);

    //pipe issue
   // give output 
    if(i!=rpipec-1){
      if(dup2(pipefd[i][WRITE],STDOUT_FILENO) < 0){
        printf ("i = %d\n", i);   
        unix_error("failed to give output");
      }
    }

    //take input
    if(i!=0){
      if(dup2(pipefd[i-1][READ],STDIN_FILENO) < 0) {
        printf ("i = %d\n", i);
        unix_error("failed to take input");
      }
    }

   for(int j = 0; j<rpipec; j++){
    close(pipefd[j][WRITE]);
    close(pipefd[j][READ]);
   }

    if(execvp(argv[i][0], argv[i]) < 0) {
      unix_error("failed to execute process.\n");

      exit(0);
    }
    
   }
   // child end

   
   for(int j = 0; j<rpipec; j++){
   if(j<=i)close(pipefd[j][WRITE]);
   if(j<i) close(pipefd[j][READ]);
   }
   close(pipefd[rpipec-1][WRITE]);
   


   // parent
   Sigprocmask(SIG_BLOCK, &mask_all, NULL);
   addjob(jobs, pid, is_bg+1, cmdline);
   Sigprocmask(SIG_SETMASK, &prev_one, NULL);

   if(is_bg) {
     struct job_t *newjob = getjobpid(jobs, pid);
      printf("[%d] (%d) %s", newjob->jid, newjob->pid, newjob->cmdline);
   }
   else {
    waitfg(pid);
    }
   }

 }
 return;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 * argv[MAXPIPES][MAXARGS]
*/

int parseline(const char *cmdline, char *(*argv)[MAXARGS] , int *rpipec  )
{
  static char array[MAXLINE]; /* holds local copy of command line */
  char* buf = array;          /* ptr that traverses command line */
  char* delim;                /* points to first space delimiter */
  char* pdelim;               /* points to pipe */
  int argc;                   /* number of args */
  int bg=0;                   /* background job? */
  int pipec;

  strcpy(buf, cmdline);
  buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */

  argc = 0;// How many argv
  pipec = 0;

  // ignore leading spaces
  while (*buf && (*buf == ' ')) buf++;

  if (*buf == '\'') {
    buf++;
    delim = strchr(buf, '\'');
  } else {
    delim = strchr(buf, ' ');
  }

  while (delim) {
    argv[pipec ][ argc++] = buf;
    *delim = '\0';
    buf = delim + 1;

    // ignore spaces
    while (*buf && (*buf == ' ')) buf++;

    if (*buf) pdelim = strchr(buf, '|');

    // if there is pipe right on buf pointer
    if (*buf && pdelim  && *buf == *pdelim) {
      pipec++;
      argc=0;
      buf = buf + 1;

      // ignore spaces
      while (*buf && ( *buf == ' ')) buf++;
    }

    if (*buf == '\'') {
      buf++;
      delim = strchr(buf, '\'');
    } else {
      delim = strchr(buf, ' ');
    }
  }
  argv[pipec][ argc] = NULL;
  pipec++;
  *rpipec = pipec;// change pipec value for eval()

  // ignore blank line
  if (argc == 0) return 1;

  // should the job run in the background?
 if ((bg = (strcmp(argv[pipec-1][argc-1] , "&") == 0)) != 0) {
    argv[pipec-1][--argc] = NULL; 
  }
  return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 *    If it is built-in command, returns 1
 *    else, returns 0
 */
int builtin_cmd(char *(*argv)[MAXARGS] )
{
  if(strcmp(argv[0][0], "quit") == 0) {
    exit(0);
    return 1;
  } else if (strcmp(argv[0][0], "bg") == 0) {
    do_bgfg(argv);
    return 1;
  } else if (strcmp(argv[0][0], "fg") == 0) {
    do_bgfg(argv);
    return 1;
  } else if (strcmp(argv[0][0], "jobs") == 0) {
    listjobs(jobs);
    fflush(stdout);
    return 1;
  } else return 0;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char *(*argv)[MAXARGS])
{
  sigset_t mask_sigchld, prev_one;
  struct job_t *job;
  int is_bg = !strcmp(argv[0][0], "bg");

  if (argv[0][1] == NULL){
    printf("%s command requires PID or %%jobid argument\n", argv[0][0]);
    return;
  }

  // case 1 : we get %jobid argument
  if (argv[0][1][0] == '%'){
    char *jobid = &argv[0][1][1];
    job = getjobjid(jobs, atoi(jobid));
    if (job == NULL) {
      printf("%%%d: No such job\n",atoi(jobid));
      return;
    }
  }
  
  // case 2 : we get PID argument
  else {
    if(!isnumber(argv[0][1])) {
    printf("%s: argument must be a PID or %%jobid\n", argv[0][0]);
    return;
    }
    job = getjobpid(jobs, atoi(argv[0][1]));
    if (job == NULL){
      printf("(%d): No such process\n",atoi(argv[0][1]));
      return;
    }
  }

  // for block sigchild
  Sigemptyset(&mask_sigchld);
  Sigaddset(&mask_sigchld, SIGCHLD);


  // mask SIGCHLD
  Sigprocmask(SIG_BLOCK, &mask_sigchld, &prev_one);
  // send SIGCONT signal
  Kill(-(job->pid), SIGCONT);

  // bg : Stopped background job -> Running background job
  // fg : Backgroung job -> Running foreground job
  if (is_bg){
    printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    job->state = BG;
  } else {
    job->state = FG;
  }
  Sigprocmask(SIG_SETMASK, &prev_one, NULL);

  if(!is_bg){
    waitfg(job->pid);
  }


  return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process.
 * In infinite loop, waits for fgpid no longer returns current pid
 */
void waitfg(pid_t pid)
{ 
  while(1){
    sleep(1);
    if (fgpid(jobs) != pid) break;
  }
  return;
}


/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
// In eval(), there can be "race" case between addjob and deletejob.
// There is solution.
// 1. Block SIGCHLD before calling fork()
// 2. unblock only after calling addjob()
// 3. child processes get their parent's blocked set,
// so we need to unblock child's SIGCHLD before calling execve()
void sigchld_handler(int sig)
{
  // save errno
  int olderrno = errno;
  int status;
  sigset_t mask_all, prev_all;
  pid_t pid;

  Sigfillset(&mask_all);
  while((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
   
    // with status, we can check the exit condition of reaped child
    // If exited nomally by exit call or return
    if (WIFEXITED(status)){
      // block all signal and delete job
      Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
      deletejob(jobs, pid);
      Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }

    // if the child is stopped
    if (WIFSTOPPED(status)) {
      printf("Job [%d] (%d) stopped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
      fflush(stdout);

      // if signal is sent by other process, not terminal, we need additional works for editing job list
      struct job_t *fgjob = getjobpid(jobs, pid);
      Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
      if(fgjob->state != ST){
        fgjob->state = ST;
      } 
      Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }

    // if the child is terminated by signal
    if (WIFSIGNALED(status)){
      printf("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
      fflush(stdout);
      // block all signail and delete job
      Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
      deletejob(jobs, pid);
      Sigprocmask(SIG_SETMASK, &prev_all, NULL);
    }
  }

  // restore errno
  errno = olderrno;
  return;

}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */

void sigint_handler(int sig)
{
  // save errno for it not to be modified.
  int olderrno = errno;

  // find foreground job and send SIGINT by kill function
  pid_t pid = fgpid(jobs);
  if (pid == 0) {
    return;
  }
  Kill(-pid, SIGINT);

  //restore it
  errno = olderrno;

  return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */

void sigtstp_handler(int sig)
{
  // save errno
  int olderrno = errno;
  
  // find foreground job
  pid_t pid = fgpid(jobs);
  if (pid == 0){
    return;
  }

  Kill(-pid, SIGTSTP);

  // for not to be interrupted by all signal
  sigset_t mask_all, prev_all;
  Sigfillset(&mask_all);
  Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);

  // make job state ST
  struct job_t *fgjob = getjobpid(jobs, pid);
  fgjob->state = ST;

  // restore block bit vector and errno
  Sigprocmask(SIG_SETMASK, &prev_all, NULL);
  errno = olderrno;
  return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
  job->pid = 0;
  job->jid = 0;
  job->state = UNDEF;
  job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
  int i, max=0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].state = state;
      jobs[i].jid = nextjid++;
      if (nextjid > MAXJOBS)
        nextjid = 1;
      strcpy(jobs[i].cmdline, cmdline);
      if(verbose){
        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
      }
      return 1;
    }
  }
  printf("Tried to create too many jobs\n");
  return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      clearjob(&jobs[i]);
      nextjid = maxjid(jobs)+1;
      return 1;
    }
  }
  return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].state == FG)
      return jobs[i].pid;
  return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
  int i;

  if (pid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid)
      return &jobs[i];
  return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
  int i;

  if (jid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid == jid)
      return &jobs[i];
  return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
  int i;

  if (pid < 1)
    return 0;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid) {
      return jobs[i].jid;
    }
  return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
  int i;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
      printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
      switch (jobs[i].state) {
        case BG:
          printf("Running ");
          break;
        case FG:
          printf("Foreground ");
          break;
        case ST:
          printf("Stopped ");
          break;
        default:
          printf("listjobs: Internal error: job[%d].state=%d ",
              i, jobs[i].state);
      }
      printf("%s", jobs[i].cmdline);
    }
  }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/
/*
 * isnumber - check whether the string is number
 * return 1 if it's number, return 0 if not
 */
int isnumber(char* number){
  int i = 0;
  while (number[i] != '\0'){
    if (!isdigit(number[i])){
      return 0;
    }
    i++;
  }
  return 1;
}


/*
 * usage - print a help message
 */
void usage(void)
{
  printf("Usage: shell [-hvp]\n");
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
  fprintf(stdout, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
  fprintf(stdout, "%s\n", msg);
  exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler)
{
  struct sigaction action, old_action;

  action.sa_handler = handler;
  sigemptyset(&action.sa_mask); /* block sigs of type being handled */
  action.sa_flags = SA_RESTART; /* restart syscalls if possible */

  if (sigaction(signum, &action, &old_action) < 0)
    unix_error("Signal error");
  return (old_action.sa_handler);
}

/*
 *  Kill - wrapper for the kill function.
 *  if pid > 0, send sig to process pid.
 *  if pid = 0, send sig to process group including itself.
 *  if pid < 0, send sig to process group |pid|
 *
 */
int Kill(pid_t pid, int sig)
{
  int temp = kill(pid, sig);
  if (temp<0){
    unix_error("kill failed");
  }
  return temp;
}

/*
 *  Sigaddset - wrapper for the sigaddset function
 *  
 */
int Sigaddset(sigset_t *set, int signum) 
{
    int temp  = sigaddset(set, signum);
    if (temp < 0) {
        unix_error("sigaddset error");
    }
    return temp;
}

/*
 * Sigemptyset - wrapper for the sigemptyset function
 */
int Sigemptyset(sigset_t *set)
{
    int temp = sigemptyset(set);
    if (temp < 0) {
        unix_error("sigemptyset error");
    }
    return temp;
}

/*
 * Sigfillset - wrapper for the sigfillset function
 */
int Sigfillset(sigset_t *set)
{
    int temp = sigfillset(set);
    if (temp < 0) {
        unix_error("sigfillset error");
    }
    return temp;
}

/*
 * Sigprocmask - wrapper for the sigprocmask function
 * change the set of current blocked signal
 * SIG_BLOCK : blocked = blocked | set
 * SIG_UNBLOCK : blocked & ~set
 * SIG_SETMASK : blocked = set
 * if oldset is not null, previous value of blocked bit vector is saved to oldset
 *
 * set is modified by these functions below.
 * sigemptyset : initialize set to empty set
 * sigfillset : add all singnal to set
 * sigaddset : add signum to set
 */
int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
    int temp = sigprocmask(how, set, oldset);
    if (temp < 0) {
        unix_error("sigprocmask error");
    }
    return temp;
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
  printf("Terminating after receipt of SIGQUIT signal\n");
  exit(1);
}

/* $end tshref-ans */
