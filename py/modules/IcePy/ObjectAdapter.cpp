// **********************************************************************
//
// Copyright (c) 2003-2004 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <ObjectAdapter.h>
#include <Communicator.h>
#include <Current.h>
#include <Operation.h>
#include <Proxy.h>
#include <Util.h>
#include <Ice/Communicator.h>
#include <Ice/LocalException.h>
#include <Ice/Locator.h>
#include <Ice/ObjectAdapter.h>
#include <Ice/Router.h>
#include <Ice/ServantLocator.h>

using namespace std;
using namespace IcePy;

namespace IcePy
{

struct ObjectAdapterObject
{
    PyObject_HEAD
    Ice::ObjectAdapterPtr* adapter;
};

//
// Encapsulates a Python servant.
//
class ServantWrapper : public Ice::BlobjectAsync
{
public:

    ServantWrapper(PyObject*);
    ~ServantWrapper();

    virtual void ice_invoke_async(const Ice::AMD_Object_ice_invokePtr&, const vector<Ice::Byte>&, const Ice::Current&);

    PyObject* getObject();

private:

    PyObject* _servant;
    string _id;
    typedef map<string, OperationPtr> OperationMap;
    OperationMap _operationMap;
    OperationMap::iterator _lastOp;
};
typedef IceUtil::Handle<ServantWrapper> ServantWrapperPtr;

class ServantLocatorWrapper : public Ice::ServantLocator
{
public:

    ServantLocatorWrapper(PyObject*);
    ~ServantLocatorWrapper();

    virtual Ice::ObjectPtr locate(const Ice::Current&, Ice::LocalObjectPtr&);

    virtual void finished(const Ice::Current&, const Ice::ObjectPtr&, const Ice::LocalObjectPtr&);

    virtual void deactivate(const string&);

    PyObject* getObject();

private:

    //
    // This object is created in locate() and destroyed after finished(). Ice guarantees
    // that these two functions are called in the same thread, therefore the AdoptThread
    // member does "the right thing".
    //
    struct Cookie : public Ice::LocalObject
    {
        Cookie();
        ~Cookie();

        AdoptThread adoptThread;
        PyObject* current;
        ServantWrapperPtr servant;
        PyObject* cookie;
    };
    typedef IceUtil::Handle<Cookie> CookiePtr;

    PyObject* _locator;
    PyObject* _objectType;
};
typedef IceUtil::Handle<ServantLocatorWrapper> ServantLocatorWrapperPtr;

}

IcePy::ServantWrapper::ServantWrapper(PyObject* servant) :
    _servant(servant), _lastOp(_operationMap.end())
{
    Py_INCREF(_servant);
    PyObjectHandle id = PyObject_CallMethod(servant, "ice_id", NULL);
    if(id.get() != NULL)
    {
        if(!PyString_Check(id.get()))
        {
            PyErr_SetString(PyExc_RuntimeError, "ice_id must return a string");
        }
        else
        {
            _id = PyString_AS_STRING(id.get());
        }
    }
}

IcePy::ServantWrapper::~ServantWrapper()
{
    Py_DECREF(_servant);
}

void
IcePy::ServantWrapper::ice_invoke_async(const Ice::AMD_Object_ice_invokePtr& cb, const vector<Ice::Byte>& inParams,
                                        const Ice::Current& current)
{
    AdoptThread adoptThread; // Ensure the current thread is able to call into Python.

    try
    {
        //
        // Locate the Operation object. As an optimization we keep a reference
        // to the most recent operation we've dispatched, so check that first.
        //
        OperationPtr op;
        if(_lastOp != _operationMap.end() && _lastOp->first == current.operation)
        {
            op = _lastOp->second;
        }
        else
        {
            //
            // Next check our cache of operations.
            //
            _lastOp = _operationMap.find(current.operation);
            if(_lastOp == _operationMap.end())
            {
                //
                // Look for the Operation object in the servant's type.
                //
                string attrName = "_op_" + current.operation;
                PyObjectHandle h = PyObject_GetAttrString((PyObject*)_servant->ob_type,
                                                          const_cast<char*>(attrName.c_str()));
                if(h.get() == NULL)
                {
                    Ice::OperationNotExistException ex(__FILE__, __LINE__);
                    ex.id = current.id;
                    ex.facet = current.facet;
                    ex.operation = current.operation;
                    throw ex;
                }

                op = getOperation(h.get());
                _lastOp = _operationMap.insert(OperationMap::value_type(current.operation, op)).first;
            }
            else
            {
                op = _lastOp->second;
            }
        }

        op->dispatch(_servant, cb, inParams, current);
    }
    catch(const Ice::Exception& ex)
    {
        cb->ice_exception(ex);
    }
}

