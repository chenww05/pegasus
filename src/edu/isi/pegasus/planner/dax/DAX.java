/**
 *  Copyright 2007-2008 University Of Southern California
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
package edu.isi.pegasus.planner.dax;

/**
 * Creates a DAX job object
 * @author GAURANG MEHTA gmehta at isi dot edu
 * @see AbstractJob
 * @version $Revision$
 */
public class DAX extends AbstractJob {

    /**
     * Create a DAX job object
     * @param id The unique id of the DAX job object. Must be of type [A-Za-z][-A-Za-z0-9_]*
     * @param dagname The DAX file to plan and submit
     */
    public DAX(String id, String daxname) {
        this(id, daxname, null);
    }

    /**
     * Create a DAX job object
     * @param id The unique id of the DAX job object. Must be of type [A-Za-z][-A-Za-z0-9_]*
     * @param dagname The DAX file to plan and submit
     * @param label
     */
    public DAX(String id, String daxname, String label) {
        super();
        checkID(id);
        // to decide whether to exit. Currently just logging error and proceeding.
        mId = id;
        mName = daxname;
        mNodeLabel = label;
    }
}