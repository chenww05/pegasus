#include <list>
#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <unistd.h>
#include <math.h>

#include "dag.h"
#include "engine.h"
#include "master.h"
#include "worker.h"
#include "failure.h"
#include "log.h"
#include "protocol.h"

static char *program;
static int rank;

void usage() {
    if (rank == 0) {
        fprintf(stderr,
            "Usage: %s [options] DAGFILE\n"
            "\n"
            "Options:\n"
            "   -h|--help            Print this message\n"
            "   -v|--verbose         Increase logging level\n"
            "   -q|--quiet           Decrease logging level\n"
            "   -o|--stdout PATH     Path to stdout file for tasks\n"
            "   -e|--stderr PATH     Path to stderr file for tasks\n"
            "   -s|--skip-rescue     Ignore existing rescue file (still creates one)\n"
            "   -m|--max-failures N  Stop submitting tasks after N tasks have failed\n"
            "   -t|--tries N         Try tasks N times before marking them failed\n"
            "   -n|--nolock          Do not try to lock DAGFILE\n"
            "   -r|--rescue PATH     Path to rescue file [default: DAGFILE.rescue]\n"
            "   --host-script PATH   Path to script that will be launched on each host\n"
            "   --host-memory N      Amount of memory per host in MB\n"
            "   --host-cpus N        Number of CPUs per host\n"
            "   --strict-limits      Enforce strict task resource limits\n",
            program
        );
    }
}

void argerror(const std::string message) {
    if (rank == 0) {
        fprintf(stderr, "%s\n", message.c_str());
    }
}

