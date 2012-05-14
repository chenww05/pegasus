#include <string>
#include "stdio.h"

#include "stdlib.h"
#include "dag.h"
#include "failure.h"

void test_dag() {
    DAG dag("test/test.dag");
    
    Task *alpha = dag.get_task("Alpha");
    if (alpha == NULL) {
        myfailure("Didn't parse Alpha");
    }
    if (alpha->command.compare("/bin/echo Alpha") != 0) {
        myfailure("Command failed for Alpha: %s", alpha->command.c_str());
    }
    
    Task *beta = dag.get_task("Beta");
    if (beta == NULL) {
        myfailure("Didn't parse Beta");
    }
    if (beta->command.compare("/bin/echo Beta") != 0) {
        myfailure("Command failed for Beta: %s", beta->command.c_str());
    }
    
    if (alpha->children[0] != beta) {
        myfailure("No children");
    }
    
    if (beta->parents[0] != alpha) {
        myfailure("No parents");
    }
}

void test_rescue() {
    DAG dag("test/diamond.dag", "test/diamond.rescue");
    
    Task *a = dag.get_task("A");
    Task *b = dag.get_task("B");
    Task *c = dag.get_task("C");
    Task *d = dag.get_task("D");
    
    if (!a->success) {
        myfailure("A should have been successful");
    }

    if (!b->success) {
        myfailure("B should have been successful");
    }

    if (!c->success) {
        myfailure("C should have been successful");
    }

    if (d->success) {
        myfailure("D should have been failed");
    }
}

void test_pegasus_dag() {
    DAG dag("test/pegasus.dag");
    
    Task *a = dag.get_task("A");
    
    if (a->pegasus_id.compare("1") != 0) {
        myfailure("A should have had pegasus_id");
    }
    
    if (a->pegasus_transformation.compare("mDiffFit:3.3") != 0) {
        myfailure("A should have had pegasus_transformation");
    }
    
    Task *b = dag.get_task("B");
    
    if (b->pegasus_id.compare("2") != 0) {
        myfailure("B should have had pegasus_id");
    }
    
    if (b->pegasus_transformation.compare("mDiff:3.3") != 0) {
        myfailure("B should have had pegasus_transformation");
    }
}

int main(int argc, char *argv[]) {
    test_dag();
    test_rescue();
    test_pegasus_dag();
    return 0;
}