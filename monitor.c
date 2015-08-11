#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <libvirt/libvirt.h>
#include <ncurses.h>

#define ROW_LENGTH 80
#define HOST_INFORMATION_BUFFER_SIZE 240
#define ERROR 4294967295 	/* Unsigned int -1 */

struct domain_metadata {
  time_t prevTimestamp;
  time_t curTimestamp;
  unsigned long long prevCpuTime;
  unsigned long long curCpuTime;
};

void
get_host_informations (virConnectPtr conn, char *buffer)
{
  virNodeInfo nodeinfo;
  char *host;
  time_t current_time;
  char row[4][ROW_LENGTH];

  /* Zeroing host_information */
  memset (buffer, '\0', HOST_INFORMATION_BUFFER_SIZE);

  /* For obtaining various information about the virtualization host. 
   * If successful returns 0, error occurred -1 is returned instead. */
  if (virNodeGetInfo (conn, &nodeinfo))
  {
    sprintf (buffer, "%s\n", "Failed to get informations about the virtualization host.");
    return;
  }

  /* First row
   * Write host name, Model, current time */
  host = virConnectGetHostname (conn);
  time (&current_time);

  sprintf (row[0], "[ %s: %-.17s ] [ %s: %-.7s ] - %s", 
      "Host name", host, 
      "Model", nodeinfo.model,
      ctime (&current_time));
  free (host);

  /* Second row
   * Write maximum support virtual CPUs, Number of CPUs, MHz of CPUs */
  sprintf (row[1], "[ %s: %-2d] [ %s: %-2u] [ %s:%5uMhz]",
      "Maximum support virtual CPUs", virConnectGetMaxVcpus (conn, NULL),
      "Number of CPUs", nodeinfo.cpus,
      "CPUs freq", nodeinfo.mhz);

  /* Third row
   * Write Node free memory (Total amount of free memory on the virtualization host. */
  sprintf (row[2], "[ %s:%5luMB] [ %s: %-2u] [ %s:%5lluMB ]",
      "Memory size", nodeinfo.memory / 1024,
      "Number of NUMA nodes", nodeinfo.nodes,
      "Node free memory", virNodeGetFreeMemory (conn) / (1024 * 1024));

  /* Merge all rows */
  sprintf (buffer, "%s%s\n%s\n", row[0], row[1], row[2]);
}

int
get_domains_ptr (virDomainPtr **allDomains, virConnectPtr conn)
{
  int numDomains = 0;
  int numActiveDomains, numInactiveDomains;
  char **inactiveDomains;
  int *activeDomains, i;

  /* Number of active & inactive domains. */
  numActiveDomains = virConnectNumOfDomains (conn);
  numInactiveDomains = virConnectNumOfDefinedDomains (conn);

  /* Malloc the variables for holding domains. */
  *allDomains = (virDomainPtr *) malloc (sizeof (virDomainPtr) * (numActiveDomains + numInactiveDomains));
  inactiveDomains = (char **) malloc (sizeof (char *) * numInactiveDomains);
  activeDomains = (int *) malloc (sizeof (int) * numActiveDomains);

  /* I don't know why count domains number again.
   * Just from The Application Development Guide PDF. */
  numActiveDomains = virConnectListDomains (conn, activeDomains, numActiveDomains);
  numInactiveDomains = virConnectListDefinedDomains (conn, inactiveDomains, numInactiveDomains);

  /* Save active domain's virDomainPtr into allDomains. */
  for (i = 0; i < numActiveDomains; i++)
  {
    (*allDomains)[numDomains] = virDomainLookupByID (conn, activeDomains[i]);
    numDomains++;
  }
  free (activeDomains);

  /* Save inactive domain's virDomainPtr into allDomains. */
  for (i = 0; i < numInactiveDomains; i++)
  {
    (*allDomains)[numDomains] = virDomainLookupByName (conn, inactiveDomains[i]);
    free (inactiveDomains[i]);
    numDomains++;
  }
  free (inactiveDomains);

  /* Return the number of domains. */
  return numDomains;
}

char *
getDomainState (char state)
{
  switch (state)
  {
    case 0:
      return "NOSTATE";
    case 1:
      return "RUNNING";
    case 2:
      return "BLOCKED";
    case 3:
      return "PAUSED";
    case 4:
      return "SHUTDOWN";
    case 5:
      return "SHUTOFF";
    case 6:
      return "CRASHED";
    case 7:
      return "PMSUSPEND";
    case 8:
      return "LAST";
    default:
      return "ERROR";
  }
}

