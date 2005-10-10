// **********************************************************************
//
// Copyright (c) 2003-2005 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************
package IceGrid.TreeNode;

import java.awt.Component;
import javax.swing.Icon;
import javax.swing.JMenuItem;
import javax.swing.JOptionPane;
import javax.swing.JPopupMenu;
import javax.swing.JTree;

import javax.swing.tree.TreeCellRenderer;
import javax.swing.tree.DefaultTreeCellRenderer;

import IceGrid.NodeDescriptor;
import IceGrid.Model;
import IceGrid.NodeDynamicInfo;
import IceGrid.NodeUpdateDescriptor;
import IceGrid.ServerDynamicInfo;
import IceGrid.AdapterDynamicInfo;
import IceGrid.ServerDescriptor;
import IceGrid.ServerInstanceDescriptor;
import IceGrid.ServerState;
import IceGrid.SimpleInternalFrame;
import IceGrid.TemplateDescriptor;
import IceGrid.Utils;

class Node extends EditableParent implements InstanceParent
{  
    static public NodeDescriptor
    copyDescriptor(NodeDescriptor nd)
    {
	NodeDescriptor copy = (NodeDescriptor)nd.clone();
	
	copy.serverInstances = new java.util.LinkedList();
	java.util.Iterator p = nd.serverInstances.iterator();
	while(p.hasNext())
	{
	    copy.serverInstances.add(Server.copyDescriptor(
					 (ServerInstanceDescriptor)p.next()));
	}
       
	copy.servers = new java.util.LinkedList();
	p = nd.servers.iterator();
	while(p.hasNext())
	{
	    copy.serverInstances.add(Server.copyDescriptor(
					 (ServerDescriptor)p.next()));
	}
	
	return copy;
    }

    public boolean[] getAvailableActions()
    {
	boolean[] actions = new boolean[ACTION_COUNT];

	actions[COPY] = true;
	actions[DELETE] = true;

	Object descriptor =  _model.getClipboard();
	if(descriptor != null)
	{
	    actions[PASTE] = descriptor instanceof NodeDescriptor ||
		descriptor instanceof ServerInstanceDescriptor ||
		descriptor instanceof ServerDescriptor;
	}
	actions[NEW_SERVER] = true;
	actions[NEW_SERVER_ICEBOX] = true;
	actions[NEW_SERVER_FROM_TEMPLATE] = true;
	return actions;
    }

    public JPopupMenu getPopupMenu()
    {
	if(_popup == null)
	{
	    _popup = new PopupMenu(_model);

	    JMenuItem newServerItem = new JMenuItem(_model.getActions()[NEW_SERVER]);
	    newServerItem.setText("New server");
	    _popup.add(newServerItem);

	    JMenuItem newIceBoxItem = new JMenuItem(_model.getActions()[NEW_SERVER_ICEBOX]);
	    newIceBoxItem.setText("New IceBox server");
	    _popup.add(newIceBoxItem);

	    JMenuItem newServerFromTemplateItem = 
		new JMenuItem(_model.getActions()[NEW_SERVER_FROM_TEMPLATE]);
	    newServerFromTemplateItem.setText("New server from template");
	    _popup.add(newServerFromTemplateItem); 
	}
	return _popup;
    }
    public void copy()
    {
	_model.setClipboard(copyDescriptor(_descriptor));
	_model.getActions()[PASTE].setEnabled(true);
    }
    public void paste()
    {
	Object descriptor =  _model.getClipboard();
	if(descriptor instanceof NodeDescriptor)
	{
	    _parent.paste();
	}
	else if(descriptor instanceof ServerInstanceDescriptor)
	{
	    newServer(Server.copyDescriptor((ServerInstanceDescriptor)descriptor));
	}
	else
	{
	    newServer(Server.copyDescriptor(((ServerDescriptor)descriptor)));
	}
    }
    public void newServer()
    {
	newServer(Server.newServerDescriptor());
    }
    public void newServerIceBox()
    {
	newServer(Server.newIceBoxDescriptor());
    }
    public void newServerFromTemplate()
    {
	ServerInstanceDescriptor descriptor = 
	    new ServerInstanceDescriptor("",
					 new java.util.HashMap());

	newServer(descriptor);
    }
    