PyObject*
IcePy::ServantWrapper::getObject()
{
    Py_INCREF(_servant);
    return _servant;
}

IcePy::ServantLocatorWrapper::ServantLocatorWrapper(PyObject* locator) :
    _locator(locator)
{
    Py_INCREF(_locator);
    _objectType = lookupType("Ice.Object");
}

IcePy::ServantLocatorWrapper::~ServantLocatorWrapper()
{
}

Ice::ObjectPtr
IcePy::ServantLocatorWrapper::locate(const Ice::Current& current, Ice::LocalObjectPtr& cookie)
{
    CookiePtr c = new Cookie; // The Cookie constructor adopts this thread.
    c->current = createCurrent(current);
    if(c->current == NULL)
    {
        throwPythonException();
    }

    //
    // Invoke locate on the Python object. We expect the object to return either
    // the servant by itself, or the servant in a tuple with an optional cookie
    // object.
    //
    PyObjectHandle res = PyObject_CallMethod(_locator, "locate", "O", c->current);
    if(PyErr_Occurred())
    {
        throwPythonException();
    }

    if(res.get() == Py_None)
    {
        return 0;
    }

    PyObject* servantObj = NULL;
    PyObject* cookieObj = Py_None;
    if(PyTuple_Check(res.get()))
    {
        if(PyTuple_GET_SIZE(res.get()) > 2)
        {
            PyErr_Warn(PyExc_RuntimeWarning, "invalid return value for ServantLocator::locate");
            return 0;
        }
        servantObj = PyTuple_GET_ITEM(res.get(), 0);
        if(PyTuple_GET_SIZE(res.get()) > 1)
        {
            cookieObj = PyTuple_GET_ITEM(res.get(), 1);
        }
    }
    else
    {
        servantObj = res.get();
    }

    //
    // Verify that the servant is an Ice object.
    //
    if(!PyObject_IsInstance(servantObj, _objectType))
    {
        PyErr_Warn(PyExc_RuntimeWarning, "return value of ServantLocator::locate is not an Ice object");
        return 0;
    }

    //
    // Save state in our cookie and return a wrapper for the servant.
    //
    c->servant = new ServantWrapper(servantObj);
    c->cookie = cookieObj;
    Py_INCREF(c->cookie);
    cookie = c;
    return c->servant;
}

void
IcePy::ServantLocatorWrapper::finished(const Ice::Current&, const Ice::ObjectPtr&, const Ice::LocalObjectPtr& cookie)
{
    CookiePtr c = CookiePtr::dynamicCast(cookie);
    assert(c);

    ServantWrapperPtr wrapper = ServantWrapperPtr::dynamicCast(c->servant);
    PyObjectHandle servantObj = wrapper->getObject();

    PyObjectHandle res = PyObject_CallMethod(_locator, "finished", "OOO", c->current, servantObj.get(), c->cookie);
    if(PyErr_Occurred())
    {
        throwPythonException();
    }
}

void
IcePy::ServantLocatorWrapper::deactivate(const string& category)
{
    AdoptThread adoptThread; // Ensure the current thread is able to call into Python.

    PyObjectHandle res = PyObject_CallMethod(_locator, "deactivate", "s", category.c_str());
    if(PyErr_Occurred())
    {
        throwPythonException();
    }

    Py_DECREF(_locator);
}

PyObject*
IcePy::ServantLocatorWrapper::getObject()
{
    Py_INCREF(_locator);
    return _locator;
}

IcePy::ServantLocatorWrapper::Cookie::Cookie()
{
    current = NULL;
    cookie = NULL;
}

IcePy::ServantLocatorWrapper::Cookie::~Cookie()
{
    Py_XDECREF(current);
    Py_XDECREF(cookie);
}

#ifdef WIN32
extern "C"
#endif
static ObjectAdapterObject*
adapterNew(PyObject* /*arg*/)
{
    PyErr_Format(PyExc_RuntimeError, "Use communicator.createObjectAdapter to create an adapter");
    return NULL;
}

