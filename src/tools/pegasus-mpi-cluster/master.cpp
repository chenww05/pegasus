#include <map>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <sys/time.h>

#include "master.h"
#include "failure.h"
#include "comm.h"
#include "protocol.h"
#include "log.h"
#include "tools.h"

using std::string;
using std::vector;
using std::map;

#define MESSAGE_DUMP_FILE "pmc.message.dmp"

static bool ABORT = false;

static void on_signal(int signo) {
    log_fatal("Caught signal %d", signo);
    if (signo == SIGALRM) {
        log_fatal("Workflow wall time exceeded!");
    }
    ABORT = true;
}

static void log_invalid_message(Message *mesg) {
    /* Log as much information about the message as we can */
    log_error("Master got invalid message: size=%u, source=%d", 
              mesg->msgsize, mesg->source);
    
    /* Write the message to a memory dump file */
    log_error("Writing invalid message to %s", MESSAGE_DUMP_FILE);
    FILE *dump = fopen(MESSAGE_DUMP_FILE, "w");
    if (dump == NULL) {
        log_error("Unable to open message dump file: %s", strerror(errno));
        return;
    }
    if (fwrite(mesg->msg, mesg->msgsize, 1, dump) != 1) {
        log_error("Incomplete message dump: %s", strerror(errno));
    }
    if (fclose(dump)) {
        log_error("Error closing message dump file: %s", strerror(errno));
    }
}

void Host::log_status() {
    log_trace("Host %s now has %u MB, %u CPUs, and %u slots free", 
        this->host_name.c_str(), this->memory, this->cpus, this->slots);
}

JobstateLog::JobstateLog(const string &path) {
    this->path = path;
    this->logfile = NULL;
}

JobstateLog::~JobstateLog() {
    close();
}

void JobstateLog::open() {
    if (this->logfile != NULL) {
        return;
    }
    
    this->logfile = fopen(path.c_str(), "a");
    if (this->logfile == NULL) {
        myfailures("Unable to open %s", path.c_str());
    }
}

void JobstateLog::close() {
    if (logfile != NULL) {
        fclose(logfile);
        logfile = NULL;
    }
}

void JobstateLog::on_event(WorkflowEvent event, Task *task) {
    if (!logfile) {
        open();
    }
    
    double now = current_time();
    switch (event) {
        case TASK_QUEUED:
            fprintf(logfile, "%0.6lf %s SUBMIT %d.0 - - %u\n", now, 
                    task->name.c_str(), task->submit_seq, task->submit_seq);
            break;
        case TASK_SUBMIT:
            fprintf(logfile, "%0.6lf %s EXECUTE %d.0 - - %u\n", now, 
                    task->name.c_str(), task->submit_seq, task->submit_seq);
            break;
        case TASK_SUCCESS:
            fprintf(logfile, "%0.6lf %s JOB_TERMINATED %d.0 - - %u\n", now, 
                    task->name.c_str(), task->submit_seq, task->submit_seq);
            fprintf(logfile, "%0.6lf %s JOB_SUCCESS %d - - %u\n", now, 
                    task->name.c_str(), task->last_exitcode, task->submit_seq);
            break;
        case TASK_FAILURE:
            fprintf(logfile, "%0.6lf %s JOB_TERMINATED %d.0 - - %u\n", now, 
                    task->name.c_str(), task->submit_seq, task->submit_seq);
            fprintf(logfile, "%0.6lf %s JOB_FAILURE %d - - %u\n", now,
                    task->name.c_str(), task->last_exitcode, task->submit_seq);
            break;
        case WORKFLOW_START:
            fprintf(logfile, "%0.6lf INTERNAL *** PMC_STARTED ***\n", now);
            break;
        case WORKFLOW_SUCCESS:
            fprintf(logfile, "%0.6lf INTERNAL *** PMC_FINISHED 0 ***\n", now);
            break;
        case WORKFLOW_FAILURE:
            fprintf(logfile, "%0.6lf INTERNAL *** PMC_FINISHED 1 ***\n", now);
            break;
    }
}

DAGManLog::DAGManLog(const string &logpath, const string &dagpath) {
    this->logpath = logpath;
    this->dagpath = filename(dagpath);
    this->logfile = NULL;
}