     public Component getTreeCellRendererComponent(
	    JTree tree,
	    Object value,
	    boolean sel,
	    boolean expanded,
	    boolean leaf,
	    int row,
	    boolean hasFocus) 
    {
	if(_cellRenderer == null)
	{
	    //
	    // Initialization
	    //
	    _cellRenderer = new DefaultTreeCellRenderer();
	    _nodeUpOpen = Utils.getIcon("/icons/node_up_open.png");
	    _nodeDownOpen = Utils.getIcon("/icons/node_down_open.png");
	    _nodeUpClosed = Utils.getIcon("/icons/node_up_closed.png");
	    _nodeDownClosed = Utils.getIcon("/icons/node_down_closed.png");
	}

	//
	// TODO: separate icons for open and close
	//
	if(_up)
	{
	    _cellRenderer.setToolTipText("Up and running");
	    if(expanded)
	    {
		_cellRenderer.setOpenIcon(_nodeUpOpen);
	    }
	    else
	    {
		_cellRenderer.setClosedIcon(_nodeUpClosed);
	    }
	}
	else
	{
	    _cellRenderer.setToolTipText("Not running");
	    if(expanded)
	    {
		_cellRenderer.setOpenIcon(_nodeDownOpen);
	    }
	    else
	    {
		_cellRenderer.setClosedIcon(_nodeDownClosed);
	    }
	}

	return _cellRenderer.getTreeCellRendererComponent(
	    tree, value, sel, expanded, leaf, row, hasFocus);
    }
    
    public void displayProperties()
    {
	SimpleInternalFrame propertiesFrame = _model.getPropertiesFrame();
	
	propertiesFrame.setTitle("Properties for " + _id);
	if(_editor == null)
	{
	    _editor = new NodeEditor(_model.getMainFrame());
	}
	
	_editor.show(this);
	propertiesFrame.setContent(_editor.getComponent());

	propertiesFrame.validate();
	propertiesFrame.repaint();
    }

    public Object getDescriptor()
    {
	return _descriptor;
    }
    
    static private class Backup
    {
	java.util.TreeSet removedElements;
	java.util.Map parameterValues;
    }

    public Object rebuild(CommonBase child, java.util.List editables) 
	throws UpdateFailedException
    {
	Backup backup = new Backup();
	backup.removedElements = (java.util.TreeSet)_removedElements.clone();

	removeChild(child, true);
	Server server = (Server)child;
	ServerInstanceDescriptor instanceDescriptor = server.getInstanceDescriptor();
	
	if(instanceDescriptor != null)
	{
	    TemplateDescriptor templateDescriptor 
		= getApplication().findServerTemplateDescriptor(instanceDescriptor.template);

	    java.util.Set parameters = new java.util.HashSet(templateDescriptor.parameters);
	    if(!parameters.equals(instanceDescriptor.parameterValues.keySet()))
	    {
		backup.parameterValues = instanceDescriptor.parameterValues;
		instanceDescriptor.parameterValues = Editor.makeParameterValues(
		    instanceDescriptor.parameterValues, templateDescriptor.parameters);
	    }
      
	    try
	    {
		Server newServer = createServer(true, instanceDescriptor, getApplication());
		addChild(newServer, true);
	    }
	    catch(UpdateFailedException e)
	    {
		e.addParent(this);
		restore(child, backup);
		throw e;
	    }
	}
	else
	{
	    try
	    {
		Server newServer = createServer(true, server.getServerDescriptor(), getApplication());
		addChild(newServer, true);
	    }
	    catch(UpdateFailedException e)
	    {
		restore(child, backup);
		throw e;
	    }
	}
	return backup;
    }