#ifdef WIN32
extern "C"
#endif
static void
adapterDealloc(ObjectAdapterObject* self)
{
    delete self->adapter;
    PyObject_Del(self);
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterGetName(ObjectAdapterObject* self)
{
    assert(self->adapter);
    string name;
    try
    {
        name = (*self->adapter)->getName();
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return PyString_FromString(const_cast<char*>(name.c_str()));
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterGetCommunicator(ObjectAdapterObject* self)
{
    assert(self->adapter);
    Ice::CommunicatorPtr communicator;
    try
    {
        communicator = (*self->adapter)->getCommunicator();
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return createCommunicator(communicator);
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterActivate(ObjectAdapterObject* self)
{
    assert(self->adapter);
    try
    {
        AllowThreads allowThreads; // Release Python's global interpreter lock during blocking calls.
        (*self->adapter)->activate();
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterHold(ObjectAdapterObject* self)
{
    assert(self->adapter);
    try
    {
        (*self->adapter)->hold();
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterWaitForHold(ObjectAdapterObject* self)
{
    assert(self->adapter);
    try
    {
        AllowThreads allowThreads; // Release Python's global interpreter lock during blocking calls.
        (*self->adapter)->waitForHold();
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterDeactivate(ObjectAdapterObject* self)
{
    assert(self->adapter);
    try
    {
        AllowThreads allowThreads; // Release Python's global interpreter lock during blocking calls.
        (*self->adapter)->deactivate();
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterWaitForDeactivate(ObjectAdapterObject* self)
{
    assert(self->adapter);
    try
    {
        AllowThreads allowThreads; // Release Python's global interpreter lock during blocking calls.
        (*self->adapter)->waitForDeactivate();
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterAdd(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* objectType = lookupType("Ice.Object");
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* servant;
    PyObject* id;
    if(!PyArg_ParseTuple(args, "O!O!", objectType, &servant, identityType, &id))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    ServantWrapperPtr wrapper = new ServantWrapper(servant);
    if(PyErr_Occurred())
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPrx proxy;
    try
    {
        proxy = (*self->adapter)->add(wrapper, ident);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return createProxy(proxy, (*self->adapter)->getCommunicator());
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterAddFacet(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* objectType = lookupType("Ice.Object");
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* servant;
    PyObject* id;
    char* facet;
    if(!PyArg_ParseTuple(args, "O!O!s", objectType, &servant, identityType, &id, &facet))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    ServantWrapperPtr wrapper = new ServantWrapper(servant);
    if(PyErr_Occurred())
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPrx proxy;
    try
    {
        proxy = (*self->adapter)->addFacet(wrapper, ident, facet);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return createProxy(proxy, (*self->adapter)->getCommunicator());
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterAddWithUUID(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* objectType = lookupType("Ice.Object");
    PyObject* servant;
    if(!PyArg_ParseTuple(args, "O!", objectType, &servant))
    {
        return NULL;
    }

    ServantWrapperPtr wrapper = new ServantWrapper(servant);
    if(PyErr_Occurred())
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPrx proxy;
    try
    {
        proxy = (*self->adapter)->addWithUUID(wrapper);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return createProxy(proxy, (*self->adapter)->getCommunicator());
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterAddFacetWithUUID(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* objectType = lookupType("Ice.Object");
    PyObject* servant;
    char* facet;
    if(!PyArg_ParseTuple(args, "O!s", objectType, &servant, &facet))
    {
        return NULL;
    }

    ServantWrapperPtr wrapper = new ServantWrapper(servant);
    if(PyErr_Occurred())
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPrx proxy;
    try
    {
        proxy = (*self->adapter)->addFacetWithUUID(wrapper, facet);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return createProxy(proxy, (*self->adapter)->getCommunicator());
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterRemove(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    if(!PyArg_ParseTuple(args, "O!", identityType, &id))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPtr obj;
    try
    {
        obj = (*self->adapter)->remove(ident);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    ServantWrapperPtr wrapper = ServantWrapperPtr::dynamicCast(obj);
    assert(wrapper);
    return wrapper->getObject();
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterRemoveFacet(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    char* facet;
    if(!PyArg_ParseTuple(args, "O!s", identityType, &id, &facet))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPtr obj;
    try
    {
        obj = (*self->adapter)->removeFacet(ident, facet);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    ServantWrapperPtr wrapper = ServantWrapperPtr::dynamicCast(obj);
    assert(wrapper);
    return wrapper->getObject();
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterRemoveAllFacets(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    if(!PyArg_ParseTuple(args, "O!", identityType, &id))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::FacetMap facetMap;
    try
    {
        facetMap = (*self->adapter)->removeAllFacets(ident);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    PyObjectHandle result = PyDict_New();
    if(result.get() == NULL)
    {
        return NULL;
    }

    for(Ice::FacetMap::iterator p = facetMap.begin(); p != facetMap.end(); ++p)
    {
        ServantWrapperPtr wrapper = ServantWrapperPtr::dynamicCast(p->second);
        assert(wrapper);
        PyObjectHandle obj = wrapper->getObject();
        if(PyDict_SetItemString(result.get(), const_cast<char*>(p->first.c_str()), obj.get()) < 0)
        {
            return NULL;
        }
    }

    return result.release();
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterFind(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    if(!PyArg_ParseTuple(args, "O!", identityType, &id))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPtr obj;
    try
    {
        obj = (*self->adapter)->find(ident);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    ServantWrapperPtr wrapper = ServantWrapperPtr::dynamicCast(obj);
    assert(wrapper);
    return wrapper->getObject();
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterFindFacet(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    char* facet;
    if(!PyArg_ParseTuple(args, "O!s", identityType, &id, &facet))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPtr obj;
    try
    {
        obj = (*self->adapter)->findFacet(ident, facet);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    ServantWrapperPtr wrapper = ServantWrapperPtr::dynamicCast(obj);
    assert(wrapper);
    return wrapper->getObject();
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterFindAllFacets(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    if(!PyArg_ParseTuple(args, "O!", identityType, &id))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::FacetMap facetMap;
    try
    {
        facetMap = (*self->adapter)->findAllFacets(ident);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    PyObjectHandle result = PyDict_New();
    if(result.get() == NULL)
    {
        return NULL;
    }

    for(Ice::FacetMap::iterator p = facetMap.begin(); p != facetMap.end(); ++p)
    {
        ServantWrapperPtr wrapper = ServantWrapperPtr::dynamicCast(p->second);
        assert(wrapper);
        PyObjectHandle obj = wrapper->getObject();
        if(PyDict_SetItemString(result.get(), const_cast<char*>(p->first.c_str()), obj.get()) < 0)
        {
            return NULL;
        }
    }

    return result.release();
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterFindByProxy(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* proxyType = lookupType("Ice.ObjectPrx");
    PyObject* proxy;
    if(!PyArg_ParseTuple(args, "O!", proxyType, &proxy))
    {
        return NULL;
    }

    Ice::ObjectPrx prx = getProxy(proxy);

    assert(self->adapter);
    Ice::ObjectPtr obj;
    try
    {
        obj = (*self->adapter)->findByProxy(prx);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    ServantWrapperPtr wrapper = ServantWrapperPtr::dynamicCast(obj);
    assert(wrapper);
    return wrapper->getObject();
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterAddServantLocator(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* locatorType = lookupType("Ice.ServantLocator");
    PyObject* locator;
    char* category;
    if(!PyArg_ParseTuple(args, "O!s", locatorType, &locator, &category))
    {
        return NULL;
    }

    ServantLocatorWrapperPtr wrapper = new ServantLocatorWrapper(locator);

    assert(self->adapter);
    try
    {
        (*self->adapter)->addServantLocator(wrapper, category);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterFindServantLocator(ObjectAdapterObject* self, PyObject* args)
{
    char* category;
    if(!PyArg_ParseTuple(args, "s", &category))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ServantLocatorPtr locator;
    try
    {
        locator = (*self->adapter)->findServantLocator(category);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    ServantLocatorWrapperPtr wrapper = ServantLocatorWrapperPtr::dynamicCast(locator);
    assert(wrapper);
    return wrapper->getObject();
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterCreateProxy(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    if(!PyArg_ParseTuple(args, "O!", identityType, &id))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPrx proxy;
    try
    {
        proxy = (*self->adapter)->createProxy(ident);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return createProxy(proxy, (*self->adapter)->getCommunicator());
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterCreateDirectProxy(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    if(!PyArg_ParseTuple(args, "O!", identityType, &id))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPrx proxy;
    try
    {
        proxy = (*self->adapter)->createDirectProxy(ident);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return createProxy(proxy, (*self->adapter)->getCommunicator());
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterCreateReverseProxy(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* identityType = lookupType("Ice.Identity");
    PyObject* id;
    if(!PyArg_ParseTuple(args, "O!", identityType, &id))
    {
        return NULL;
    }

    Ice::Identity ident;
    if(!getIdentity(id, ident))
    {
        return NULL;
    }

    assert(self->adapter);
    Ice::ObjectPrx proxy;
    try
    {
        proxy = (*self->adapter)->createReverseProxy(ident);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    return createProxy(proxy, (*self->adapter)->getCommunicator());
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterAddRouter(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* proxyType = lookupType("Ice.RouterPrx");
    PyObject* proxy;
    if(!PyArg_ParseTuple(args, "O!", proxyType, &proxy))
    {
        return NULL;
    }

    Ice::RouterPrx router = Ice::RouterPrx::uncheckedCast(getProxy(proxy));

    assert(self->adapter);
    try
    {
        AllowThreads allowThreads; // Release Python's global interpreter lock during blocking calls.
        (*self->adapter)->addRouter(router);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

#ifdef WIN32
extern "C"
#endif
static PyObject*
adapterSetLocator(ObjectAdapterObject* self, PyObject* args)
{
    PyObject* proxyType = lookupType("Ice.LocatorPrx");
    PyObject* proxy;
    if(!PyArg_ParseTuple(args, "O!", proxyType, &proxy))
    {
        return NULL;
    }

    Ice::LocatorPrx locator = Ice::LocatorPrx::uncheckedCast(getProxy(proxy));

    assert(self->adapter);
    try
    {
        AllowThreads allowThreads; // Release Python's global interpreter lock during blocking calls.
        (*self->adapter)->setLocator(locator);
    }
    catch(const Ice::Exception& ex)
    {
        setPythonException(ex);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyMethodDef AdapterMethods[] =
{
    { "getName", (PyCFunction)adapterGetName, METH_NOARGS,
        PyDoc_STR("getName() -> string") },
    { "getCommunicator", (PyCFunction)adapterGetCommunicator, METH_NOARGS,
        PyDoc_STR("getCommunicator() -> Ice.Communicator") },
    { "activate", (PyCFunction)adapterActivate, METH_NOARGS,
        PyDoc_STR("activate() -> None") },
    { "hold", (PyCFunction)adapterHold, METH_NOARGS,
        PyDoc_STR("hold() -> None") },
    { "waitForHold", (PyCFunction)adapterWaitForHold, METH_NOARGS,
        PyDoc_STR("waitForHold() -> None") },
    { "deactivate", (PyCFunction)adapterDeactivate, METH_NOARGS,
        PyDoc_STR("deactivate() -> None") },
    { "waitForDeactivate", (PyCFunction)adapterWaitForDeactivate, METH_NOARGS,
        PyDoc_STR("waitForDeactivate() -> None") },
    { "add", (PyCFunction)adapterAdd, METH_VARARGS,
        PyDoc_STR("add(servant, identity) -> Ice.ObjectPrx") },
    { "addFacet", (PyCFunction)adapterAddFacet, METH_VARARGS,
        PyDoc_STR("addFacet(servant, identity, facet) -> Ice.ObjectPrx") },
    { "addWithUUID", (PyCFunction)adapterAddWithUUID, METH_VARARGS,
        PyDoc_STR("addWithUUID(servant) -> Ice.ObjectPrx") },
    { "addFacetWithUUID", (PyCFunction)adapterAddFacetWithUUID, METH_VARARGS,
        PyDoc_STR("addFacetWithUUID(servant, facet) -> Ice.ObjectPrx") },
    { "remove", (PyCFunction)adapterRemove, METH_VARARGS,
        PyDoc_STR("remove(identity) -> Ice.Object") },
    { "removeFacet", (PyCFunction)adapterRemoveFacet, METH_VARARGS,
        PyDoc_STR("removeFacet(identity, facet) -> Ice.Object") },
    { "removeAllFacets", (PyCFunction)adapterRemoveAllFacets, METH_VARARGS,
        PyDoc_STR("removeAllFacets(identity) -> dictionary") },
    { "find", (PyCFunction)adapterFind, METH_VARARGS,
        PyDoc_STR("find(identity) -> Ice.Object") },
    { "findFacet", (PyCFunction)adapterFindFacet, METH_VARARGS,
        PyDoc_STR("findFacet(identity, facet) -> Ice.Object") },
    { "findAllFacets", (PyCFunction)adapterFindAllFacets, METH_VARARGS,
        PyDoc_STR("findAllFacets(identity) -> dictionary") },
    { "findByProxy", (PyCFunction)adapterFindByProxy, METH_VARARGS,
        PyDoc_STR("findByProxy(Ice.ObjectPrx) -> Ice.Object") },
    { "addServantLocator", (PyCFunction)adapterAddServantLocator, METH_VARARGS,
        PyDoc_STR("addServantLocator(Ice.ServantLocator, category) -> None") },
    { "findServantLocator", (PyCFunction)adapterFindServantLocator, METH_VARARGS,
        PyDoc_STR("findServantLocator(category) -> Ice.ServantLocator") },
    { "createProxy", (PyCFunction)adapterCreateProxy, METH_VARARGS,
        PyDoc_STR("createProxy(identity) -> Ice.ObjectPrx") },
    { "createDirectProxy", (PyCFunction)adapterCreateDirectProxy, METH_VARARGS,
        PyDoc_STR("createDirectProxy(identity) -> Ice.ObjectPrx") },
    { "createReverseProxy", (PyCFunction)adapterCreateReverseProxy, METH_VARARGS,
        PyDoc_STR("createReverseProxy(identity) -> Ice.ObjectPrx") },
    { "addRouter", (PyCFunction)adapterAddRouter, METH_VARARGS,
        PyDoc_STR("addRouter(proxy) -> None") },
    { "setLocator", (PyCFunction)adapterSetLocator, METH_VARARGS,
        PyDoc_STR("setLocator(proxy) -> None") },
    { NULL, NULL} /* sentinel */
};

namespace IcePy
{

PyTypeObject ObjectAdapterType =
{
    /* The ob_type field must be initialized in the module init function
     * to be portable to Windows without using C++. */
    PyObject_HEAD_INIT(NULL)
    0,                               /* ob_size */
    "IcePy.ObjectAdapter",           /* tp_name */
    sizeof(ObjectAdapterObject),     /* tp_basicsize */
    0,                               /* tp_itemsize */
    /* methods */
    (destructor)adapterDealloc,      /* tp_dealloc */
    0,                               /* tp_print */
    0,                               /* tp_getattr */
    0,                               /* tp_setattr */
    0,                               /* tp_compare */
    0,                               /* tp_repr */
    0,                               /* tp_as_number */
    0,                               /* tp_as_sequence */
    0,                               /* tp_as_mapping */
    0,                               /* tp_hash */
    0,                               /* tp_call */
    0,                               /* tp_str */
    0,                               /* tp_getattro */
    0,                               /* tp_setattro */
    0,                               /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,              /* tp_flags */
    0,                               /* tp_doc */
    0,                               /* tp_traverse */
    0,                               /* tp_clear */
    0,                               /* tp_richcompare */
    0,                               /* tp_weaklistoffset */
    0,                               /* tp_iter */
    0,                               /* tp_iternext */
    AdapterMethods,                  /* tp_methods */
    0,                               /* tp_members */
    0,                               /* tp_getset */
    0,                               /* tp_base */
    0,                               /* tp_dict */
    0,                               /* tp_descr_get */
    0,                               /* tp_descr_set */
    0,                               /* tp_dictoffset */
    0,                               /* tp_init */
    0,                               /* tp_alloc */
    (newfunc)adapterNew,             /* tp_new */
    0,                               /* tp_free */
    0,                               /* tp_is_gc */
};

}

bool
IcePy::initObjectAdapter(PyObject* module)
{
    if(PyType_Ready(&ObjectAdapterType) < 0)
    {
        return false;
    }
    if(PyModule_AddObject(module, "ObjectAdapter", (PyObject*)&ObjectAdapterType) < 0)
    {
        return false;
    }

    return true;
}

PyObject*
IcePy::createObjectAdapter(const Ice::ObjectAdapterPtr& adapter)
{
    ObjectAdapterObject* obj = PyObject_New(ObjectAdapterObject, &ObjectAdapterType);
    if (obj != NULL)
    {
        obj->adapter = new Ice::ObjectAdapterPtr(adapter);
    }
    return (PyObject*)obj;
}
