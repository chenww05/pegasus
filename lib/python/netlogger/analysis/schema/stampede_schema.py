"""
Contains the code to create and map objects to the Stampede DB schema
via a SQLAlchemy interface.
"""
__rcsid__ = "$Id: stampede_schema.py 26790 2010-11-22 18:34:13Z mgoode $"
__author__ = "Monte Goode MMGoode@lbl.gov"

from netlogger.analysis.schema._base import SABase, SchemaIntegrityError
try:
    from sqlalchemy import *
    from sqlalchemy import orm, exceptions, func, exc
    from sqlalchemy.orm import relation, backref
except ImportError, e:
    print '** SQLAlchemy library needs to be installed: http://www.sqlalchemy.org/ **\n'
    raise ImportError, e

import time
import warnings
        
# Empty classes that will be populated and mapped
# to tables via the SQLAlch mapper.
class Host(SABase):
    pass
    
class Workflow(SABase):
    pass
    
class Workflowstate(SABase):
    pass

class Job(SABase):
    pass
    
class Jobstate(SABase):
    pass
    
class Task(SABase):
    pass
    
class File(SABase):
    pass
    
class Edge(SABase):
    pass
    
class EdgeStatic(SABase):
    pass
    
def initializeToPegasusDB(db, metadata):
    """
    Function to create the Stampede schema if it does not exist,
    if it does exist, then connect and set up object mappings.
    
    @type   db: SQLAlch db/engine object.
    @param  db: Engine object to initialize.
    @type   metadata: SQLAlch metadata object.
    @param  metadata: Associated metadata object to initialize.
    """
    
    KeyInt = Integer
    # MySQL likes using BIGINT for PKs but some other
    # DB don't like it so swap as needed.
    if db.name == 'mysql':
        KeyInt = BigInteger
        
    if db.name == 'sqlite':
        warnings.filterwarnings('ignore', '.*does \*not\* support Decimal*.')
    
    # st_host definition
    # ==> Information from kickstart output file
    # 
    # site_name = <resource, from invocation element>
    # hostname = <hostname, from invocation element>
    # ip_address = <hostaddr, from invocation element>
    # uname = <combined (system, release, machine) from machine element>
    # total_ram = <ram_total from machine element>
    
    st_host = Table('host', metadata,
                    Column('host_id', KeyInt, primary_key=True, nullable=False),
                    Column('site_name', VARCHAR(255), nullable=False),
                    Column('hostname', VARCHAR(255), nullable=False),
                    Column('ip_address', VARCHAR(255), nullable=False),
                    Column('uname', VARCHAR(255), nullable=True),
                    Column('total_ram', Integer, nullable=True)
    )
    
    #Index('host_id_UNIQUE', st_host.c.host_id, unique=True)
    Index('UNIQUE_HOST', st_host.c.site_name, st_host.c.hostname,
        st_host.c.ip_address, unique=True)
    
    try:
        orm.mapper(Host, st_host)
    except exc.ArgumentError:
        pass
    
    # st_workflow definition
    # ==> Information comes from braindump.txt file
    
    # wf_uuid = autogenerated by database
    # dax_label = label
    # timestamp = pegasus_wf_time
    # submit_hostname = (currently missing)
    # submit_dir = run
    # planner_arguments = (currently missing)
    # user = (currently missing)
    # grid_dn = (currently missing)
    # planner version = pegasus version
    # parent_workflow_id = wf_id of parent workflow
    
    st_workflow = Table('workflow', metadata,
                        Column('wf_id', KeyInt, primary_key=True, nullable=False),
                        Column('wf_uuid', VARCHAR(255), nullable=False),
                        Column('dax_label', VARCHAR(255), nullable=True),
                        Column('timestamp', NUMERIC(precision=16,scale=6), nullable=True),
                        Column('submit_hostname', VARCHAR(255), nullable=True),
                        Column('submit_dir', TEXT, nullable=True),
                        Column('planner_arguments', TEXT, nullable=True),
                        Column('user', VARCHAR(255), nullable=True),
                        Column('grid_dn', VARCHAR(255), nullable=True),
                        Column('planner_version', VARCHAR(255), nullable=True),
                        Column('parent_workflow_id', KeyInt,
                                ForeignKey("workflow.wf_id"), nullable=True)
    )
    
    Index('wf_uuid_UNIQUE', st_workflow.c.wf_uuid, unique=True)
    #Index('FK_PARENT_WF_ID', st_workflow.c.parent_workflow_id, unique=False)
    
    try:
        orm.mapper(Workflow, st_workflow, properties = {
                'child_wf':relation(Workflow, cascade='all'),
                'child_job':relation(Job, backref='st_workflow', cascade='all'),
                'child_wfs':relation(Workflowstate, backref='st_workflow', cascade='all')
            }
        )
    except exc.ArgumentError:
        pass
    
    st_workflowstate = Table('workflowstate', metadata,
    # All three columns are marked as primary key to produce the desired
    # effect - ie: it is the combo of the three columns that make a row
    # unique.
                        Column('wf_id', KeyInt, ForeignKey('workflow.wf_id'), 
                                nullable=False, primary_key=True),
                        Column('state', VARCHAR(255), nullable=False, primary_key=True),
                        Column('timestamp', NUMERIC(precision=16,scale=6), nullable=False, primary_key=True,
                        default=time.time())
    )
    
    #Index('FK_STATE_WF_ID', st_workflowstate.c.wf_id, unique=False)
    Index('UNIQUE_WORKFLOWSTATE', st_workflowstate.c.wf_id, st_workflowstate.c.state, 
        st_workflowstate.c.timestamp, unique=True)
    
    try:
        orm.mapper(Workflowstate, st_workflowstate)
    except exc.ArgumentError:
        pass
    
    # st_job definition
    # ==> Information comes mainly from kickstart output file, 
    #       but also from dagman.out file
    # 
    # job_id = autogenerated
    # host_id = <hostname from invocation element>
    # name = <jobname from dagman.out file>
    # condor id in <condor id from dagman.out file>
    # jobtype = <from .sub file pegasus_job_class>
    # clustered = boolean (true if jobname begins with merged_)
    # site_name = <resource from invocation element>
    # remote_user = <user from invocation element>
    # remote_working_dir = <cwd element>
    # cjob_start_time = (only for clustered job, struct entry of .out file, start)
    # cduration = (only for clustered job, struct entry of .out file, duration)
    
    st_job = Table('job', metadata,
                    Column('job_id', KeyInt, primary_key=True, nullable=False),
                    Column('wf_id', KeyInt,
                            ForeignKey('workflow.wf_id'), nullable=False),
                    Column('job_submit_seq', INT, nullable=False),
                    Column('name', VARCHAR(255), nullable=False),
                    Column('host_id', KeyInt,
                            ForeignKey('host.host_id'), nullable=True),
                    Column('condor_id', VARCHAR(255), nullable=True),
                    Column('jobtype', VARCHAR(255), nullable=False),
                    Column('clustered', BOOLEAN, nullable=True, default=False),
                    Column('site_name', VARCHAR(255), nullable=True),
                    Column('remote_user', VARCHAR(255), nullable=True),
                    Column('remote_working_dir', TEXT, nullable=True),
                    Column('cluster_start_time', NUMERIC(16,6), nullable=True),
                    Column('cluster_duration', NUMERIC(10,3), nullable=True)
    )
    
    #Index('FK_JOB_WF_ID', st_job.c.wf_id, unique=False)
    #Index('FK_JOB_HOST_ID', st_job.c.host_id, unique=False)
    Index('UNIQUE_JOB', st_job.c.wf_id, st_job.c.job_submit_seq, unique=True)
    Index('IDX_JOB_LOOKUP', st_job.c.wf_id, st_job.c.name, unique=False)
    
    Edge.parent_id = Column('parent_id', KeyInt)
    
    try:
        orm.mapper(Job, st_job, properties = {
                'child_tsk':relation(Task, backref='st_job', cascade='all'),
                'child_jst':relation(Jobstate, backref='st_job', cascade='all'),
                'child_file':relation(File, backref='st_job', cascade='all'),
            }
        )
    except exc.ArgumentError:
        pass
    
    # st_jobstate definition
    # ==> Same information that currently goes into jobstate.log file, 
    #       obtained from dagman.out file
    # 
    # job_id = from st_job table (autogenerated)
    # state = from dagman.out file (3rd column of jobstate.log file)
    # timestamp = from dagman,out file (1st column of jobstate.log file)
    
    st_jobstate = Table('jobstate', metadata,
    # All four columns are marked as primary key to produce the desired
    # effect - ie: it is the combo of the four columns that make a row
    # unique.
                        Column('job_id', KeyInt, ForeignKey('job.job_id'), 
                                nullable=False, primary_key=True),
                        Column('state', VARCHAR(255), nullable=False, primary_key=True),
                        Column('timestamp', NUMERIC(precision=16,scale=6), nullable=False, primary_key=True,
                        default=time.time()),
                        Column('jobstate_submit_seq', INT, nullable=False, primary_key=True)
    )
    
    #Index('FK_STATE_JOB_ID', st_jobstate.c.job_id, unique=False)
    Index('UNIQUE_JOBSTATE', st_jobstate.c.job_id, st_jobstate.c.state, 
        st_jobstate.c.timestamp, st_jobstate.c.jobstate_submit_seq, unique=True)
    
    try:
        orm.mapper(Jobstate, st_jobstate)
    except exc.ArgumentError:
        pass
        
    # st_task definition
    # ==> Information comes from kickstart output file
    # 
    # task_id = autogenerated here
    # job_id = from st_job, autogenerated
    # start_time =  <start from mainjob element>
    # duration = <duration, from mainjob element>
    # exitcode = <regular exitcode, from status element>
    # transformation = <transformation from invocation element>
    # arguments = <argument vector, joined by single space>
    
    st_task = Table('task', metadata,
                    Column('task_id', KeyInt, primary_key=True, nullable=False),
                    Column('job_id', KeyInt,
                            ForeignKey('job.job_id'), nullable=False),
                    Column('task_submit_seq', INT, nullable=False),
                    Column('start_time', NUMERIC(16,6), nullable=False,
                            default=time.time()),
                    Column('duration', NUMERIC(10,3), nullable=False),
                    Column('exitcode', INT, nullable=False),
                    Column('transformation', TEXT, nullable=False),
                    Column('executable', TEXT, nullable=False),
                    Column('arguments', TEXT, nullable=True)
    )
    
    #Index('task_id_UNIQUE', st_task.c.task_id, unique=True)
    #Index('FK_TASK_JOB_ID', st_task.c.job_id, unique=False)
    Index('UNIQUE_TASK', st_task.c.job_id, st_task.c.task_submit_seq, unique=True)
    
    try:
        orm.mapper(Task, st_task)
    except exc.ArgumentError:
        pass
    
    # st_file definition
    # ==> Information will come from kickstart output file
    
    st_file = Table('file', metadata,
                    Column('file_id', KeyInt, primary_key=True, nullable=False),
                    Column('job_id', KeyInt,
                            ForeignKey('job.job_id'), nullable=True),
                    Column('lfn', VARCHAR(255), nullable=True),
                    Column('size', INT, nullable=True),
                    Column('md_checksum', VARCHAR(255), nullable=True),
                    Column('type', VARCHAR(255), nullable=True)
    )
    
    #Index('file_id_UNIQUE', st_file.c.file_id, unique=True)
    #Index('FK_FILE_JOB_ID', st_file.c.job_id, unique=False)
    
    try:
        orm.mapper(File, st_file)
    except exc.ArgumentError:
        pass
        
    st_edge_static = Table('edge_static', metadata,
                            Column('wf_uuid', VARCHAR(255), primary_key=True, nullable=False),
                            Column('parent', VARCHAR(255), primary_key=True, nullable=False),
                            Column('child', VARCHAR(255), primary_key=True, nullable=False)
    )
    
    Index('UNIQUE_STATIC_EDGE', st_edge_static.c.wf_uuid, 
            st_edge_static.c.parent, st_edge_static.c.child, unique=True)
    
    try:
        orm.mapper(EdgeStatic, st_edge_static)
    except exc.ArgumentError:
        pass
        
    st_edge = Table('edge', metadata,
                    Column('parent_id', KeyInt,
                            ForeignKey('job.job_id'), primary_key=True, nullable=False),
                    Column('child_id', KeyInt,
                            ForeignKey('job.job_id'), primary_key=True, nullable=False)
    )
    
    Index('UNIQUE_EDGE', st_edge.c.parent_id, st_edge.c.child_id, unique=True)
    
    try:
        orm.mapper(Edge, st_edge)
    except exc.ArgumentError:
        pass
    
    metadata.create_all(db)
    pass
    

def main():
    """
    Example of how to creat SQLAlch object and initialize/create
    to Stampede DB schema.
    """
    db = create_engine('sqlite:///pegasusTest.db', echo=True)
    metadata = MetaData()
    initializeToPegasusDB(db, metadata)
    metadata.bind = db

    sm = orm.sessionmaker(bind=db, autoflush=True, autocommit=False,
        expire_on_commit=True)
    session = orm.scoped_session(sm)
    
    pass
    
if __name__ == '__main__':
    main()