    public void restore(CommonBase child, Object backupObject)
    {
	Backup backup = (Backup)backupObject;
	_removedElements = backup.removedElements;

	Server goodServer = (Server)child;
	ServerInstanceDescriptor instanceDescriptor = goodServer.getInstanceDescriptor();
	if(instanceDescriptor != null &&  backup.parameterValues != null)
	{
	    instanceDescriptor.parameterValues = backup.parameterValues;
	}

	CommonBase badServer = findChildWithDescriptor(goodServer.getDescriptor());
	if(badServer != null)
	{
	    removeChild(badServer, true);
	}

	try
	{
	    addChild(child, true);
	}
	catch(UpdateFailedException e)
	{
	    assert false; // impossible
	}
    }

    private Server createServer(boolean brandNew, ServerInstanceDescriptor instanceDescriptor,
				Application application) throws UpdateFailedException
    {
	//
	// Find template
	//
	TemplateDescriptor templateDescriptor = 
	    application.findServerTemplateDescriptor(instanceDescriptor.template);

	assert templateDescriptor != null;
	    
	ServerDescriptor serverDescriptor = 
	    (ServerDescriptor)templateDescriptor.descriptor;
	
	assert serverDescriptor != null;
	
	//
	// Build resolver
	//
	Utils.Resolver instanceResolver = 
	    new Utils.Resolver(_resolver, 
			       instanceDescriptor.parameterValues,
			       templateDescriptor.parameterDefaults);
	
	String serverId = instanceResolver.substitute(serverDescriptor.id);
	instanceResolver.put("server", serverId);
	
	//
	// Create server
	//
	return new Server(brandNew, serverId, instanceResolver, instanceDescriptor, 
			  serverDescriptor, application);
    }

    private Server createServer(boolean brandNew, ServerDescriptor serverDescriptor,
				Application application) throws UpdateFailedException
    {
	//
	// Build resolver
	//
	Utils.Resolver instanceResolver = new Utils.Resolver(_resolver);
	String serverId = instanceResolver.substitute(serverDescriptor.id);
	instanceResolver.put("server", serverId);
	
	//
	// Create server
	//
	return new Server(brandNew, serverId, instanceResolver, null, serverDescriptor, 
			  application);

    }

    void up()
    {
	_up = true;
	fireNodeChangedEvent(this);
    }

    boolean down()
    {
	_up = false;

	if(_descriptor == null)
	{
	    return true;
	}
	else
	{
	    fireNodeChangedEvent(this);
	    return false;
	}
    }

    NodeUpdateDescriptor getUpdate()
    {
	NodeUpdateDescriptor update = new NodeUpdateDescriptor();
	update.name = _id;

	//
	// First: servers
	//
	if(isNew())
	{
	    update.removeServers = new String[0];
	}
	else
	{
	    update.removeServers = removedElements();
	    for(int i = 0; i < update.removeServers.length; ++i)
	    {
		System.err.println(update.removeServers[i]);
	    }
	}

	update.serverInstances = new java.util.LinkedList();
	update.servers = new java.util.LinkedList();

	java.util.Iterator p = _children.iterator();
	while(p.hasNext())
	{
	    Server server = (Server)p.next();
	    if(isNew() || server.isModified() || server.isNew())
	    {
		ServerInstanceDescriptor instanceDescriptor = server.getInstanceDescriptor();
		if(instanceDescriptor != null)
		{
		    update.serverInstances.add(instanceDescriptor);
		}
		else
		{
		    update.servers.add(server.getDescriptor());
		}
	    }
	}
	
	//
	// Anything in this update?
	//
	if(!isNew() && !isModified() && update.removeServers.length == 0
	   && update.servers.size() == 0
	   && update.serverInstances.size() == 0)
	{
	    return null;
	}

	if(isNew())
	{
	    update.variables = _descriptor.variables;
	    update.removeVariables = new String[0];
	}
	else
	{
	    //
	    // Diff variables (TODO: avoid duplication with same code in Application)
	    //
	    update.variables = (java.util.TreeMap)_descriptor.variables.clone();
	    java.util.List removeVariables = new java.util.LinkedList();

	    p = _origVariables.entrySet().iterator();
	    while(p.hasNext())
	    {
		java.util.Map.Entry entry = (java.util.Map.Entry)p.next();
		Object key = entry.getKey();
		Object newValue =  update.variables.get(key);
		if(newValue == null)
		{
		    removeVariables.add(key);
		}
		else
		{
		    Object value = entry.getValue();
		    if(newValue.equals(value))
		    {
			update.variables.remove(key);
		    }
		}
	    }
	    update.removeVariables = (String[])removeVariables.toArray(new String[0]);
	}
	       

	return update;
    }