int mpidag(int argc, char *argv[]) {
    int numprocs;
    program = argv[0];
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    
    std::list<char *> flags;
    for (int i=1; i<argc; i++) {
        flags.push_back(argv[i]);
    }
    
    std::string outfile = "stdout";
    std::string errfile = "stderr";
    std::list<std::string> args;
    int loglevel = LOG_INFO;
    bool skiprescue = false;
    int max_failures = 0;
    int tries = 1;
    bool lock = true;
    std::string rescuefile = "";
    std::string host_script = "";
    unsigned host_memory = 0;
    unsigned host_cpus = 0;
    bool strict_limits = false;
    
    // Environment variable defaults
    char *env_host_script = getenv("PMC_HOST_SCRIPT");
    if (env_host_script != NULL) {
        host_script = env_host_script;
    }
    
    char *env_host_memory = getenv("PMC_HOST_MEMORY");
    if (env_host_memory != NULL) {
        if (sscanf(env_host_memory, "%u", &host_memory) != 1) {
            argerror("Invalid value for PMC_HOST_MEMORY");
            return 1;
        }
    }
    
    char *env_host_cpus = getenv("PMC_HOST_CPUS");
    if (env_host_cpus != NULL) {
        if (sscanf(env_host_cpus, "%u", &host_cpus) != 1) {
            argerror("Invalid value of PMC_HOST_CPUS");
            return 1;
        }
    }
    
    while (flags.size() > 0) {
        std::string flag = flags.front();
        if (flag == "-h" || flag == "--help") {
            usage();
            return 0;
        } else if (flag == "-o" || flag == "--stdout") {
            flags.pop_front();
            if (flags.size() == 0) {
                argerror("-o/--stdout requires PATH");
                return 1;
            }
            outfile = flags.front();
        } else if (flag == "-e" || flag == "--stderr") {
            flags.pop_front();
            if (flags.size() == 0) {
                argerror("-e/--stderr requires PATH");
                return 1;
            }
            errfile = flags.front();
        } else if (flag == "-q" || flag == "--quiet") {
            loglevel -= 1;
        } else if (flag == "-v" || flag == "--verbose") {
            loglevel += 1;
        } else if (flag == "-s" || flag == "--skip-rescue") {
            skiprescue = true;
        } else if (flag == "-m" || flag == "--max-failures") {
            flags.pop_front();
            if (flags.size() == 0) {
                argerror("-m/--max-failures requires N");
                return 1;
            }
            std::string N = flags.front();
            if (!sscanf(N.c_str(), "%d", &max_failures)) {
                argerror("N for -m/--max-failures is invalid");
                return 1;
            }
            if (max_failures < 0) {
                argerror("N for -m/--max-failures must be >= 0");
                return 1;
            }
        } else if (flag == "-t" || flag == "--tries") {
            flags.pop_front();
            if (flags.size() == 0) {
                argerror("-t/--tries requires N");
                return 1;
            }
            std::string N = flags.front();
            if (!sscanf(N.c_str(), "%d", &tries)) {
                argerror("N for -t/--tries is invalid");
                return 1;
            }
            if (tries < 1) {
                argerror("N for -t/--tries must be >= 1");
                return 1;
            }
        } else if (flag == "-n" || flag == "--nolock") {
            lock = false;
        } else if (flag == "-r" || flag == "--rescue") {
            flags.pop_front();
            if (flags.size() == 0) {
                argerror("-r/--rescue requires PATH");
                return 1;
            }
            rescuefile = flags.front();
        } else if (flag == "--host-script") {
            flags.pop_front();
            if (flags.size() == 0) {
                argerror("--host-script requires PATH");
                return 1;
            }
            host_script = flags.front();
        } else if (flag == "--host-memory") {
            flags.pop_front();
            if (flags.size() == 0) {
                argerror("--host-memory requires N");
                return 1;
            }
            std::string host_memory_string = flags.front();
            if (sscanf(host_memory_string.c_str(), "%u", &host_memory) != 1) {
                argerror("Invalid value for --host-memory");
                return 1;
            }
            if (host_memory < 1) {
                argerror("--host-memory must be an integer >= 1");
                return 1;
            }
        } else if (flag == "--host-cpus") {
            flags.pop_front();
            if (flags.size() == 0) {
                argerror("--host-cpus requires N");
                return 1;
            }
            std::string host_cpus_string = flags.front();
            if (sscanf(host_cpus_string.c_str(), "%u", &host_cpus) != 1) {
                argerror("Invalid value for --host-cpus");
                return 1;
            }
            if (host_cpus < 1) {
                argerror("--host-cpus must be an integer >= 1");
                return 1;
            }
        } else if (flag == "--strict-limits") {
            strict_limits = true;
        } else if (flag[0] == '-') {
            std::string message = "Unrecognized argument: ";
            message += flag[0];
            argerror(message);
            return 1;
        } else {
            args.push_back(flag);
        }
        flags.pop_front();
    }
    
    if (args.size() == 0) {
        usage();
        return 1;
    }
    
    if (args.size() > 1) {
        argerror("Invalid argument\n");
        return 1;
    }
    
    std::string dagfile = args.front();
    
    log_set_level(loglevel);
    
    if (numprocs < 2) {
        fprintf(stderr, "At least one worker process is required\n");
        return 1;
    }
    
    // Everything is pretty deterministic up until the processes reach
    // this point. Once we get here the different processes can diverge 
    // in their behavior for many reasons (file systems issues, bad nodes,
    // etc.), so be careful how failures are handled after this point
    // and make sure MPI_Abort is called when something bad happens.
    
    if (rank == 0) {
        // If no rescue file specified, use default
        if (rescuefile == "") {
            rescuefile = dagfile + ".rescue";
        }
        
        std::string oldrescue = rescuefile;
        std::string newrescue = rescuefile;
        
        if (skiprescue) {
            // User does not want to read old rescue file
            oldrescue = "";
        }
        
        log_debug("Using old rescue file: %s", oldrescue.c_str());
        log_debug("Using new rescue file: %s", newrescue.c_str());
        
        bool has_host_script = ("" != host_script);
        
        DAG dag(dagfile, oldrescue, lock, tries);
        Engine engine(dag, newrescue, max_failures);
        return Master(program, engine, dag, dagfile, outfile, errfile, has_host_script).run();
    } else {
        return Worker(host_script, host_memory, host_cpus, strict_limits).run();
    }
}

int main(int argc, char *argv[]) {
    try {
        MPI_Init(&argc, &argv);
        MPI_Errhandler_set(MPI_COMM_WORLD, MPI_ERRORS_ARE_FATAL);
        int rc = mpidag(argc, argv);
        MPI_Finalize();
        return rc;
    } catch (std::exception &error) {
        // If we catch an execption here, then one of the
        // processes has hit an unsolvable problem and we
        // need to abort the entire workflow.
        fprintf(stderr, "ABORT: %s\n", error.what());
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
}