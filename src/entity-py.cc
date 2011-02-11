// Copyright 2010, Florent Lamiraux, Thomas Moulard, LAAS-CNRS.
//
// This file is part of dynamic-graph-python.
// dynamic-graph-python is free software: you can redistribute it
// and/or modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// dynamic-graph-python is distributed in the hope that it will be
// useful, but WITHOUT ANY WARRANTY; without even the implied warranty
// of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Lesser Public License for more details.  You should
// have received a copy of the GNU Lesser General Public License along
// with dynamic-graph. If not, see <http://www.gnu.org/licenses/>.

#include <Python.h>
#include <iostream>

#include <dynamic-graph/entity.h>
#include <dynamic-graph/factory.h>
#include <../src/convert-dg-to-py.hh>

#include <dynamic-graph/command.h>
#include <dynamic-graph/value.h>
#include <dynamic-graph/pool.h>

using dynamicgraph::Entity;
using dynamicgraph::SignalBase;
using dynamicgraph::ExceptionAbstract;
using dynamicgraph::command::Command;
using dynamicgraph::command::Value;
using dynamicgraph::Vector;
using dynamicgraph::Matrix;

namespace dynamicgraph {
  namespace python {

    extern PyObject* error;
    using namespace convert;

    namespace entity {

      /**
	 \brief Create an instance of Entity
      */
      PyObject* create(PyObject* /*self*/, PyObject* args)
      {
	char *className = NULL;
	char *instanceName = NULL;

	if (!PyArg_ParseTuple(args, "ss", &className, &instanceName))
	  return NULL;

	Entity* obj = NULL;
	/* Try to find if the corresponding object already exists. */
	if( dynamicgraph::g_pool.existEntity( instanceName,obj ) )
	  {
	    if( obj->getClassName()!=className )
	      throw ExceptionPython( ExceptionPython::CLASS_INCONSISTENT,
				     "Found an object with the same name but of different class." );
	  }
	else /* If not, create a new object. */
	  {
	    try {
	      obj = dynamicgraph::g_factory.newEntity(std::string(className),
						      std::string(instanceName));
	    } catch (std::exception& exc) {
	      PyErr_SetString(error, exc.what());
	      return NULL;
	    }
	  }

	// Return the pointer as a PyCObject
	return PyCObject_FromVoidPtr((void*)obj, NULL);
      }

      /**
	 \brief Get name of entity
      */
      PyObject* getName(PyObject* /*self*/, PyObject* args)
      {
	PyObject* object = NULL;
	void* pointer = NULL;
	std::string name;

	if (!PyArg_ParseTuple(args, "O", &object))
	  return NULL;
	if (!PyCObject_Check(object)) {
	  PyErr_SetString(PyExc_TypeError,
			  "function takes a PyCObject as argument");
	  return NULL;
	}

	pointer = PyCObject_AsVoidPtr(object);
	Entity* entity = (Entity*)pointer;

	try {
	 name = entity->getName();
	} catch(ExceptionAbstract& exc) {
	  PyErr_SetString(error, exc.getStringMessage().c_str());
	  return NULL;
	}
	return Py_BuildValue("s", name.c_str());
      }

      /**
	 \brief Get a signal by name
      */
      PyObject* getSignal(PyObject* /*self*/, PyObject* args)
      {
	char *name = NULL;
	PyObject* object = NULL;
	void* pointer = NULL;

	if (!PyArg_ParseTuple(args, "Os", &object, &name))
	  return NULL;

	if (!PyCObject_Check(object)) {
	  PyErr_SetString(PyExc_TypeError,
			  "function takes a PyCObject as argument");
	  return NULL;
	}

	pointer = PyCObject_AsVoidPtr(object);
	Entity* entity = (Entity*)pointer;

	SignalBase<int>* signal = NULL;
	try {
	  signal = &(entity->getSignal(std::string(name)));
	} catch(ExceptionAbstract& exc) {
	  PyErr_SetString(error, exc.getStringMessage().c_str());
	  return NULL;
	}
	// Return the pointer to the signal without destructor since the signal
	// is not owned by the calling object but by the Entity.
	return PyCObject_FromVoidPtr((void*)signal, NULL);
      }

      PyObject* listSignals(PyObject* /*self*/, PyObject* args)
      {
	void* pointer = NULL;
	PyObject* object = NULL;

	if (!PyArg_ParseTuple(args, "O", &object))
	  return NULL;

	if (!PyCObject_Check(object))
	  return NULL;

	pointer = PyCObject_AsVoidPtr(object);
	Entity* entity = (Entity*)pointer;

	try {
	  Entity::SignalMap signalMap = entity->getSignalMap();
	  // Create a tuple of same size as the command map
	  PyObject* result = PyTuple_New(signalMap.size());
	  unsigned int count = 0;

	  for (Entity::SignalMap::iterator it = signalMap.begin();
	       it != signalMap.end(); it++) {
	    SignalBase<int>* signal = it->second;
	    PyObject* pySignal = PyCObject_FromVoidPtr((void*)signal, NULL);
	    PyTuple_SET_ITEM(result, count, pySignal);
	    count++;
	  }
	  return result;
	} catch(ExceptionAbstract& exc) {
	  PyErr_SetString(error, exc.getStringMessage().c_str());
	}
	return NULL;
      }