DAGManLog::~DAGManLog() {
    close();
}

void DAGManLog::open() {
    if (this->logfile != NULL) {
        return;
    }
    
    this->logfile = fopen(logpath.c_str(), "w");
    if (this->logfile == NULL) {
        myfailures("Unable to open %s", logpath.c_str());
    }
}

void DAGManLog::close() {
    if (logfile != NULL) {
        fclose(logfile);
        logfile = NULL;
    }
}

void DAGManLog::on_event(WorkflowEvent event, Task *task) {
    if (!logfile) {
        open();
    }
    
    /* Format the timestamp for the log file entry */
    time_t ts;
    ::time(&ts);
    struct tm now;
    ::localtime_r(&ts, &now);
    char date[18];
    sprintf(date, /*mm/dd/yy hh:mm:ss*/ "%02d/%02d/%02d %02d:%02d:%02d", 
            now.tm_mon+1, now.tm_mday, now.tm_year-100, 
            now.tm_hour, now.tm_min, now.tm_sec);
    
    switch (event) {
        case TASK_QUEUED:
            fprintf(logfile, "%s Submitting Condor Node %s job(s)...\n", 
                    date, task->name.c_str());
            fprintf(logfile, "%s Event: ULOG_SUBMIT for Condor Node %s (%d.0)\n",
                    date, task->name.c_str(), task->submit_seq);
            break;
        case TASK_SUBMIT:
            fprintf(logfile, "%s Event: ULOG_EXECUTE for Condor Node %s (%d.0)\n",
                    date, task->name.c_str(), task->submit_seq);
            break;
        case TASK_SUCCESS:
            fprintf(logfile, "%s Event: ULOG_JOB_TERMINATED for Condor Node %s (%d.0)\n",
                    date, task->name.c_str(), task->submit_seq);
            fprintf(logfile, "%s Node %s job proc (%d.0) completed successfully.\n",
                    date, task->name.c_str(), task->submit_seq);
            break;
        case TASK_FAILURE:
            fprintf(logfile, "%s Event: ULOG_JOB_TERMINATED for Condor Node %s (%d.0)\n",
                    date, task->name.c_str(), task->submit_seq);
            fprintf(logfile, "%s Node %s job proc (%d.0) failed with status %d.\n",
                    date, task->name.c_str(), task->submit_seq, task->last_exitcode);
            break;
        case WORKFLOW_START:
            fprintf(logfile, "%s This is a fake DAGMan log file generated by PMC "
                    "for the purpose of tricking monitord\n", date);
            fprintf(logfile, "%s ** condor_scheduniv_exec.0.0 (CONDOR_DAGMAN) "
                    "STARTING UP\n", date);
            fprintf(logfile, "%s ** PID = %d\n", date, getpid());
            fprintf(logfile, "%s Parsing %s ...\n", date, dagpath.c_str()); 
            break;
        case WORKFLOW_SUCCESS:
            fprintf(logfile, "%s **** condor_scheduniv_exec.0.0 (condor_DAGMAN) "
                    "pid %d EXITING WITH STATUS 0\n", date, getpid());
            break;
        case WORKFLOW_FAILURE:
            fprintf(logfile, "%s **** condor_scheduniv_exec.0.0 (condor_DAGMAN) "
                    "pid %d EXITING WITH STATUS 1\n", date, getpid());
            break;
    }
}

Master::Master(Communicator *comm, const string &program, Engine &engine,
        DAG &dag, const string &dagfile, const string &outfile,
        const string &errfile, bool has_host_script, double max_wall_time,
        const string &resourcefile, bool per_task_stdio, int maxfds) {
    this->comm = comm;
    this->program = program;
    this->dagfile = dagfile;
    this->outfile = outfile;
    this->errfile = errfile;
    this->engine = &engine;
    this->dag = &dag;
    this->has_host_script = has_host_script;
    this->max_wall_time = max_wall_time;

    this->submitted_count = 0;
    this->success_count = 0;
    this->failed_count = 0;

    this->start_time = 0.0;
    this->finish_time = 0.0;
    this->wall_time = 0.0;

    this->total_cpus = 0;
    this->total_runtime = 0.0;

    this->memory_avail = 0;
    this->cpus_avail = 0;
    this->slots_avail = 0;

    // Determine the number of workers we have
    int numprocs = comm->size();
    this->numworkers = numprocs - 1;
    if (numworkers == 0) {
        myfailure("Need at least 1 worker");
    }

    if (resourcefile == "") {
        this->resource_log = NULL;
    } else {
        this->resource_log = fopen(resourcefile.c_str(), "a");
    }

    this->per_task_stdio = per_task_stdio;

    // Task submit sequence starts at 1
    this->task_submit_seq = 1;

    this->fdcache = new FDCache(maxfds);
}