    NodeDescriptor update(NodeUpdateDescriptor update, Application application)
	throws UpdateFailedException
    {
	if(_descriptor == null)
	{
	    NodeDescriptor descriptor = new NodeDescriptor(update.variables,
							   update.serverInstances,
							   update.servers,
							   update.loadFactor.value);

	    init(descriptor, application, true);
	    return _descriptor;
	}

	//
	// Otherwise it's a real update
	//

	//
	// Variables
	//
	for(int i = 0; i < update.removeVariables.length; ++i)
	{
	    _descriptor.variables.remove(update.removeVariables[i]);
	}
	_descriptor.variables.putAll(update.variables);

	//
	// One big set of removes
	//
	removeChildren(update.removeServers);

	//
	// Update _descriptor
	//
	for(int i = 0; i < update.removeServers.length; ++i)
	{
	    _descriptor.serverInstances.remove(update.removeServers[i]);
	    _descriptor.servers.remove(update.removeServers[i]);
	} 

	//
	// One big set of updates, followed by inserts
	//
	java.util.Vector newChildren = new java.util.Vector();
	java.util.Vector updatedChildren = new java.util.Vector();
	
	java.util.Iterator p = update.serverInstances.iterator();
	while(p.hasNext())
	{
	    ServerInstanceDescriptor instanceDescriptor = 
		(ServerInstanceDescriptor)p.next();
	    
	    //
	    // Find template
	    //
	    TemplateDescriptor templateDescriptor = 
		application.findServerTemplateDescriptor(instanceDescriptor.template);

	    assert templateDescriptor != null;
	    
	    ServerDescriptor serverDescriptor = 
		(ServerDescriptor)templateDescriptor.descriptor;
	    
	    assert serverDescriptor != null;
	    
	    //
	    // Build resolver
	    //
	    Utils.Resolver instanceResolver = 
		new Utils.Resolver(_resolver, 
				   instanceDescriptor.parameterValues,
				   templateDescriptor.parameterDefaults);
	    
	    String serverId = instanceResolver.substitute(serverDescriptor.id);
	    instanceResolver.put("server", serverId);
	    
	    //
	    // Lookup server
	    //
	    Server server = (Server)findChild(serverId);
	    if(server != null)
	    {
		server.rebuild(instanceResolver, instanceDescriptor, serverDescriptor,
			       application);
		updatedChildren.add(server);
	    }
	    else
	    {
		server = new Server(false, serverId, instanceResolver, instanceDescriptor, 
				    serverDescriptor, application);
		newChildren.add(server);
		_descriptor.serverInstances.add(instanceDescriptor);
	    }
	    
	}
	
	//
	// Plain servers
	//
	p = update.servers.iterator();
	while(p.hasNext())
	{
	    ServerDescriptor serverDescriptor = (ServerDescriptor)p.next();

	    //
	    // Build resolver
	    //
	    Utils.Resolver instanceResolver = new Utils.Resolver(_resolver);
	    String serverId = instanceResolver.substitute(serverDescriptor.id);
	    instanceResolver.put("server", serverId);
	    
	    //
	    // Lookup server
	    //
	    Server server = (Server)findChild(serverId);
	    
	    if(server != null)
	    {
		server.rebuild(instanceResolver, null, serverDescriptor,
			       application);
		updatedChildren.add(server);
	    }
	    else
	    {
		server = new Server(false, serverId, instanceResolver, null, 
				    serverDescriptor, application);
		newChildren.add(server);
		_descriptor.servers.add(serverDescriptor);
	    }
	}
	
	updateChildren((CommonBaseI[])updatedChildren.toArray(new CommonBaseI[0]));
	addChildren((CommonBaseI[])newChildren.toArray(new CommonBaseI[0]));

	return null;
    }
   