      PyObject* executeCommand(PyObject* /*self*/, PyObject* args)
      {
	PyObject* object = NULL;
	PyObject* argTuple = NULL;
	char* commandName = NULL;
	void* pointer = NULL;

	if (!PyArg_ParseTuple(args, "OsO", &object, &commandName, &argTuple)) {
	  return NULL;
	}

	// Retrieve the entity instance
	if (!PyCObject_Check(object)) {
	  PyErr_SetString(PyExc_TypeError, "first argument is not an object");
	  return NULL;
	}
	pointer = PyCObject_AsVoidPtr(object);
	Entity* entity = (Entity*)pointer;

	// Retrieve the argument tuple
	if (!PyTuple_Check(argTuple)) {
	  PyErr_SetString(PyExc_TypeError, "third argument is not a tuple");
	  return NULL;
	}
	unsigned int size = PyTuple_Size(argTuple);

	std::map<const std::string, Command*> commandMap =
	  entity->getNewStyleCommandMap();

	if (commandMap.count(std::string(commandName)) != 1) {
	  std::string msg = "command " + std::string(commandName) +
	    " is not referenced in Entity " + entity->getName();
	  PyErr_SetString(error, msg.c_str());
	  return NULL;
	}
	Command* command = commandMap[std::string(commandName)];
	// Check that tuple size is equal to command number of arguments
	const std::vector<Value::Type> typeVector = command->valueTypes();
	if (size != typeVector.size()) {
	  std::stringstream ss;
	  ss << "command takes " <<  typeVector.size()
	     << " parameters, " << size << " given.";
	  PyErr_SetString(error, ss.str().c_str());
	  return NULL;
	}

	std::vector<Value> valueVector;
	for (unsigned int iParam=0; iParam<size; iParam++) {
	  PyObject* PyValue = PyTuple_GetItem(argTuple, iParam);
	  Value::Type valueType = typeVector[iParam];
	  try {
	    Value value = pythonToValue(PyValue, valueType);
	    valueVector.push_back(value);
	  } catch (ExceptionAbstract& exc) {
	    std::stringstream ss;
	    ss << "Error while parsing argument " << iParam+1 << ": "
	       << exc.what() << ".";
	    PyErr_SetString(error, ss.str().c_str()) ;
	    return NULL;
	  }
	}
	command->setParameterValues(valueVector);
	try {
	  Value result = command->execute();
	  return valueToPython(result);
	} catch (const std::exception& exc) {
	  PyErr_SetString(error, exc.what()) ;
	  return NULL;
	}
	return NULL;
      }

      PyObject* listCommands(PyObject* /*self*/, PyObject* args)
      {
	PyObject* object = NULL;
	if (!PyArg_ParseTuple(args, "O", &object)) {
	  return NULL;
	}

	// Retrieve the entity instance
	if (!PyCObject_Check(object)) {
	  PyErr_SetString(PyExc_TypeError,
			  "function takes a PyCObject as argument");
	  return NULL;
	}
	void* pointer = PyCObject_AsVoidPtr(object);
	Entity* entity = (Entity*)pointer;
	typedef	std::map<const std::string, command::Command*>  CommandMap;
	CommandMap map = entity->getNewStyleCommandMap();
	unsigned int nbCommands = map.size();
	// Create a tuple of same size as the command map
	PyObject* result = PyTuple_New(nbCommands);
	unsigned int count = 0;
	for (CommandMap::iterator it=map.begin();
	     it != map.end(); it++) {
	  std::string commandName = it->first;
	  PyObject* pyName = Py_BuildValue("s", commandName.c_str());
	  PyTuple_SET_ITEM(result, count, pyName);
	  count++;
	}
	return result;
      }
      PyObject* getCommandDocstring(PyObject* /*self*/, PyObject* args)
      {
	PyObject* object = NULL;
	char* commandName;
	if (!PyArg_ParseTuple(args, "Os", &object, &commandName)) {
	  return NULL;
	}

	// Retrieve the entity instance
	if (!PyCObject_Check(object)) {
	  PyErr_SetString(error, "first argument is not an object");
	  return NULL;
	}
	void* pointer = PyCObject_AsVoidPtr(object);
	Entity* entity = (Entity*)pointer;
	typedef	std::map<const std::string, command::Command*>  CommandMap;
	CommandMap map = entity->getNewStyleCommandMap();
	command::Command* command = NULL;
	try {
	  command = map[commandName];
	} catch (const std::exception& exc) {
	  PyErr_SetString(error, exc.what());
	}
	std::string docstring = command->getDocstring();
	return Py_BuildValue("s", docstring.c_str());
      }

      PyObject* display(PyObject* /*self*/, PyObject* args)
      {
	/* Retrieve the entity instance. */
	PyObject* object = NULL;
	if (!PyArg_ParseTuple(args, "O", &object)
	    || (!PyCObject_Check(object)) )
	  {
	    PyErr_SetString(error, "first argument is not an object");
	    return NULL;
	  }
	void* pointer = PyCObject_AsVoidPtr(object);
	Entity* entity = (Entity*)pointer;

	/* Run the display function. */
	std::ostringstream oss;
	entity->display(oss);

	/* Return the resulting string. */
	return Py_BuildValue("s", oss.str().c_str());
      }
    }
  }
}