Master::~Master() {
    vector<Slot *>::iterator s;
    for (s = slots.begin(); s != slots.end(); s++) {
        delete *s;
    }

    vector<Host *>::iterator h;
    for (h = hosts.begin(); h != hosts.end(); h++) {
        delete *h;
    }

    if (resource_log != NULL && fileno(resource_log) > 2) {
        log_trace("Closing resource log");
        fclose(resource_log);
    }

    if (fdcache != NULL) {
        fdcache->close();
        delete fdcache;
        fdcache = NULL;
    }
}

void Master::add_listener(WorkflowEventListener *l) {
    listeners.push_back(l);
}

void Master::publish_event(WorkflowEvent event, Task *task) {
    list<WorkflowEventListener *>::iterator i;
    for (i=listeners.begin(); i!=listeners.end(); i++) {
        (*i)->on_event(event, task);
    }
}

void Master::submit_task(Task *task, int rank) {
    log_debug("Submitting task %s to slot %d", task->name.c_str(), rank);
    
    CommandMessage cmd(task->name, task->command, task->pegasus_id, 
            task->memory, task->cpus, task->pipe_forwards, task->file_forwards);
    comm->send_message(&cmd, rank);
    
    publish_event(TASK_SUBMIT, task);
    
    this->submitted_count++;
}

void Master::wait_for_results() {
    // This will process all the waiting messages. If there are none 
    // waiting, then it will block until one arrives. If there are 
    // several waiting, then it will process them all and return without 
    // waiting.
    unsigned int tasks = 0;
    unsigned int messages = 0;
    do {
        
        /* If the user specifies a maximum wall time for the workflow, then 
         * the master sets a timeout by calling alarm(), which causes the 
         * kernel to send a SIGALRM when the timer expires. Also, on most 
         * PBS systems when the max wall time is reached PBS sends the 
         * process a SIGTERM. When PMC catches these signals it sets the 
         * ABORT flag. In many MPI implementations, however, signals do 
         * not interrupt blocking message calls such as MPI_Recv. So we 
         * cannot be waiting in MPI_Recv when the signal is caught or we 
         * cannot respond to it. So we give a timeout to recv_message so 
         * that it does not block forever.
         */
        double timeout = 0;
        if (max_wall_time > 0) {
            double now = current_time();
            double deadline = start_time + (max_wall_time * 60.0);
            timeout = deadline - now;
        }
        log_trace("Waiting for result");
        Message *mesg = comm->recv_message(timeout);
        if (mesg == NULL || ABORT) {
            ABORT = true;
            return;
        }
        messages++;
        if (ResultMessage *res = dynamic_cast<ResultMessage *>(mesg)) {
            process_result(res);
            tasks++;
        } else if (IODataMessage *iod = dynamic_cast<IODataMessage *>(mesg)) {
            process_iodata(iod);
        } else {
            myfailure("Expected result or I/O data message");
        }
        delete mesg;
        
        // We need to do this while tasks == 0 because the caller
        // of this method assumes that it will process at least one
        // task before returning
    } while (comm->message_waiting() || tasks == 0);
    
    log_trace("Processed %u task(s) and %u message(s) this cycle", 
            tasks, messages);
}

