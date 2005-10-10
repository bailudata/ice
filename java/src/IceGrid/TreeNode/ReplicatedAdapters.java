// **********************************************************************
//
// Copyright (c) 2003-2005 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************
package IceGrid.TreeNode;

import javax.swing.AbstractListModel;

import IceGrid.ReplicatedAdapterDescriptor;
import IceGrid.Model;
import IceGrid.Utils;

class ReplicatedAdapters extends EditableParent
{
    ReplicatedAdapters(java.util.List descriptors, Model model)
	throws UpdateFailedException
    {
	super(false, "Replicated Adapters", model);
	_descriptors = descriptors;

	java.util.Iterator p = _descriptors.iterator();
	while(p.hasNext())
	{
	    ReplicatedAdapterDescriptor descriptor 
		= (ReplicatedAdapterDescriptor)p.next();
	    
	    addChild(new ReplicatedAdapter(false, descriptor, _model));
	}
    }

    ReplicatedAdapters(ReplicatedAdapters o)
	throws UpdateFailedException
    {
	super(o);
	_descriptors = o._descriptors;
	java.util.Iterator p = o._children.iterator();
	while(p.hasNext())
	{
	    ReplicatedAdapter ra = (ReplicatedAdapter)p.next();
	    addChild(new ReplicatedAdapter(ra));
	}
    }

    java.util.LinkedList getUpdates()
    {
	java.util.LinkedList updates = new java.util.LinkedList();
	java.util.Iterator p = _children.iterator();
	while(p.hasNext())
	{
	    ReplicatedAdapter ra = (ReplicatedAdapter)p.next();
	    if(ra.isNew() || ra.isModified())
	    {
		updates.add(ra.getDescriptor());
	    }
	}
	return updates;
    }


    void update() throws UpdateFailedException
    {
	java.util.Set keepSet = new java.util.HashSet();

	java.util.Iterator p = _descriptors.iterator();
	while(p.hasNext())
	{
	    ReplicatedAdapterDescriptor descriptor 
		= (ReplicatedAdapterDescriptor)p.next();
	    keepSet.add(descriptor.id);

	    ReplicatedAdapter ra = (ReplicatedAdapter)findChild(descriptor.id);
	    assert ra != null;
	    ra.rebuild(descriptor);
	}
	purgeChildren(keepSet);
	fireStructureChangedEvent(this);
    }


    void update(java.util.List descriptors, String[] removeAdapters)
	throws UpdateFailedException
    {
	_descriptors = descriptors;

	//
	// One big set of removes
	//
	removeChildren(removeAdapters);

	//
	// One big set of updates, followed by inserts
	//
	java.util.Vector newChildren = new java.util.Vector();
	java.util.Vector updatedChildren = new java.util.Vector();
	
	java.util.Iterator p = descriptors.iterator();
	while(p.hasNext())
	{
	    ReplicatedAdapterDescriptor descriptor =
		(ReplicatedAdapterDescriptor)p.next();
	    
	    ReplicatedAdapter child 
		= (ReplicatedAdapter)findChild(descriptor.id);
	    if(child == null)
	    {
		newChildren.add(new ReplicatedAdapter(false, descriptor,
						      _model));
	    }
	    else
	    {
		child.rebuild(descriptor);
		updatedChildren.add(child);
	    }
	}
	
	updateChildren((CommonBaseI[])updatedChildren.toArray
		       (new CommonBaseI[0]));
	addChildren((CommonBaseI[])newChildren.toArray(new CommonBaseI[0]));
    }

    private java.util.List _descriptors;
}