    public void commit()
    {
	super.commit();
	if(_descriptor != null)
	{
	    _origVariables = (java.util.Map)_descriptor.variables.clone();
	}
    }

    Node(String nodeName, Model model)
    {
	super(false, nodeName, model);  
	_up = true;
    }

    Node(boolean brandNew, String nodeName, NodeDescriptor descriptor, Application application)
	throws UpdateFailedException
    {
	super(brandNew, nodeName, application.getModel());
	init(descriptor, application, false);
    } 
    
    Node(Node o)
    {
	super(o);
	_descriptor = o._descriptor;
	_resolver = o._resolver;
	_origVariables = o._origVariables;
	_up = o._up;

	//
	// Deep-copy children
	//
	java.util.Iterator p = o._children.iterator();
	while(p.hasNext())
	{
	    Server server = (Server)p.next();
	    try
	    {
		addChild(new Server(server));
	    }
	    catch(UpdateFailedException e)
	    {
		assert false; // impossible
	    }
	}
    }

    void init(NodeDescriptor descriptor, Application application, boolean fireEvent)
	throws UpdateFailedException
    {
	assert _descriptor == null;
	_descriptor = descriptor;
	_origVariables = (java.util.Map)_descriptor.variables.clone();
	

	_resolver = new Utils.Resolver(new java.util.Map[]
	    {_descriptor.variables, application.getVariables()});
				       
	_resolver.put("application", application.getId());
	_resolver.put("node", getId());
	
	//
	// Template instances
	//
	java.util.Iterator p = _descriptor.serverInstances.iterator();
	while(p.hasNext())
	{
	    ServerInstanceDescriptor instanceDescriptor = 
		(ServerInstanceDescriptor)p.next();
	   
	    addChild(createServer(false, instanceDescriptor, application));
	}

	//
	// Plain servers
	//
	p = _descriptor.servers.iterator();
	while(p.hasNext())
	{
	    ServerDescriptor serverDescriptor = (ServerDescriptor)p.next();
	    addChild(createServer(false, serverDescriptor, application));
	}

	if(fireEvent)
	{
	    fireStructureChangedEvent(this);
	}
    }
    
    
    void update() throws UpdateFailedException
    {
	if(_descriptor == null)
	{
	    //
	    // Nothing to do
	    //
	    return;
	}

	Application application = getApplication();

	_resolver = new Utils.Resolver(new java.util.Map[]
	    {_descriptor.variables, application.getVariables()});
				       
	_resolver.put("application", application.getId());
	_resolver.put("node", getId());
	
	//
	// Existing servers not in this list will be removed
	//
	java.util.Set serverIdSet = new java.util.HashSet();

	//
	// Template instances
	//
	java.util.Iterator p = _descriptor.serverInstances.iterator();
	while(p.hasNext())
	{
	    ServerInstanceDescriptor instanceDescriptor = 
		(ServerInstanceDescriptor)p.next();
	    
	    //
	    // Find template
	    //
	    TemplateDescriptor templateDescriptor = 
		application.findServerTemplateDescriptor(instanceDescriptor.template);

	    assert templateDescriptor != null;

	    ServerDescriptor serverDescriptor = 
		(ServerDescriptor)templateDescriptor.descriptor;
	    
	    assert serverDescriptor != null;
	    
	    //
	    // Build resolver
	    //
	    Utils.Resolver instanceResolver = 
		new Utils.Resolver(_resolver, 
				   instanceDescriptor.parameterValues,
				   templateDescriptor.parameterDefaults);
	    
	    String serverId = instanceResolver.substitute(serverDescriptor.id);
	    instanceResolver.put("server", serverId);
	    serverIdSet.add(serverId);
	    
	    //
	    // Lookup server
	    //
	    Server server = (Server)findChild(serverId);
	    if(server != null)
	    {
		server.rebuild(instanceResolver, instanceDescriptor, 
			       serverDescriptor, application);
	    }
	    else
	    {
		//
		// Create server
		//
		server = new Server(true, serverId, instanceResolver, instanceDescriptor, 
				    serverDescriptor, application);
		addChild(server, true);
	    }
	}

	//
	// Plain servers
	//
	p = _descriptor.servers.iterator();
	while(p.hasNext())
	{
	    ServerDescriptor serverDescriptor = (ServerDescriptor)p.next();

	    //
	    // Build resolver
	    //
	    Utils.Resolver instanceResolver = new Utils.Resolver(_resolver);
	    String serverId = instanceResolver.substitute(serverDescriptor.id);
	    instanceResolver.put("server", serverId);
	    serverIdSet.add(serverId);

	    //
	    // Lookup server
	    //
	    Server server = (Server)findChild(serverId);
	    if(server != null)
	    {
		server.rebuild(instanceResolver, null, serverDescriptor, application);
	    }
	    else
	    {
		//
		// Create server
		//
		server = new Server(true, serverId, instanceResolver, null, serverDescriptor, 
				    application);
		addChild(server);
	    }
	}

	purgeChildren(serverIdSet);
    }