void Master::process_iodata(IODataMessage *mesg) {
    /* Perform some sanity checks on the message. This
     * was added because of an issue with mangled messages
     * on TACC Stampede.
     */
    if (mesg->size > mesg->msgsize) {
        log_invalid_message(mesg);
        myfailure("Invalid I/O message: invalid size");
    }
    if (mesg->filename.size() == 0) {
        log_invalid_message(mesg);
        myfailure("Invalid I/O message: bad filename");
    }
    if (mesg->task.size() == 0) {
        log_invalid_message(mesg);
        myfailure("Invalid I/O message: bad task name");
    }
    
    log_trace("Got %u bytes for file %s", mesg->size, mesg->filename.c_str());
    
    if (fdcache->write(mesg->filename, mesg->data, mesg->size) < 0) {
        log_error("Error writing %d bytes to %s for task %s", mesg->size,
                mesg->filename.c_str(), mesg->task.c_str());
        
        Task *task = this->dag->get_task(mesg->task);
        if (task == NULL) {
            // If the task is not found then there is a problem, but
            // we can probably just ignore it at this point.
            myfailure("Unable to find task %s for I/O failure", 
                      mesg->task.c_str());
        }
        
        task->io_failed = true;
    }
}

void Master::process_result(ResultMessage *mesg) {
    string name = mesg->name;
    int exitcode = mesg->exitcode;
    int rank = mesg->source;
    double task_runtime = mesg->runtime;
    
    total_runtime += task_runtime;
    
    Task *task = this->dag->get_task(name);

    if (task->io_failed) {
        // If there was an error processing I/O data for this task, 
        // then record it as a failure
        
        log_error("Task %s failed due to collective I/O errors", name.c_str());
        this->failed_count++;
        
        // Set the exitcode to something non-zero to force the failure
        exitcode = 256;
        
        // Reset the flag so that, if the task is retried, it won't
        // automatically fail again
        task->io_failed = false;
    } else if (exitcode == 0) {
        log_debug("Task %s finished with exitcode %d", name.c_str(), exitcode);
        this->success_count++;
    } else {
        log_error("Task %s failed with exitcode %d", name.c_str(), exitcode);
        this->failed_count++;
    }
    
    task->last_exitcode = exitcode;
    
    this->engine->mark_task_finished(task, exitcode);
    
    if (exitcode == 0) {
        publish_event(TASK_SUCCESS, task);
    } else {
        publish_event(TASK_FAILURE, task);
    }
    
    // Mark slot idle
    log_trace("Worker %d is idle", rank);
    Slot *slot = slots[rank-1];
    
    // Return resources to host
    release_resources(slot->host, task->cpus, task->memory);

    // Mark slot as free
    free_slots.push_back(slot);
}

void Master::allocate_resources(Host *host, unsigned cpus, unsigned memory) {

    memory_avail -= memory;
    cpus_avail -= cpus;
    slots_avail -= 1;

    host->memory -= memory;
    host->cpus -= cpus;
    host->slots -= 1;

    host->log_status();
    log_resources(host->slots, host->cpus, host->memory, host->host_name);
    log_resources(slots_avail, cpus_avail, memory_avail, "*");
}

void Master::release_resources(Host *host, unsigned cpus, unsigned memory) {

    memory_avail += memory;
    cpus_avail += cpus;
    slots_avail += 1;

    host->cpus += cpus;
    host->memory += memory;
    host->slots += 1;

    host->log_status();
    log_resources(host->slots, host->cpus, host->memory, host->host_name);
    log_resources(slots_avail, cpus_avail, memory_avail, "*");
}

void Master::log_resources(unsigned slots, unsigned cpus, unsigned memory, const string &hostname) {
    if (resource_log == NULL) {
        return;
    }

    struct timeval ts;

    gettimeofday(&ts, NULL);
    double timestamp = ts.tv_sec + (ts.tv_usec / 1.0e6);

    fprintf(resource_log, "%lf,%u,%u,%u,%s\n", 
            timestamp, slots, cpus, memory, hostname.c_str());
}

