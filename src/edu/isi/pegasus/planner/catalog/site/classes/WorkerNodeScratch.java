/*
 * 
 *   Copyright 2007-2008 University Of Southern California
 * 
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 *   Unless required by applicable law or agreed to in writing,
 *   software distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 * 
 */

package edu.isi.pegasus.planner.catalog.site.classes;

import java.io.Writer;
import java.io.IOException;


/**
 * This data class describes the scratch area on a head node. The difference
 * from the worker node scratch is that it additionally has a worker shared directory
 * that designates the shared directory amongst the worker nodes.
 * 
 * 
 * @version $Revision$
 * @author Karan Vahi
 */
public class WorkerNodeScratch extends StorageType {
    
    
    /**
     * The directory shared only amongst the worker nodes.
     */
    protected WorkerSharedDirectory mWorkerShared;
    
    /**
     * The default constructor
     */
    public WorkerNodeScratch() {
        super();
        mWorkerShared = null;
    }
    
    /**
     * The overloaded constructor
     * 
     * @param type  StorageType
     */
    public WorkerNodeScratch( StorageType type ) {
        this( type.getLocalDirectory(), type.getSharedDirectory() );
        mWorkerShared = null;
    }
    
    
    /**
     * The overloaded constructor.
     * 
     * @param local   the local directory on the node.
     * @param shared  the shared directory on the node.
     */
    public WorkerNodeScratch( LocalDirectory local, SharedDirectory shared ){
        super( local, shared );
        mWorkerShared = null;
    }

    /**
     * Sets the directory shared amongst the worker nodes only.
     * 
     * @param directory   the worker node shared directory.
     */
    public void setWorkerSharedDirectory( WorkerSharedDirectory directory ){
        mWorkerShared = directory;        
    }
    
    /**
     * Returns the directory shared amongst the worker nodes only.
     * 
     * @return the worker shared directory.
     */
    public WorkerSharedDirectory getWorkerSharedDirectory(  ){
        return mWorkerShared;        
    }
    
     /**
     * Returns the clone of the object.
     *
     * @return the clone
     */
    public Object clone(){
        WorkerNodeScratch obj;
        
        obj = ( WorkerNodeScratch ) super.clone();
        
        if( this.getWorkerSharedDirectory() != null ){
            obj.setWorkerSharedDirectory( (WorkerSharedDirectory)this.getWorkerSharedDirectory().clone() );
        }
        
        return obj;
    }

    /**
     * Writes out the xml description of the object. 
     *
     * @param writer is a Writer opened and ready for writing. This can also
     *               be a StringWriter for efficient output.
     * @param indent the indent to be used.
     *
     * @exception IOException if something fishy happens to the stream.
     */
    public void toXML( Writer writer, String indent ) throws IOException {
        String newLine = System.getProperty( "line.separator", "\r\n" );
        String newIndent = indent + "\t";
        
        //write out the  xml element
        writer.write( indent );
        writer.write( "<scratch>" );        
        writer.write( newLine );
      
        this.getLocalDirectory().toXML( writer, newIndent );
        this.getSharedDirectory().toXML( writer, newIndent );
        if( this.getWorkerSharedDirectory() != null ){
            this.getWorkerSharedDirectory().toXML( writer, newIndent );
        }
        
        writer.write( indent );
        writer.write( "</scratch>" );
        writer.write( newLine );
    }

    
}
