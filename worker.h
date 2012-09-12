#ifndef WORKER_H
#define WORKER_H

#include <string>

// Give the host script 60 seconds to exit
#define HOST_SCRIPT_TIMEOUT 60

// Give processes in the host script's process 
// group 5 seconds after SIGTERM before sending SIGKILL
#define HOST_SCRIPT_GRACE_PERIOD 5

class Pipe {
public:
    std::string varname;
    std::string filename;
    int readfd;
    int writefd;
    std::string buffer;
    
    Pipe(std::string forward, int readfd, int writefd);
    ~Pipe();
    void save(char *buf, unsigned n);
    const char *data();
    unsigned size();
    void close();
    void closeread();
    void closewrite();
};

class Worker {
    std::string outfile;
    std::string errfile;
    
    int rank;
    int host_rank;
    
    std::string host_script;
    pid_t host_script_pgid;
    
    std::string host_name;
    unsigned int host_memory;
    unsigned int host_cpus;
    
    bool strict_limits;
    
    void run_host_script();
    void kill_host_script_group();
public:
    Worker(const std::string &outfile, const std::string &errfile, const std::string &host_script, unsigned host_memory = 0, unsigned host_cpus = 0, bool strict_limits = false);
    ~Worker();
    int run();
};

#endif /* WORKER_H */