    void removeInstanceDescriptor(ServerInstanceDescriptor d)
    {
	//
	// A straight remove uses equals(), which is not the desired behavior
	//
	java.util.Iterator p = _descriptor.serverInstances.iterator();
	while(p.hasNext())
	{
	    if(d == p.next())
	    {
		p.remove();
		break;
	    }
	}
    }

    java.util.List findServerInstances(String template)
    {
	java.util.List result = new java.util.LinkedList();
	java.util.Iterator p = _children.iterator();
	while(p.hasNext())
	{
	    Server server = (Server)p.next();
	    ServerInstanceDescriptor instanceDescriptor
		= server.getInstanceDescriptor();

	    if(instanceDescriptor != null && 
	       instanceDescriptor.template.equals(template))
	    {
		result.add(server);
	    }
	}
	return result;
    }


    void removeServerInstances(String template)
    {
	java.util.List toRemove = new java.util.LinkedList();

	java.util.Iterator p = _children.iterator();
	while(p.hasNext())
	{
	    Server server = (Server)p.next();
	    ServerInstanceDescriptor instanceDescriptor
		= server.getInstanceDescriptor();

	    if(instanceDescriptor != null && 
	       instanceDescriptor.template.equals(template))
	    {
		//
		// Remove instance
		//
		removeInstanceDescriptor(instanceDescriptor);
		_removedElements.add(server.getId());
		toRemove.add(server.getId());
	    }
	}

	if(toRemove.size() > 0)
	{
	    removeChildren((String[])toRemove.toArray(new String[0]));
	}
    }

    java.util.List findServiceInstances(String template)
    {
	java.util.List result = new java.util.LinkedList();
	java.util.Iterator p = _children.iterator();
	while(p.hasNext())
	{
	    Server server = (Server)p.next();
	    result.addAll(server.findServiceInstances(template));
	}
	return result;
    }


    void removeServiceInstances(String template)
    {	
	java.util.Iterator p = _children.iterator();
	while(p.hasNext())
	{
	    Server server = (Server)p.next();
	    server.removeServiceInstances(template);
	}
    }

