//--------------------------------------------------------------------------------------------------
// System Programming                         I/O Lab                                    Fall 2020
//
/// @file
/// @brief resursively traverse directory tree and list all entries
/// @author <yourname>
/// @studid <studentid>
//--------------------------------------------------------------------------------------------------

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>
#include <grp.h>
#include <pwd.h>

#define MAX_DIR 64            ///< maximum number of directories supported

/// @brief output control flags
#define F_TREE      0x1       ///< enable tree view
#define F_SUMMARY   0x2       ///< enable summary
#define F_VERBOSE   0x4       ///< turn on verbose mode

/// @brief struct holding the summary
struct summary {
  unsigned int dirs;          ///< number of directories encountered
  unsigned int files;         ///< number of files
  unsigned int links;         ///< number of links
  unsigned int fifos;         ///< number of pipes
  unsigned int socks;         ///< number of sockets

  unsigned long long size;    ///< total size (in bytes)
  unsigned long long blocks;  ///< total number of blocks (512 byte blocks)
};


/// @brief abort the program with EXIT_FAILURE and an optional error message
///
/// @param msg optional error message or NULL
void panic(const char *msg)
{
  if (msg) fprintf(stderr, "%s\n", msg);
  exit(EXIT_FAILURE);
}


/// @brief read next directory entry from open directory 'dir'. Ignores '.' and '..' entries
///
/// @param dir open DIR* stream
/// @retval entry on success
/// @retval NULL on error or if there are no more entries
struct dirent *getNext(DIR *dir)
{
  struct dirent *next;
  int ignore;

  do {
    errno = 0;
    next = readdir(dir);
    if (errno != 0) perror(NULL);
    ignore = next && ((strcmp(next->d_name, ".") == 0) || (strcmp(next->d_name, "..") == 0));
  } while (next && ignore);

  return next;
}


/// @brief qsort comparator to sort directory entries. Sorted by name, directories first.
///
/// @param a pointer to first entry
/// @param b pointer to second entry
/// @retval -1 if a<b
/// @retval 0  if a==b
/// @retval 1  if a>b
static int dirent_compare(const void *a, const void *b)
{
  struct dirent *e1 = (struct dirent*)a;
  struct dirent *e2 = (struct dirent*)b;

  // if one of the entries is a directory, it comes first
  if (e1->d_type != e2->d_type) {
    if (e1->d_type == DT_DIR) return -1;
    if (e2->d_type == DT_DIR) return 1;
  }

  // otherwise sorty by name
  return strcmp(e1->d_name, e2->d_name);
}