void Master::merge_all_task_stdio() {
    // If we have per task stdio we don't need to merge the outputs
    if (per_task_stdio) {
        return;
    }
    
    log_info("Merging task stdio from workers...");
    
    FILE *task_stdout = stdout;
    FILE *task_stderr = stderr;
    
    // Open task stdout
    if (outfile == "stdout") {
        task_stdout = stdout;
    } else {
        task_stdout = fopen(this->outfile.c_str(), "w");
        if (task_stdout == NULL) {
            myfailures("Unable to open stdout file: %s\n", this->outfile.c_str());
        }
    }
    
    // Open task stderr
    if (errfile == "stderr") {
        task_stderr = stderr;
    } else if (errfile == outfile) {
        task_stderr = task_stdout;
    } else {
        task_stderr = fopen(this->errfile.c_str(), "w");
        if (task_stderr == NULL) {
            myfailures("Unable to open stderr file: %s\n", this->outfile.c_str());
        }
    }
    
    char rankstr[10];
    for (int i=1; i<=numworkers; i++) {
        log_debug("Merging stdio from worker %d...", i);

        sprintf(rankstr, "%d", i);
        
        string task_outfile = this->dagfile + ".out." + rankstr;
        this->merge_task_stdio(task_stdout, task_outfile, "stdout");
        
        string task_errfile = this->dagfile + ".err." + rankstr;
        this->merge_task_stdio(task_stderr, task_errfile, "stderr");
    }
    
    if (fileno(task_stdout) > 2) {
        fclose(task_stdout);
    }
    
    if (fileno(task_stderr) > 2) {
        fclose(task_stderr);
    }
}

void Master::merge_task_stdio(FILE *dest, const string &srcfile, const string &stream) {
    log_trace("Merging %s file: %s", stream.c_str(), srcfile.c_str());
    
    FILE *src = fopen(srcfile.c_str(), "r");
    if (src == NULL) {
        // The file may not exist if the worker didn't run any tasks, just print a warning
        if (errno == ENOENT) {
            log_warn("No %s file: %s", stream.c_str(), srcfile.c_str());
            return;
        } else {
            myfailures("Unable to open task %s file: %s", stream.c_str(), srcfile.c_str());
        }
    }
    
    char buf[BUFSIZ];
    while (1) {
        int r = fread(buf, 1, BUFSIZ, src);
        if (r < 0) {
            myfailures("Error reading source file: %s", srcfile.c_str());
        }
        if (r == 0) {
            break;
        }
        int w = fwrite(buf, 1, r, dest);
        if (w < r) {
            myfailures("Error writing to dest file");
        }
    }
    
    fclose(src);
    
    if (unlink(srcfile.c_str())) {
        myfailures("Unable to delete task %s file: %s", stream.c_str(), srcfile.c_str());
    }
}

void Master::write_cluster_summary(bool failed) {
    // pegasus cluster output - used for provenance
    char date[32];
    iso2date(start_time, date, sizeof(date));
    
    char summary[BUFSIZ];
    sprintf(summary, "[cluster-summary stat=\"%s\", tasks=%u, submitted=%u, succeeded=%u, failed=%u, extra=0,"
                 " start=\"%s\", duration=%.3f, pid=%d, app=\"%s\", runtime=%.3f, slots=%d, cpus=%u]\n",
                 failed ? "failed" : "ok", 
                 this->dag->size(),
                 this->submitted_count,
                 this->success_count, 
                 this->failed_count,
                 date,
                 wall_time,
                 getpid(),
                 this->program.c_str(),
                 total_runtime,
                 this->numworkers,
                 this->total_cpus);
    
    int len = strlen(summary);

    // XXX This should probably be written to the task_stdout, but for most
    // pegasus workflows task_stdout = stdout
    int w = fwrite(summary, 1, len, stdout);
    if (w < len) {
        myfailures("Error writing cluster-summary");
    }
}

/*
 * Register all workers, create hosts, create slots. Assign a host-centric 
 * rank to each of the workers. The worker with the lowest global rank on 
 * each host is given host rank 0, the next lowest is given host rank 1, 
 * and so on. The master is not given a host rank.
 */