    Utils.Resolver getResolver()
    {
	return _resolver;
    }


    void tryAdd(ServerInstanceDescriptor instanceDescriptor,
		ServerDescriptor serverDescriptor) throws UpdateFailedException
    {
	try
	{
	    if(instanceDescriptor != null)
	    {
		_descriptor.serverInstances.add(instanceDescriptor);
		addChild(createServer(true, instanceDescriptor, getApplication()),
			 true);
	    }
	    else
	    {
		_descriptor.servers.add(serverDescriptor);
		addChild(createServer(true, serverDescriptor, getApplication()),
			 true);
	    }
	}
	catch(UpdateFailedException e)
	{
	    e.addParent(this);
	    if(instanceDescriptor != null)
	    {
		removeDescriptor(instanceDescriptor);
	    }
	    else
	    {
		removeDescriptor(serverDescriptor);
	    }
	    throw e;
	}
    }


    void addDescriptor(ServerDescriptor sd)
    {
	_descriptor.servers.add(sd);
    }

    void removeDescriptor(ServerDescriptor sd)
    {
	//
	// A straight remove uses equals(), which is not the desired behavior
	//
	java.util.Iterator p = _descriptor.servers.iterator();
	while(p.hasNext())
	{
	    if(sd == p.next())
	    {
		p.remove();
		break;
	    }
	}
    }
    void addDescriptor(ServerInstanceDescriptor sd)
    {
	_descriptor.serverInstances.add(sd);
    }
    void removeDescriptor(ServerInstanceDescriptor sd)
    {
	//
	// A straight remove uses equals(), which is not the desired behavior
	//
	java.util.Iterator p = _descriptor.serverInstances.iterator();
	while(p.hasNext())
	{
	    if(sd == p.next())
	    {
		p.remove();
		break;
	    }
	}
    }

    private void newServer(ServerDescriptor descriptor)
    {
	descriptor.id = makeNewChildId(descriptor.id);
	
	Server server = new Server(descriptor.id, null, descriptor, _model);
	try
	{
	    addChild(server, true);
	}
	catch(UpdateFailedException e)
	{
	    assert false;
	}
	_model.setSelectionPath(server.getPath());
    }

    private void newServer(ServerInstanceDescriptor descriptor)
    {
	String id = makeNewChildId("NewServer");
	
	//
	// Make sure descriptor.template points to a real template
	//
	ServerTemplate t = getApplication().findServerTemplate(descriptor.template);
	
	if(t == null)
	{
	    t = (ServerTemplate)getApplication().getServerTemplates().getChildAt(0);
	    
	    if(t == null)
	    {
		JOptionPane.showMessageDialog(
		    _model.getMainFrame(),
		    "You need to create a server template before you can create a server from a template.",
		    "No Server Template",
		    JOptionPane.INFORMATION_MESSAGE);
		return;
	    }
	    else
	    {
		descriptor.template = t.getId();
		descriptor.parameterValues = new java.util.HashMap();
	    }
	}
	
	ServerDescriptor sd = (ServerDescriptor)
	    ((TemplateDescriptor)t.getDescriptor()).descriptor;

	Server server = new Server(id, descriptor, sd, _model);
	try
	{
	    addChild(server, true);
	}
	catch(UpdateFailedException e)
	{
	    assert false;
	}
	_model.setSelectionPath(server.getPath());
    }

    private NodeDescriptor _descriptor;
    private Utils.Resolver _resolver;

    private java.util.Map _origVariables;

    private boolean _up = false;

    static private DefaultTreeCellRenderer _cellRenderer;
    static private Icon _nodeUpOpen;
    static private Icon _nodeUpClosed;
    static private Icon _nodeDownOpen;
    static private Icon _nodeDownClosed;

    static private NodeEditor _editor;
    static private JPopupMenu _popup;
}