/// @brief recursively process directory @a dn and print its tree
///
/// @param dn absolute or relative path string
/// @param pstr prefix string printed in front of each entry
/// @param stats pointer to statistics
/// @param flags output control flags (F_*)
void processDir(const char *dn, const char *pstr, struct summary *stats, unsigned int flags)
{
  // TODO
  unsigned int nentry, nmax;
  struct dirent *entry, *entries;

  // open directory
  DIR *directory  = opendir(dn);
  if (directory == NULL) {
  printf("%s%sERROR: %s\n", pstr, flags & F_TREE ? "`-" : "  ", strerror(errno));
  return;
  }

  // read directory
  nentry = 0;
  nmax = 0;
  entries = NULL;

  while ((entry = getNext(directory)) != NULL ){
     if (nentry == nmax) {
      nmax = nmax + 1024;

    struct dirent *nentries = realloc(entries, sizeof(struct dirent)*nmax);
    if (nentries == NULL) {
      printf("%s%sERROR: %s\n", pstr, flags & F_TREE ? "`-" : "  ", strerror(errno));
      free(entries);
      closedir(directory);
      return ;
    }
  entries = nentries;

  }
     entries[nentry] = *entry;
     nentry++;
  }

  closedir(directory);

  if (nentry == 0) return;

  // sort entries
  //
  qsort(entries, nentry, sizeof(entries[0]), dirent_compare);

 //process entries
  for (unsigned int pos=0; pos<nentry; pos++){
    struct dirent *this = &entries[pos];
    struct stat sb;
    int stat_valid = 0;
    char *fn, *out, *tmp;

    if (asprintf(&fn, "%s%s", dn, this->d_name) == -1) panic("Out of memory");

    //make output string
    if (flags & F_TREE) {
      if (asprintf(&out, "%s%s", pstr, pos<nentry-1 ? "|-" : "`-") == -1 ) panic("Out of memory.");
    } else {
      if (asprintf(&out, "%s  ", pstr) == -1 ) panic("Out of memory.");
    }

    if (asprintf(&tmp, "%s%s", out, this->d_name) == -1) panic("Out of memory.");
    free(out);
    out = tmp;

      // file meta-data
      if (flags & F_VERBOSE) {
        stat_valid = lstat(fn, &sb) == 0;
       //printf("\nHi! fn is %s and &sb is %p\n", fn, &sb);

        if(stat_valid) {
          // type
          char type = S_ISREG(sb.st_mode) ? ' ' :
            S_ISDIR(sb.st_mode) ? 'd' :
            S_ISCHR(sb.st_mode) ? 'c' :
            S_ISBLK(sb.st_mode) ? 'b' :
            S_ISLNK(sb.st_mode) ? 'l' :
            S_ISFIFO(sb.st_mode) ? 'f' :
            S_ISSOCK(sb.st_mode) ? 's' :
            '?';

          // user name
          char *ustr = NULL;
          struct passwd *pwd = getpwuid(sb.st_uid);
          if (pwd != NULL) ustr = strdup(pwd->pw_name);
          else {
            if (asprintf(&ustr, "%d", sb.st_uid) == -1) panic("Out of memory.");
          }

          //group name
          char *gstr = NULL;
          struct group *grp = getgrgid(sb.st_gid);
          if (grp != NULL) gstr = strdup(grp->gr_name);
          else {
            if (asprintf(&gstr, "%d", sb.st_gid) == -1) panic("Out of memory.");
          }

          // make ... if necessary
          char fstr[55];
          fstr[54] = '\0';
          strncpy(fstr, out, sizeof(fstr));
          if (fstr[54] != '\0') {
            fstr[51] = '.';
            fstr[52] = '.';
            fstr[53] = '.';
            fstr[54] = '\0';
          }

          // make it together
          if (asprintf(&tmp, "%-54s %8s:%-8s %10ld %8ld %c", fstr, ustr, gstr, sb.st_size, sb.st_blocks, type) == -1) {
            panic("Out of memory.");
          }
          free(out);
          out = tmp;

          free(ustr);
          free(gstr);
        } else {
          if (asprintf(&tmp, "%-54s %s", out, strerror(errno)) == -1) panic("Out of memory.");
          free(out);
          out = tmp;
        
      }
    }

    printf("%s\n", out);
    free(out);

    //stats
    if (flags & F_SUMMARY) {
      switch (this->d_type) {
        case DT_REG: stats->files++; break;
        case DT_DIR: stats->dirs++; break;
        case DT_LNK: stats->links++; break;
        case DT_FIFO: stats->fifos++; break;
        case DT_SOCK: stats->socks++; break;
        default: ;
      }

      if ((flags & F_VERBOSE) && stat_valid) {
        stats->size += sb.st_size;
        stats->blocks += sb.st_blocks;
      }
    }

    // if entry is a directory
    if (this->d_type == DT_DIR) {

      // tree prefix string
      char *npstr;
    if (asprintf(&npstr, flags & F_TREE && pos<nentry-1 ? "%s| " : "%s ", pstr) == -1)
        panic("Out of memory.");
     if ( asprintf(&fn, "%s/", fn) == -1)
       panic("Out of memory.");
      processDir(fn, npstr, stats, flags);

      free(npstr);
    }
    free(fn);
  }
  free(entries);
}


/// @brief print program syntax and an optional error message. Aborts the program with EXIT_FAILURE
///
/// @param argv0 command line argument 0 (executable)
/// @param error optional error (format) string (printf format) or NULL
/// @param ... parameter to the error format string
void syntax(const char *argv0, const char *error, ...)
{
  if (error) {
    va_list ap;

    va_start(ap, error);
    vfprintf(stderr, error, ap);
    va_end(ap);

    printf("\n\n");
  }

  assert(argv0 != NULL);

  fprintf(stderr, "Usage %s [-t] [-s] [-v] [-h] [path...]\n"
                  "Gather information about directory trees. If no path is given, the current directory\n"
                  "is analyzed.\n"
                  "\n"
                  "Options:\n"
                  " -t        print the directory tree (default if no other option specified)\n"
                  " -s        print summary of directories (total number of files, total file size, etc)\n"
                  " -v        print detailed information for each file. Turns on tree view.\n"
                  " -h        print this help\n"
                  " path...   list of space-separated paths (max %d). Default is the current directory.\n",
                  basename(argv0), MAX_DIR);

  exit(EXIT_FAILURE);
}