void Master::register_workers() {
    typedef map<string, Host *> HostMap;
    HostMap hostmap;
    
    typedef map<int, string> HostnameMap;
    HostnameMap hostnames;
    
    // Collect host names from all workers, create host objects
    for (int i=0; i<numworkers; i++) {
        
        RegistrationMessage *msg = dynamic_cast<RegistrationMessage *>(comm->recv_message());
        if (msg == NULL) {
            myfailure("Expected registration message");
        }
        int rank = msg->source;
        string hostname = msg->hostname;
        unsigned int memory = msg->memory;
        unsigned int cpus = msg->cpus;
        delete msg;
        
        hostnames[rank] = hostname;
        
        if (hostmap.find(hostname) == hostmap.end()) {
            // If the host is not found, create a new one
            log_debug("Got new host: name=%s, mem=%u, cpus=%u", hostname.c_str(), memory, cpus);
            Host *newhost = new Host(hostname, memory, cpus);
            hosts.push_back(newhost);
            hostmap[hostname] = newhost;
            
            total_cpus += cpus;
            cpus_avail += cpus;
            memory_avail += memory;
        } else {
            // Otherwise, increment the number of slots available
            Host *host = hostmap[hostname];
            host->slots += 1;
        }
        
        slots_avail += 1;
        
        log_debug("Slot %d on host %s", rank, hostname.c_str());
    }
    
    typedef map<string, int> RankMap;
    RankMap ranks;
    
    // Create slots, assign a host rank to each worker
    for (int rank=1; rank<=numworkers; rank++) {
        string hostname = hostnames.find(rank)->second;
        
        // Find host
        Host *host = hostmap.find(hostname)->second;
        
        // Create new slot
        Slot *slot = new Slot(rank, host);
        slots.push_back(slot);
        free_slots.push_back(slot);
        
        // Compute hostrank for this slot
        RankMap::iterator nextrank = ranks.find(hostname);
        int hostrank = 0;
        if (nextrank != ranks.end()) {
            hostrank = nextrank->second;
        }
        ranks[hostname] = hostrank + 1;
        
        HostrankMessage hrmsg(hostrank);
        comm->send_message(&hrmsg, rank);
        
        log_debug("Host rank of worker %d is %d", rank, hostrank);
    }
    
    // Log the initial resource availability
    for (vector<Host *>::iterator i = hosts.begin(); i!=hosts.end(); i++) {
        Host *host = *i;
        log_resources(host->slots, host->cpus, host->memory, host->host_name);
    }
    log_resources(slots_avail, cpus_avail, memory_avail, "*");
}

void Master::schedule_tasks() {
    log_debug("Scheduling %d tasks on %d slots...", 
        ready_queue.size(), free_slots.size());
    
    int scheduled = 0;
    TaskList deferred_tasks;
    
    while (ready_queue.size() > 0 && free_slots.size() > 0) {
        Task *task = ready_queue.top();
        ready_queue.pop();
        
        log_trace("Scheduling task %s", task->name.c_str());
        
        bool match = false;
        
        for (SlotList::iterator s = free_slots.begin(); s != free_slots.end(); s++) {
            Slot *slot = *s;
            Host *host = slot->host;
            
            // If the task fits, schedule it
            if (host->memory >= task->memory && host->cpus >= task->cpus) {
                
                log_trace("Matched task %s to slot %d on host %s", 
                    task->name.c_str(), slot->rank, host->host_name.c_str());
                
                // Reserve the resources
                allocate_resources(host, task->cpus, task->memory);
                
                submit_task(task, slot->rank);
                
                s = free_slots.erase(s);
                
                // so that the s++ in the loop doesn't skip one
                s--;
                
                match = true;
                scheduled += 1;
                
                // This is to break out of the slot loop so that we can 
                // consider the next task
                break;
            }
        }
        
        if (!match) {
            // If the task could not be scheduled, then we save it 
            // and move on to the next one. It will be requeued later.
            log_trace("No slot found for task %s", task->name.c_str());
            deferred_tasks.push_back(task);
        }
    }
    
    log_debug("Scheduled %d tasks and deferred %d tasks", scheduled, deferred_tasks.size());
    
    // Requeue all the deferred tasks
    for (TaskList::iterator t = deferred_tasks.begin(); t != deferred_tasks.end(); t++) {
        ready_queue.push(*t);
    }
}

void Master::queue_ready_tasks() {
    while (this->engine->has_ready_task()) {
        Task *task = this->engine->next_ready_task();

        log_debug("Queueing task %s", task->name.c_str());
        
        // Assign a submit sequence number to this task
        task->submit_seq = this->task_submit_seq++;
        
        ready_queue.push(task);
        
        publish_event(TASK_QUEUED, task);
    }
}