double
get_domain_cpu_usage (struct domain_metadata *domain_metadatas, int guestCpus)
{
  double pcentbase, usage;

  pcentbase = ((((*domain_metadatas).curCpuTime - (*domain_metadatas).prevCpuTime) * 100.0) /
      	       (((*domain_metadatas).curTimestamp - (*domain_metadatas).prevTimestamp) * 1000.0 * 1000.0 * 1000.0));
  usage = pcentbase / guestCpus;
  (*domain_metadatas).prevCpuTime = (*domain_metadatas).curCpuTime;
  (*domain_metadatas).prevTimestamp = (*domain_metadatas).curTimestamp;

  return usage < 100.0 ? (usage > 0.0 ? usage : 0.0) : 100.0;
}

void
get_domains_informations (virConnectPtr conn, virDomainPtr *allDomains, int numDomains, struct domain_metadata *domain_metadatas)
{
  char row[ROW_LENGTH], hostname[21];
  int i;
  virDomainInfo info;
  virDomainMemoryStatStruct mem_stat;
  double cpu_usage;

  for (i = 0; i < numDomains; i++)
  {
    /* Zeroing */
    memset (row, '\0', ROW_LENGTH);
    memset (&mem_stat, '\0', sizeof (virDomainMemoryStatStruct));
    memset (hostname, '\0', sizeof (hostname));
    memset (&info, '\0', sizeof (virDomainInfo));

    /* Extract information about a domain. 
     * If error occured, continue to loop */
    if (virDomainGetInfo (allDomains[i], &info) == -1)
      continue;

    /* Get memory stats only when running state. */
    if (info.state < 2)
    {
      if (virDomainMemoryStats (allDomains[i], &mem_stat, 1, 0) == -1)
	mem_stat.val = 0;
    }

    /* Save current UNIX timestamp and vCPU timestamp.
     * Calculate cpu usage by both timestamp informations. */
    domain_metadatas[i].curTimestamp = time (NULL);
    domain_metadatas[i].curCpuTime = info.cpuTime;
    cpu_usage = get_domain_cpu_usage (domain_metadatas + i, info.nrVirtCpu);

    /* Trim hostname. Prevent stacksmashing. */
    strncpy (hostname, virDomainGetName (allDomains[i]), 20);
    
    sprintf (row, "  %-22s %10s %5luMb %5lluMb %7d %6.1lf%s %9s\n", 
	hostname,
	getDomainState (info.state),
	info.memory / 1024,
	mem_stat.val / 1024,
	info.nrVirtCpu,
	cpu_usage,
	"%%",
	virDomainGetOSType (allDomains[i]));

    printw (row);
  }
}

void
print_title (char *msg)
{
  /* Color reverse */
  attron (A_REVERSE);
  printw ("%-79s\n", msg);
  attroff (A_REVERSE);
}

void
print_columns ()
{
  attron (A_REVERSE);
  printw ("%-24s %10s %7s %7s %7s %7s %9s  \n", "  NAME", "STATE", "MEMORY", "USING", "#vCPUs", "CPU%", "OS-TYPE");
  attroff (A_REVERSE);
}

int
main (void) 
{
  char host_information[HOST_INFORMATION_BUFFER_SIZE];
  virConnectPtr conn;
  virDomainPtr *allDomains = NULL;
  int numDomains;
  int ch;
  struct domain_metadata *domain_metadatas;

  /* Start curses mode.
   * Loop every 1seconds and cursor invisible. */
  initscr ();
  halfdelay (10);
  curs_set (0);	

  /* Open a connection for full read-write access. */
  conn = virConnectOpen ("qemu:///system");
  if (conn == NULL)
  {
    fprintf (stderr, "Failed to open connection to qemu:///system\n");
    return 1;
  }

  /* Get all domains */
  numDomains = get_domains_ptr (&allDomains, conn);
  domain_metadatas = (struct domain_metadata *) calloc (numDomains, sizeof (struct domain_metadata));

  /* Main loop. */
  while ((ch = getch()) != 'q')
  {
    move (0, 0);
    /* Title of the host informations */
    print_title ("  Host informations");
    
    /* Get and print the host informations */
    get_host_informations (conn, host_information);
    printw ("%s\n", host_information);

    /* Title of the inactive domains informations */
    print_title ("  Domains informations");
    print_columns ();

    /* Get and print the domains informations */
    get_domains_informations (conn, allDomains, numDomains, domain_metadatas);

    refresh ();
  }

  /* Deallocate memory */
  if (domain_metadatas != NULL)
    free (domain_metadatas);
  if (allDomains != NULL)
    free (allDomains);

  /* Close a connection. */
  virConnectClose (conn);
  
  /* End curses mode. */
  endwin ();
  return 0;
}