/// @brief program entry point
int main(int argc, char *argv[])
{
  //
  // default directory is the current directory (".")
  //
  const char CURDIR[] = ".";
  const char *directories[MAX_DIR];
  int   ndir = 0;

  struct summary dstat, tstat;
  unsigned int flags = 0;

  //
  // parse arguments
  //
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      // format: "-<flag>"
      if      (!strcmp(argv[i], "-t")) flags |= F_TREE;
      else if (!strcmp(argv[i], "-s")) flags |= F_SUMMARY;
      else if (!strcmp(argv[i], "-v")) flags |= F_VERBOSE;
      else if (!strcmp(argv[i], "-h")) syntax(argv[0], NULL);
      else syntax(argv[0], "Unrecognized option '%s'.", argv[i]);
    } else {
      // anything else is recognized as a directory
      if (ndir < MAX_DIR) {
        directories[ndir++] = argv[i];
      } else {
        printf("Warning: maximum number of directories exceeded, ignoring '%s'.\n", argv[i]);
      }
    }
  }

  // if no directory was specified, use the current directory
  if (ndir == 0) directories[ndir++] = CURDIR;


  //
  // process each directory
  //
  // TODO

  memset(&tstat, 0, sizeof(tstat));
  for(int i=0; i<ndir; i++){
    memset(&dstat, 0, sizeof(dstat));

    // print header if summary mode
    if (flags & F_SUMMARY) {
      if(flags & F_VERBOSE) {
    printf("%-54s %8s:%-8s %10s %8s %s\n", "Name", "User", "Group", "Size", "Blocks", "Type");
      } else {
    printf("%-54s\n", "Name");
      }
    printf("----------------------------------------------------------------------------------------------------\n");
    printf("%s\n", directories[i]);
    }
    processDir(directories[i], "", &dstat, flags);
    // print footer and stat
    if (flags & F_SUMMARY){
      char *filestat, *dirstat, *linkstat, *pipestat, *socketstat, *summarystat;
    if (dstat.files == 1){
     if(asprintf(&filestat, "1 file, ")==-1) panic("Out of memory.");
    } else {
     if(asprintf(&filestat, "%d files, ", dstat.files)==-1) panic("Out of memory.");
    }

    if (dstat.dirs == 1){
      if(asprintf(&dirstat, "1 directory, ")==-1) panic("Out of memory");
    } else {
     if(asprintf(&dirstat, "%d directories, ", dstat.dirs)==-1) panic("Out of memory.");
    }

    if (dstat.links == 1){
     if(asprintf(&linkstat, "1 link, ")==-1) panic("Out of memory.");

    } else {
      if(asprintf(&linkstat, "%d links, ", dstat.links)==-1) panic("Out of memory.");
    }

    if (dstat.fifos == 1){
      if(asprintf(&pipestat, "1 pipe, ")==-1) panic("Out of memory.");
    } else {
      if(asprintf(&pipestat, "%d pipes, ", dstat.fifos)==-1) panic("Out of memory.");
    }

    if (dstat.socks == 1){
      if(asprintf(&socketstat, "and 1 socket")==-1) panic("Out of memory.");
    } else {
      if(asprintf(&socketstat, "and %d sockets", dstat.socks)==-1) panic("Out of memory.");
    }
    if(asprintf(&summarystat, "%s%s%s%s%s", filestat, dirstat, linkstat, pipestat, socketstat)==-1) panic("Out of memory.");

    printf("----------------------------------------------------------------------------------------------------\n");
    printf("%-68s %14llu %9llu\n\n",summarystat,dstat.size , dstat.blocks);
    }
    tstat.blocks += dstat.blocks;
    tstat.dirs  += dstat.dirs;
    tstat.fifos += dstat.fifos;
    tstat.files += dstat.files;
    tstat.links += dstat.links;
    tstat.size += dstat.size;
    tstat.socks += dstat.socks;
   }


  //
  // print grand total
  //
  if ((flags & F_SUMMARY) && (ndir > 1)) {
    printf("Analyzed %d directories:\n"
           "  total # of files:        %16d\n"
           "  total # of directories:  %16d\n"
           "  total # of links:        %16d\n"
           "  total # of pipes:        %16d\n"
           "  total # of socksets:     %16d\n",
           ndir, tstat.files, tstat.dirs, tstat.links, tstat.fifos, tstat.socks);

    if (flags & F_VERBOSE) {
      printf("  total file size:         %16llu\n"
             "  total # of blocks:       %16llu\n",
             tstat.size, tstat.blocks);
    }

  }

  //
  // that's all, folks
  //
  return EXIT_SUCCESS;
}