int Master::run() {
    log_info("Master starting with %d workers", numworkers);
    
    start_time = current_time();

    publish_event(WORKFLOW_START, NULL);
    
    // Install signal handlers
    struct sigaction signal_action;
    signal_action.sa_handler = on_signal;
    signal_action.sa_flags = SA_NODEFER;
    sigemptyset(&signal_action.sa_mask);
    if (sigaction(SIGALRM, &signal_action, NULL) < 0) {
        myfailures("Unable to set signal handler for SIGALRM");
    }
    if (sigaction(SIGTERM, &signal_action, NULL) < 0) {
        myfailures("Unable to set signal handler for SIGTERM");
    }
    
    // Set alarm to interrupt the master when the walltime is up
    if (this->max_wall_time > 0.0) {    
        log_info("Setting max walltime to %lf minutes", this->max_wall_time);
        alarm((unsigned)ceil(max_wall_time * 60.0));
    }
    
    register_workers();
    
    // Check to make sure that there is at least one host capable
    // of executing every task
    for (DAG::iterator t = dag->begin(); t != dag->end(); t++){
        Task *task = (*t).second;
        
        // Check all the hosts for one that can run the task
        bool match = false;
        for (unsigned h=0; h<hosts.size(); h++) {
            Host *host = hosts[h];
            if (host->memory >= task->memory && host->cpus >= task->cpus) {
                match = true;
                break;
            }
        }
        
        if (!match) {
            // There was no host found that was capable of executing the
            // task, so we must abort
            myfailure("FATAL ERROR: No host is capable of running task %s", 
                task->name.c_str());
        }
    }
    
    // If there is a host script, wait here for it to run
    if (has_host_script) {
        comm->barrier();
    }
    
    log_info("Starting workflow");
    double makespan_start = current_time();
    // Keep executing tasks until the workflow is finished or the master
    // needs to abort the workflow due to a signal being caught
    while (!this->engine->is_finished() && !ABORT) {
        queue_ready_tasks();
        schedule_tasks();
        wait_for_results();
    }
	double makespan_finish = current_time();
    
    if (ABORT) {
        log_error("Aborting workflow");
    } else {
        log_info("Workflow finished");
    }
    
    if (this->engine->max_failures_reached()) {
        log_error("Max failures reached: DAG prematurely aborted");
    }
    
    // This must be done before write_cluster_summary so that the
    // wall time can be recorded in the cluster-summary record
    finish_time = current_time();
    wall_time = finish_time - start_time;
    double makespan = makespan_finish - makespan_start;
    
    // Close FDCache here before merging output so that
    // we can be sure the data files are flushed
    fdcache->close();
    
    // Compute resource utilization
    double master_util = total_runtime / (wall_time * (numworkers+1));
    double worker_util = total_runtime / (wall_time * numworkers);
    if (total_runtime <= 0) {
        master_util = 0.0;
        worker_util = 0.0;
    }
    
    log_info("Resource utilization (with master): %lf", master_util);
    log_info("Resource utilization (without master): %lf", worker_util);
    log_info("Total runtime of tasks: %lf seconds (%lf minutes)", total_runtime, total_runtime/60.0);
    log_info("Wall time: %lf seconds (%lf minutes)", wall_time, wall_time/60.0);
    log_info("Makespan: %lf seconds (%lf minutes)", makespan, makespan/60.0);
    log_info("Throughput: %lf tasks/second", success_count/makespan);
    log_info("Bytes sent to workers: %lu", comm->sent());
    log_info("Bytes received from workers: %lu", comm->recvd());
    log_info("File descriptor cache hit rate: %lf", fdcache->hitrate());

    bool failed = ABORT || this->engine->is_failed();
    write_cluster_summary(failed);
    
    if (!per_task_stdio) merge_all_task_stdio();
    
    log_info("Sending workers shutdown messages...");
    for (int i=1; i<=numworkers; i++) {
        log_debug("Sending shutdown message to worker %d", i);
        ShutdownMessage shmsg;
        comm->send_message(&shmsg, i);
    }
    
    if (failed) {
        publish_event(WORKFLOW_FAILURE, NULL);
    } else {
        publish_event(WORKFLOW_SUCCESS, NULL);
    }
    
    if (ABORT) {
        myfailure("Workflow aborted");
        return 1;
    } else if (failed) {
        log_error("Workflow failed");
        return 1;
    } else {
        log_info("Workflow suceeded");
        return 0;
    }
}

