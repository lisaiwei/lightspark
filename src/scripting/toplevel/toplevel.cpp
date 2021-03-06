/**************************************************************************
    Lightspark, a free flash player implementation

    Copyright (C) 2009-2013  Alessandro Pignotti (a.pignotti@sssup.it)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include <list>
#include <algorithm>
#include <cstring>
#include <sstream>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <iomanip>
#define _USE_MATH_DEFINES
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cerrno>

#include <glib.h>

#include "scripting/abc.h"
#include "scripting/toplevel/toplevel.h"
#include "scripting/flash/events/flashevents.h"
#include "swf.h"
#include "compat.h"
#include "scripting/class.h"
#include "exceptions.h"
#include "backends/urlutils.h"
#include "parsing/amf3_generator.h"
#include "scripting/argconv.h"
#include "scripting/toplevel/Number.h"

using namespace std;
using namespace lightspark;

Any* const Type::anyType = new Any();
Void* const Type::voidType = new Void();

Null::Null():ASObject((Class_base*)(NULL),T_NULL)
{
	traitsInitialized = true;
	constructIndicator = true;
	constructorCallComplete = true;
}

Undefined::Undefined():ASObject((Class_base*)(NULL),T_UNDEFINED)
{
	traitsInitialized = true;
	constructIndicator = true;
	constructorCallComplete = true;
}

ASFUNCTIONBODY(Undefined,call)
{
	LOG_CALL(_("Undefined function"));
	return NULL;
}

TRISTATE Undefined::isLess(ASObject* r)
{
	//ECMA-262 complaiant
	//As undefined became NaN when converted to number the operation is undefined
	return TUNDEFINED;
}

bool Undefined::isEqual(ASObject* r)
{
	switch(r->getObjectType())
	{
		case T_UNDEFINED:
		case T_NULL:
			return true;
		case T_NUMBER:
		case T_INTEGER:
		case T_UINTEGER:
		case T_BOOLEAN:
			return false;
		case T_FUNCTION:
		case T_STRING:
			if (!r->isConstructed())
				return true;
			return false;
		default:
			return r->isEqual(this);
	}
}

int32_t Undefined::toInt()
{
	return 0;
}
int64_t Undefined::toInt64()
{
	return 0;
}

ASObject *Undefined::describeType() const
{
	return ASObject::describeType();
}

void Undefined::serialize(ByteArray* out, std::map<tiny_string, uint32_t>& stringMap,
				std::map<const ASObject*, uint32_t>& objMap,
				std::map<const Class_base*, uint32_t>& traitsMap)
{
	if (out->getObjectEncoding() == ObjectEncoding::AMF0)
		out->writeByte(amf0_undefined_marker);
	else
		out->writeByte(undefined_marker);
}

void Undefined::setVariableByMultiname(const multiname& name, ASObject* o, CONST_ALLOWED_FLAG allowConst)
{
	LOG(LOG_ERROR,"trying to set variable on undefined:"<<name <<" "<<o->toDebugString());
	throwError<TypeError>(kConvertUndefinedToObjectError);
}

IFunction::IFunction(Class_base* c):ASObject(c,T_FUNCTION),length(0),inClass(NULL),functionname(0)
{
}

void IFunction::sinit(Class_base* c)
{
	c->isReusable=true;
	c->prototype->setVariableByQName("toString","",Class<IFunction>::getFunction(c->getSystemState(),IFunction::_toString),DYNAMIC_TRAIT);

	c->setDeclaredMethodByQName("call","",Class<IFunction>::getFunction(c->getSystemState(),IFunction::_call,1),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("call",AS3,Class<IFunction>::getFunction(c->getSystemState(),IFunction::_call,1),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("apply","",Class<IFunction>::getFunction(c->getSystemState(),IFunction::apply,2),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("apply",AS3,Class<IFunction>::getFunction(c->getSystemState(),IFunction::apply,2),NORMAL_METHOD,true);
	c->setDeclaredMethodByQName("length","",Class<IFunction>::getFunction(c->getSystemState(),IFunction::_getter_length),GETTER_METHOD,true);
	c->setDeclaredMethodByQName("toString","",Class<IFunction>::getFunction(c->getSystemState(),IFunction::_toString),NORMAL_METHOD,false);
}

ASFUNCTIONBODY_GETTER_SETTER(IFunction,prototype);
ASFUNCTIONBODY_GETTER(IFunction,length);

ASFUNCTIONBODY(IFunction,apply)
{
	/* This function never changes the 'this' pointer of a method closure */
	IFunction* th=static_cast<IFunction*>(obj);
	assert_and_throw(argslen<=2);

	ASObject* newObj=NULL;
	ASObject** newArgs=NULL;
	int newArgsLen=0;
	//Validate parameters
	if(argslen==0 || args[0]->is<Null>() || args[0]->is<Undefined>())
	{
		//get the current global object
		call_context* cc = getVm(obj->getSystemState())->currentCallContext;
		if (!cc->parent_scope_stack.isNull() && cc->parent_scope_stack->scope.size() > 0)
			newObj =cc->parent_scope_stack->scope[0].object.getPtr();
		else
		{
			assert_and_throw(cc->curr_scope_stack > 0);
			newObj =cc->scope_stack[0];
		}
		newObj->incRef();
	}
	else
	{
		newObj=args[0];
		newObj->incRef();
	}
	if(argslen == 2 && args[1]->getObjectType()==T_ARRAY)
	{
		Array* array=Class<Array>::cast(args[1]);
		newArgsLen=array->size();
		newArgs=g_newa(ASObject*, newArgsLen);
		for(int i=0;i<newArgsLen;i++)
		{
			_R<ASObject> val = array->at(i);
			val->incRef();
			newArgs[i]=val.getPtr();
		}
	}

	ASObject* ret=th->call(newObj,newArgs,newArgsLen);
	return ret;
}

ASFUNCTIONBODY(IFunction,_call)
{
	/* This function never changes the 'this' pointer of a method closure */
	IFunction* th=static_cast<IFunction*>(obj);
	ASObject* newObj=NULL;
	ASObject** newArgs=NULL;
	uint32_t newArgsLen=0;
	if(argslen==0 || args[0]->is<Null>() || args[0]->is<Undefined>())
	{
		//get the current global object
		call_context* cc = getVm(obj->getSystemState())->currentCallContext;
		if (!cc->parent_scope_stack.isNull() && cc->parent_scope_stack->scope.size() > 0)
			newObj =cc->parent_scope_stack->scope[0].object.getPtr();
		else
		{
			assert_and_throw(cc->curr_scope_stack > 0);
			newObj =cc->scope_stack[0];
		}
		newObj->incRef();
	}
	else
	{
		newObj=args[0];
		newObj->incRef();
	}
	if(argslen > 1)
	{
		newArgsLen=argslen-1;
		newArgs=g_newa(ASObject*, newArgsLen);
		for(unsigned int i=0;i<newArgsLen;i++)
		{
			newArgs[i]=args[i+1];
			newArgs[i]->incRef();
		}
	}
	ASObject* ret=th->call(newObj,newArgs,newArgsLen);
	return ret;
}

ASFUNCTIONBODY(IFunction,_toString)
{
	return abstract_s(obj->getSystemState(),"function Function() {}");
}

ASObject* Class<IFunction>::generator(ASObject* const* args, const unsigned int argslen)
{
	for(unsigned int i=0;i<argslen;i++)
		args[i]->decRef();
	if (argslen > 0)
		throwError<EvalError>(kFunctionConstructorError);
	return getNopFunction();
}

ASObject *IFunction::describeType() const
{
	pugi::xml_document p;
	pugi::xml_node root = p.append_child("type");
	root.append_attribute("name").set_value("Function");
	root.append_attribute("base").set_value("Object");
	root.append_attribute("isDynamic").set_value("true");
	root.append_attribute("isFinal").set_value("false");
	root.append_attribute("isStatic").set_value("false");

	pugi::xml_node node=root.append_child("extendsClass");
	node.append_attribute("type").set_value("Object");

	// TODO: accessor
	LOG(LOG_NOT_IMPLEMENTED, "describeType for Function not completely implemented");

	return XML::createFromNode(root);
}

SyntheticFunction::SyntheticFunction(Class_base* c,method_info* m):IFunction(c),mi(m),val(NULL),func_scope(NullRef)
{
	if(mi)
		length = mi->numArgs();
	// use second freelist, first is used by Function class
	objfreelist = &c->freelist[1];
}

/**
 * This prepares a new call_context and then executes the ABC bytecode function
 * by ABCVm::executeFunction() or through JIT.
 * It consumes one reference of obj and one of each arg
 */
ASObject* SyntheticFunction::call(ASObject* obj, ASObject* const* args, uint32_t numArgs)
{
	const uint32_t opt_hit_threshold=1;
	const uint32_t jit_hit_threshold=20;
	if (!mi->body)
		return getSystemState()->getUndefinedRef();

	const uint16_t hit_count = mi->body->hit_count;
	const method_body_info::CODE_STATUS& codeStatus = mi->body->codeStatus;

	uint32_t& cur_recursion = getVm(getSystemState())->cur_recursion;
	if(cur_recursion == getVm(getSystemState())->limits.max_recursion)
	{
		for(uint32_t i=0;i<numArgs;i++)
			args[i]->decRef();
		obj->decRef();
		throwError<ASError>(kStackOverflowError);
	}

	/* resolve argument and return types */
	if(!mi->returnType)
	{
		mi->hasExplicitTypes = false;
		mi->paramTypes.reserve(mi->numArgs());
		for(size_t i=0;i < mi->numArgs();++i)
		{
			const Type* t = Type::getTypeFromMultiname(mi->paramTypeName(i), mi->context);
			if (!t)
				throwError<ReferenceError>(kClassNotFoundError, mi->paramTypeName(i)->qualifiedString(getSystemState()));
			mi->paramTypes.push_back(t);
			if(t != Type::anyType)
				mi->hasExplicitTypes = true;
		}

		const Type* t = Type::getTypeFromMultiname(mi->returnTypeName(), mi->context);
		if (!t)
			throwError<ReferenceError>(kClassNotFoundError, mi->returnTypeName()->qualifiedString(getSystemState()));
		mi->returnType = t;
	}

	if(numArgs < mi->numArgs()-mi->numOptions())
	{
		/* Not enough arguments provided.
		 * We throw if this is a method.
		 * We won't throw if all arguments are of 'Any' type.
		 * This is in accordance with the proprietary player. */
		if(isMethod() || mi->hasExplicitTypes)
			throwError<ArgumentError>(kWrongArgumentCountError,
						  obj ? obj->getClassName() : "",
						  Integer::toString(mi->numArgs()-mi->numOptions()),
						  Integer::toString(numArgs));
	}

	//For sufficiently hot methods, optimize them to the internal bytecode
	if(hit_count>=opt_hit_threshold && codeStatus==method_body_info::ORIGINAL && getSystemState()->useFastInterpreter)
	{
		ABCVm::optimizeFunction(this);
	}

	//Temporarily disable JITting
	if(mi->body->exceptions.size()==0 && getSystemState()->useJit && ((hit_count>=jit_hit_threshold && codeStatus==method_body_info::OPTIMIZED) || getSystemState()->useInterpreter==false))
	{
		//We passed the hot function threshold, synt the function
		val=mi->synt_method(getSystemState());
		assert(val);
	}
	++mi->body->hit_count;

	//Prepare arguments
	uint32_t args_len=mi->numArgs();
	int passedToLocals=imin(numArgs,args_len);
	uint32_t passedToRest=(numArgs > args_len)?(numArgs-mi->numArgs()):0;

	/* setup argumentsArray if needed */
	Array* argumentsArray = NULL;
	if(mi->needsArgs())
	{
		//The arguments does not contain default values of optional parameters,
		//i.e. f(a,b=3) called as f(7) gives arguments = { 7 }
		argumentsArray=Class<Array>::getInstanceSNoArgs(obj->getSystemState());
		argumentsArray->resize(numArgs);
		for(uint32_t j=0;j<numArgs;j++)
		{
			args[j]->incRef();
			argumentsArray->set(j,_MR(args[j]));
		}
		//Add ourself as the callee property
		incRef();
		argumentsArray->setVariableByQName("callee","",this,DECLARED_TRAIT);
	}

	/* setup call_context */
	call_context cc;
	cc.inClass = inClass;
	cc.mi=mi;
	cc.locals_size=mi->body->local_count+1;
	ASObject** locals = g_newa(ASObject*, cc.locals_size);
	cc.locals=locals;
	memset(cc.locals,0,sizeof(ASObject*)*cc.locals_size);
	cc.max_stack = mi->body->max_stack;
	ASObject** stack = g_newa(ASObject*, cc.max_stack);
	cc.stack=stack;
	cc.stack_index=0;
	cc.context=mi->context;
	//cc.code= new istringstream(mi->body->code);
	cc.parent_scope_stack=func_scope;
	cc.exec_pos=0;
	
	cc.max_scope_stack = mi->body->max_scope_depth;
	cc.curr_scope_stack= 0;
	cc.scope_stack=g_newa(ASObject*, cc.max_scope_stack);
	cc.scope_stack_dynamic=g_newa(bool, cc.max_scope_stack);
	
	memset(cc.scope_stack,0,sizeof(ASObject*)*cc.max_scope_stack);
	cc.stack_index=0;
	
	call_context* saved_cc = getVm(getSystemState())->currentCallContext;
	cc.defaultNamespaceUri = saved_cc ? saved_cc->defaultNamespaceUri : (uint32_t)BUILTIN_STRINGS::EMPTY;

	/* Set the current global object, each script in each DoABCTag has its own */
	getVm(getSystemState())->currentCallContext = &cc;

	if(isBound() && obj != closure_this.getPtr())
	{ /* closure_this can never been overriden */
		LOG_CALL(_("Calling with closure ") << this);
		if(obj)
			obj->decRef();
		obj=closure_this.getPtr();
		obj->incRef();
	}

	assert_and_throw(obj);
	obj->incRef(); //this is free'd in ~call_context
	cc.locals[0]=obj;

	/* coerce arguments to expected types */
	for(int i=0;i<passedToLocals;++i)
	{
		cc.locals[i+1] = mi->paramTypes[i]->coerce(args[i]);
	}

	//Fill missing parameters until optional parameters begin
	//like fun(a,b,c,d=3,e=5) called as fun(1,2) becomes
	//locals = {this, 1, 2, Undefined, 3, 5}
	for(uint32_t i=passedToLocals;i<args_len;++i)
	{
		int iOptional = mi->numOptions()-args_len+i;
		if(iOptional >= 0)
			cc.locals[i+1]=mi->paramTypes[i]->coerce(mi->getOptional(iOptional));
		else {
			assert(mi->paramTypes[i] == Type::anyType);
			cc.locals[i+1]=getSystemState()->getUndefinedRef();
		}
	}
	cc.argarrayposition = -1;
	if(mi->needsArgs())
	{
		assert_and_throw(cc.locals_size>args_len+1);
		cc.locals[args_len+1]=argumentsArray;
		cc.argarrayposition=args_len+1;
	}
	else if(mi->needsRest()|| passedToRest > 0) // it seems that Adobe allows additional parameters without setting "needsRest"
	{
		assert_and_throw(argumentsArray==NULL);
		Array* rest=Class<Array>::getInstanceSNoArgs(getSystemState());
		rest->resize(passedToRest);
		//Give the reference of the other args to an array
		for(uint32_t j=0;j<passedToRest;j++)
		{
			args[passedToLocals+j]->incRef();
			rest->set(j,_MR(args[passedToLocals+j]));
		}

		assert_and_throw(cc.locals_size>args_len+1);
		cc.locals[args_len+1]=rest;
		cc.argarrayposition= args_len+1;
	}
	//Parameters are ready

	ASObject* ret;
	//obtain a local reference to this function, as it may delete itself
	this->incRef();

	++cur_recursion; //increment current recursion depth
#ifndef NDEBUG
	Log::calls_indent++;
#endif
	getVm(getSystemState())->stacktrace.push_back(std::pair<uint32_t,ASObject*>(this->functionname,obj));
	while (true)
	{
		try
		{
			if(mi->body->exceptions.size() || (val==NULL && getSystemState()->useInterpreter))
			{
				if(codeStatus == method_body_info::OPTIMIZED && getSystemState()->useFastInterpreter)
				{
					//This is a mildy hot function, execute it using the fast interpreter
					ret=ABCVm::executeFunctionFast(this,&cc,obj);
				}
				else
				{
					//Switch the codeStatus to USED to make sure the method will not be optimized while being used
					const method_body_info::CODE_STATUS oldCodeStatus = codeStatus;
					mi->body->codeStatus = method_body_info::USED;
					//This is not a hot function, execute it using the interpreter
					ret=ABCVm::executeFunction(this,&cc,obj);
					//Restore the previous codeStatus
					mi->body->codeStatus = oldCodeStatus;
				}
			}
			else
				ret=val(&cc);
		}
		catch (ASObject* excobj) // Doesn't have to be an ASError at all.
		{
			unsigned int pos = cc.exec_pos;
			bool no_handler = true;

			LOG(LOG_TRACE, "got an " << excobj->toString());
			LOG(LOG_TRACE, "pos=" << pos);
			for (unsigned int i=0;i<mi->body->exceptions.size();i++)
			{
				exception_info exc=mi->body->exceptions[i];
				multiname* name=mi->context->getMultiname(exc.exc_type, NULL);
				LOG(LOG_TRACE, "f=" << exc.from << " t=" << exc.to << " type=" << *name);
				if (pos >= exc.from && pos <= exc.to && mi->context->isinstance(excobj, name))
				{
					no_handler = false;
					cc.exec_pos = exc.target;
					cc.runtime_stack_clear();
					cc.runtime_stack_push(excobj);
					while (cc.curr_scope_stack)
					{
						cc.scope_stack[--cc.curr_scope_stack]->decRef();
					}
					break;
				}
			}
			if (no_handler)
			{
				--cur_recursion; //decrement current recursion depth
#ifndef NDEBUG
				Log::calls_indent--;
#endif
				getVm(getSystemState())->stacktrace.pop_back();
				getVm(getSystemState())->currentCallContext = saved_cc;
				throw;
			}
			continue;
		}
		break;
	}
	--cur_recursion; //decrement current recursion depth
	getVm(getSystemState())->stacktrace.pop_back();
#ifndef NDEBUG
	Log::calls_indent--;
#endif
	getVm(getSystemState())->currentCallContext = saved_cc;

	this->decRef(); //free local ref
	obj->decRef();

	if(ret==NULL)
		ret=getSystemState()->getUndefinedRef();

	return mi->returnType->coerce(ret);
}

/**
 * This executes a C++ function.
 * It consumes one reference of obj and one of each arg
 */
ASObject* Function::call(ASObject* obj, ASObject* const* args, uint32_t num_args)
{
	/*
	 * We do not enforce ABCVm::limits.max_recursion here.
	 * This should be okey, because there is no infinite recursion
	 * using only builtin functions.
	 * Additionally, we still need to run builtin code (such as the ASError constructor) when
	 * ABCVm::limits.max_recursion is reached in SyntheticFunction::call.
	 */
	ASObject* ret;
	if(isBound())
	{ /* closure_this can never been overriden */
		LOG_CALL(_("Calling with closure ") << this);
		if(obj)
			obj->decRef();
		obj=closure_this.getPtr();
		obj->incRef();
	}
	assert_and_throw(obj);
	try
	{
		ret=val(obj,args,num_args);
	}
	catch(ASObject* excobj)
	{
		//If an exception is thrown from native code, clean up and rethrow
		for(uint32_t i=0;i<num_args;i++)
			args[i]->decRef();
		obj->decRef();
		throw;
	}

	for(uint32_t i=0;i<num_args;i++)
		args[i]->decRef();
	obj->decRef();
	if(ret==NULL)
		ret=getSystemState()->getUndefinedRef();
	return ret;
}
bool Function::isEqual(ASObject* r)
{
	Function* f=dynamic_cast<Function*>(r);
	if(f==NULL)
		return false;
	return (val==f->val) && (closure_this.isNull() || f->closure_this.isNull() || closure_this==f->closure_this);
}

bool Null::isEqual(ASObject* r)
{
	switch(r->getObjectType())
	{
		case T_NULL:
		case T_UNDEFINED:
			return true;
		case T_INTEGER:
		case T_UINTEGER:
		case T_NUMBER:
		case T_BOOLEAN:
			return false;
		case T_FUNCTION:
		case T_STRING:
			if (!r->isConstructed())
				return true;
			return false;
		default:
			return r->isEqual(this);
	}
}

TRISTATE Null::isLess(ASObject* r)
{
	if(r->getObjectType()==T_INTEGER)
	{
		Integer* i=static_cast<Integer*>(r);
		return (0<i->toInt())?TTRUE:TFALSE;
	}
	else if(r->getObjectType()==T_UINTEGER)
	{
		UInteger* i=static_cast<UInteger*>(r);
		return (0<i->toUInt())?TTRUE:TFALSE;
	}
	else if(r->getObjectType()==T_NUMBER)
	{
		Number* i=static_cast<Number*>(r);
		if(std::isnan(i->toNumber())) return TUNDEFINED;
		return (0<i->toNumber())?TTRUE:TFALSE;
	}
	else if(r->getObjectType()==T_BOOLEAN)
	{
		Boolean* i=static_cast<Boolean*>(r);
		return (0<i->val)?TTRUE:TFALSE;
	}
	else if(r->getObjectType()==T_NULL)
	{
		return TFALSE;
	}
	else if(r->getObjectType()==T_UNDEFINED)
	{
		return TUNDEFINED;
	}
	else if(r->getObjectType()==T_STRING)
	{
		double val2=r->toNumber();
		if(std::isnan(val2)) return TUNDEFINED;
		return (0<val2)?TTRUE:TFALSE;
	}
	else
	{
		double val2=r->toPrimitive()->toNumber();
		if(std::isnan(val2)) return TUNDEFINED;
		return (0<val2)?TTRUE:TFALSE;
	}
}

int32_t Null::getVariableByMultiname_i(const multiname& name)
{
	LOG(LOG_ERROR,"trying to get variable on null:"<<name);
	throwError<TypeError>(kConvertNullToObjectError);
	return 0;
}

_NR<ASObject> Null::getVariableByMultiname(const multiname& name, GET_VARIABLE_OPTION opt)
{
	LOG(LOG_ERROR,"trying to get variable on null:"<<name);
	throwError<TypeError>(kConvertNullToObjectError);
	return NullRef;
}

int32_t Null::toInt()
{
	return 0;
}
int64_t Null::toInt64()
{
	return 0;
}

void Null::serialize(ByteArray* out, std::map<tiny_string, uint32_t>& stringMap,
				std::map<const ASObject*, uint32_t>& objMap,
				std::map<const Class_base*, uint32_t>& traitsMap)
{
	if (out->getObjectEncoding() == ObjectEncoding::AMF0)
		out->writeByte(amf0_null_marker);
	else
		out->writeByte(null_marker);
}

void Null::setVariableByMultiname(const multiname& name, ASObject* o, CONST_ALLOWED_FLAG allowConst)
{
	LOG(LOG_ERROR,"trying to set variable on null:"<<name<<" value:"<<o->toDebugString());
	o->decRef();
	throwError<TypeError>(kConvertNullToObjectError);
}

ASObject* Void::coerce(ASObject* o) const
{
	if(!o->is<Undefined>())
		throw Class<TypeError>::getInstanceS(o->getSystemState(),"Trying to coerce o!=undefined to void");
	return o;
}

const Type* Type::getBuiltinType(SystemState *sys, const multiname* mn)
{
	if(mn->isStatic && mn->cachedType)
		return mn->cachedType;
	assert_and_throw(mn->isQName());
	assert(mn->name_type == multiname::NAME_STRING);
	if(mn == 0)
		return Type::anyType; //any
	if(mn->name_type == multiname::NAME_STRING && mn->name_s_id==BUILTIN_STRINGS::ANY
		&& mn->ns[0].hasEmptyName())
		return Type::anyType;
	if(mn->name_type == multiname::NAME_STRING && mn->name_s_id==BUILTIN_STRINGS::VOID
		&& mn->ns[0].hasEmptyName())
		return Type::voidType;

	//Check if the class has already been defined
	ASObject* target;
	ASObject* tmp=sys->systemDomain->getVariableAndTargetByMultiname(*mn, target);
	if(tmp && tmp->getObjectType()==T_CLASS)
		return static_cast<const Class_base*>(tmp);
	else
		return NULL;
}

/*
 * This should only be called after all global objects have been created
 * by running ABCContext::exec() for all ABCContexts.
 * Therefore, all classes are at least declared.
 */
const Type* Type::getTypeFromMultiname(const multiname* mn, ABCContext* context)
{
	if(mn == 0) //multiname idx zero indicates any type
		return Type::anyType;

	if(mn->isStatic && mn->cachedType)
		return mn->cachedType;

	if(mn->name_type == multiname::NAME_STRING && mn->name_s_id==BUILTIN_STRINGS::ANY
		&& mn->ns.size() == 1 && mn->ns[0].hasEmptyName())
		return Type::anyType;

	if(mn->name_type == multiname::NAME_STRING && mn->name_s_id==BUILTIN_STRINGS::VOID
		&& mn->ns.size() == 1 && mn->ns[0].hasEmptyName())
		return Type::voidType;

	ASObject* typeObject = NULL;
	/*
	 * During the newClass opcode, the class is added to context->root->applicationDomain->classesBeingDefined.
	 * The class variable in the global scope is only set a bit later.
	 * When the class has to be resolved in between (for example, the
	 * class has traits of the class's type), then we'll find it in
	 * classesBeingDefined, but context->root->getVariableAndTargetByMultiname()
	 * would still return "Undefined".
	 */
	auto i = context->root->applicationDomain->classesBeingDefined.find(mn);
	if(i != context->root->applicationDomain->classesBeingDefined.end())
		typeObject = i->second;
	else
	{
		ASObject* target;
		typeObject=context->root->applicationDomain->getVariableAndTargetByMultiname(*mn,target);
	}

	if(!typeObject)
	{
		if (mn->ns.size() >= 1 && mn->ns[0].nsNameId == BUILTIN_STRINGS::STRING_AS3VECTOR)
		{
			QName qname(mn->name_s_id,mn->ns[0].nsNameId);
			typeObject = Template<Vector>::getTemplateInstance(context->root->getSystemState(),qname,context,context->root->applicationDomain).getPtr();
		}
	}
	return typeObject ? typeObject->as<Type>() : NULL;
}

Class_base::Class_base(const QName& name, MemoryAccount* m):ASObject(Class_object::getClass(getSys()),T_CLASS),protected_ns(getSys(),"",NAMESPACE),constructor(NULL),
	borrowedVariables(m),
	context(NULL),class_name(name),memoryAccount(m),length(1),class_index(-1),isFinal(false),isSealed(false),isInterface(false),isReusable(false),use_protected(false)
{
	setConstant();
}

Class_base::Class_base(const Class_object*):ASObject((MemoryAccount*)NULL),protected_ns(getSys(),BUILTIN_STRINGS::EMPTY,NAMESPACE),constructor(NULL),
	borrowedVariables(NULL),
	context(NULL),class_name(BUILTIN_STRINGS::STRING_CLASS,BUILTIN_STRINGS::EMPTY),memoryAccount(NULL),length(1),class_index(-1),isFinal(false),isSealed(false),isInterface(false),isReusable(false),use_protected(false)
{
	setConstant();
	type=T_CLASS;
	//We have tested that (Class is Class == true) so the classdef is 'this'
	setClass(this);
	//The super is Class<ASObject> but we set it in SystemState constructor to avoid an infinite loop
}

/*
 * This copies the non-static traits of the super class to this
 * class.
 *
 * If a property is in the protected namespace of the super class, a copy is
 * created with the protected namespace of this class.
 * That is necessary, because superclass methods are called with the protected ns
 * of the current class.
 *
 * use_protns and protectedns must be set before this function is called
 */
void Class_base::copyBorrowedTraitsFromSuper()
{
	//assert(borrowedVariables.Variables.empty());
	variables_map::var_iterator i = super->borrowedVariables.Variables.begin();
	for(;i != super->borrowedVariables.Variables.end(); ++i)
	{
		const varName& name = i->first;
		variable& v = i->second;
		if(v.var)
			v.var->incRef();
		if(v.getter)
			v.getter->incRef();
		if(v.setter)
			v.setter->incRef();

		borrowedVariables.Variables.insert(make_pair(name,v));
	}
}

void Class_base::initStandardProps()
{
	incRef();
	constructorprop = _NR<ObjectConstructor>(new_objectConstructor(this,0));
	constructorprop->incRef();
	addConstructorGetter();
	
	setDeclaredMethodByQName("toString","",Class<IFunction>::getFunction(getSystemState(),Class_base::_toString),NORMAL_METHOD,false);
	incRef();
	prototype->setVariableByQName("constructor","",this,DECLARED_TRAIT);

	if(super)
		prototype->prevPrototype=super->prototype;
	addPrototypeGetter();
	addLengthGetter();
}


ASObject* Class_base::coerce(ASObject* o) const
{
	if (o->is<Undefined>())
	{
		ASObject* res = o->getSystemState()->getNullRef();
		o->decRef();
		return res;
	}
	if(o->is<Null>())
		return o;
	if(o->is<Class_base>())
	{ /* classes can be cast to the type 'Object' or 'Class' */
		if(this == Class<ASObject>::getClass(o->getSystemState())
		|| (class_name.nameId==BUILTIN_STRINGS::STRING_CLASS && class_name.nsStringId==BUILTIN_STRINGS::EMPTY))
			return o; /* 'this' is the type of a class */
		else
			throwError<TypeError>(kCheckTypeFailedError, o->getClassName(), getQualifiedClassName());
	}
	if (o->is<ObjectConstructor>())
		return o;

	//o->getClass() == NULL for primitive types
	//those are handled in overloads Class<Number>::coerce etc.
	if(!o->getClass() || !o->getClass()->isSubClass(this))
		throwError<TypeError>(kCheckTypeFailedError, o->getClassName(), getQualifiedClassName());
	return o;
}

void Class_base::setSuper(Ref<Class_base> super_)
{
	assert(!super);
	super = super_;
	copyBorrowedTraitsFromSuper();
}

ASFUNCTIONBODY(Class_base,_toString)
{
	Class_base* th = obj->as<Class_base>();
	tiny_string ret;
	ret = "[class ";
	ret += obj->getSystemState()->getStringFromUniqueId(th->class_name.nameId);
	ret += "]";
	return abstract_s(obj->getSystemState(),ret);
}

void Class_base::addConstructorGetter()
{
	setDeclaredMethodByQName("constructor","",Class<IFunction>::getFunction(getSystemState(),_getter_constructorprop),GETTER_METHOD,false);
}

void Class_base::addPrototypeGetter()
{
	setDeclaredMethodByQName("prototype","",Class<IFunction>::getFunction(getSystemState(),_getter_prototype),GETTER_METHOD,false);
}

void Class_base::addLengthGetter()
{
	setDeclaredMethodByQName("length","",Class<IFunction>::getFunction(getSystemState(),_getter_length),GETTER_METHOD,false);
}

Class_base::~Class_base()
{
}

ASObject* Class_base::_getter_constructorprop(ASObject* obj, ASObject* const* args, const unsigned int argslen)
{
	Class_base* th = NULL;
	if(obj->is<Class_base>())
		th = obj->as<Class_base>();
	else
		th = obj->getClass();
	if(argslen != 0)
		throw Class<ArgumentError>::getInstanceS(obj->getSystemState(),"Arguments provided in getter");
	ASObject* ret=th->constructorprop.getPtr();
	ret->incRef();
	return ret;
}

ASObject* Class_base::_getter_prototype(ASObject* obj, ASObject* const* args, const unsigned int argslen)
{
	if(!obj->is<Class_base>())
		throw Class<ArgumentError>::getInstanceS(obj->getSystemState(),"Function applied to wrong object");
	Class_base* th = obj->as<Class_base>();
	if(argslen != 0)
		throw Class<ArgumentError>::getInstanceS(obj->getSystemState(),"Arguments provided in getter");
	ASObject* ret=th->prototype->getObj();
	ret->incRef();
	return ret;
}
ASFUNCTIONBODY_GETTER(Class_base, length);

ASObject* Class_base::generator(ASObject* const* args, const unsigned int argslen)
{
	ASObject *ret=ASObject::generator(NULL, args, argslen);
	for(unsigned int i=0;i<argslen;i++)
		args[i]->decRef();
	return ret;
}

void Class_base::addImplementedInterface(const multiname& i)
{
	interfaces.push_back(i);
}

void Class_base::addImplementedInterface(Class_base* i)
{
	interfaces_added.push_back(i);
}

tiny_string Class_base::toString()
{
	tiny_string ret="[class ";
	ret+=getSystemState()->getStringFromUniqueId(class_name.nameId);
	ret+="]";
	return ret;
}

void Class_base::setConstructor(IFunction* c)
{
	assert_and_throw(constructor==NULL);
	constructor=c;
}

void Class_base::handleConstruction(ASObject* target, ASObject* const* args, unsigned int argslen, bool buildAndLink)
{
	if(buildAndLink)
	{
		setupDeclaredTraits(target);

		//Tell the object that the construction is complete
		target->constructionComplete();
	}

	//TODO: is there any valid case for not having a constructor?
	if(constructor)
	{
		target->incRef();
		ASObject* ret=constructor->call(target,args,argslen);
		target->constructIndicator = true;
		assert_and_throw(ret->is<Undefined>());
		ret->decRef();
	}
	else
	{
		target->constructIndicator = true;
		for(uint32_t i=0;i<argslen;i++)
			args[i]->decRef();
		//throwError<TypeError>(kConstructOfNonFunctionError);
	}
}


void Class_base::destroy()
{
	if(constructor)
	{
		constructor->decRef();
		constructor=NULL;
	}
}

void Class_base::finalize()
{
	borrowedVariables.destroyContents();
	super.reset();
	prototype.reset();
	protected_ns = nsNameAndKind(getSystemState(),"",NAMESPACE);
	constructor = NULL;
	context = NULL;
	length = 1;
	class_index = -1;
	isFinal = false;
	isSealed = false;
	isInterface = false;
	use_protected = false;
}

Template_base::Template_base(QName name) : ASObject((Class_base*)(NULL)),template_name(name)
{
	type = T_TEMPLATE;
}

Class_object* Class_object::getClass(SystemState *sys)
{
	//We check if we are registered already
	//if not we register ourselves (see also Class<T>::getClass)
	//Class object position in the map is hardwired to 0
	uint32_t classId=0;
	Class_object* ret=NULL;
	Class_base** retAddr=&sys->builtinClasses[classId];
	if(*retAddr==NULL)
	{
		//Create the class
		ret=new (sys->unaccountedMemory) Class_object();
		ret->setSystemState(sys);
		ret->incRef();
		*retAddr=ret;
	}
	else
		ret=static_cast<Class_object*>(*retAddr);

	return ret;
}

const std::vector<Class_base*>& Class_base::getInterfaces(bool *alldefined) const
{
	if (alldefined)
		*alldefined = true;
	if(!interfaces.empty())
	{
		//Recursively get interfaces implemented by this interface
		std::vector<multiname>::iterator it = interfaces.begin();
		while (it !=interfaces.end())
		{
			ASObject* target;
			ASObject* interface_obj=this->context->root->applicationDomain->
					getVariableAndTargetByMultiname(*it, target);
			if (interface_obj)
			{
				assert_and_throw(interface_obj->getObjectType()==T_CLASS);
				Class_base* inter=static_cast<Class_base*>(interface_obj);
				//Probe the interface for its interfaces
				bool bAllDefinedSub;
				inter->getInterfaces(&bAllDefinedSub);
				
				if (bAllDefinedSub)
				{
					interfaces_added.push_back(inter);
					interfaces.erase(it);
					continue;
				}
				else if (alldefined)
					*alldefined = false;
			}
			else if (alldefined)
				*alldefined = false;
			it++;
		}
	}
	return interfaces_added;
}

void Class_base::linkInterface(Class_base* c) const
{
	assert(class_index!=-1);
	//Recursively link interfaces implemented by this interface
	const std::vector<Class_base*> interfaces = getInterfaces();
	for(unsigned int i=0;i<interfaces.size();i++)
		interfaces[i]->linkInterface(c);

	assert_and_throw(context);

	//Link traits of this interface
	for(unsigned int j=0;j<context->instances[class_index].trait_count;j++)
	{
		traits_info* t=&context->instances[class_index].traits[j];
		context->linkTrait(c,t);
	}

	if(constructor)
	{
		LOG_CALL(_("Calling interface init for ") << class_name);
		ASObject* ret=constructor->call(c,NULL,0);
		assert_and_throw(ret==NULL);
	}
}

bool Class_base::isSubClass(const Class_base* cls, bool considerInterfaces) const
{
	check();
	if(cls==this || cls==Class<ASObject>::getClass(cls->getSystemState()))
		return true;

	//Now check the interfaces
	//an interface can't be subclass of a normal class, we only check the interfaces if cls is an interface itself
	if (considerInterfaces && cls->isInterface)
	{
		const std::vector<Class_base*> interfaces = getInterfaces();
		for(unsigned int i=0;i<interfaces.size();i++)
		{
			if(interfaces[i]->isSubClass(cls, considerInterfaces))
				return true;
		}
	}

	//Now ask the super
	if(super && super->isSubClass(cls, considerInterfaces))
		return true;
	return false;
}

tiny_string Class_base::getQualifiedClassName() const
{
	if(class_index==-1)
		return class_name.getQualifiedName(getSystemState());
	else
	{
		assert_and_throw(context);
		int name_index=context->instances[class_index].name;
		assert_and_throw(name_index);
		const multiname* mname=context->getMultiname(name_index,NULL);
		return mname->qualifiedString(getSystemState());
	}
}

tiny_string Class_base::getName() const
{
	return (class_name.nsStringId == BUILTIN_STRINGS::EMPTY ? 
				this->getSystemState()->getStringFromUniqueId(class_name.nameId)
			  : this->getSystemState()->getStringFromUniqueId(class_name.nsStringId) +"$"+ this->getSystemState()->getStringFromUniqueId(class_name.nameId)); 
}

ASObject *Class_base::describeType() const
{
	pugi::xml_document p;
	pugi::xml_node root = p.append_child("type");

	root.append_attribute("name").set_value(getQualifiedClassName().raw_buf());
	root.append_attribute("base").set_value("Class");
	root.append_attribute("isDynamic").set_value("true");
	root.append_attribute("isFinal").set_value("true");
	root.append_attribute("isStatic").set_value("true");

	// extendsClass
	pugi::xml_node node=root.append_child("extendsClass");
	node.append_attribute("type").set_value("class");
	node=root.append_child("extendsClass");
	node.append_attribute("type").set_value("Object");


	// variable
	if(class_index>=0)
		describeTraits(root, context->classes[class_index].traits);

	// factory
	node=root.append_child("factory");
	node.append_attribute("type").set_value(getQualifiedClassName().raw_buf());
	describeInstance(node);
	return XML::createFromNode(root);
}

void Class_base::describeInstance(pugi::xml_node& root) const
{
	// extendsClass
	const Class_base* c=super.getPtr();
	while(c)
	{
		pugi::xml_node node=root.append_child("extendsClass");
		node.append_attribute("type").set_value(c->getQualifiedClassName().raw_buf());
		c=c->super.getPtr();
	}

	// implementsInterface
	c=this;
	while(c && c->class_index>=0)
	{
		const std::vector<Class_base*>& interfaces=c->getInterfaces();
		auto it=interfaces.begin();
		for(; it!=interfaces.end(); ++it)
		{
			pugi::xml_node node=root.append_child("implementsInterface");
			node.append_attribute("type").set_value((*it)->getQualifiedClassName().raw_buf());
		}
		c=c->super.getPtr();
	}

	// variables, methods, accessors
	c=this;
	if (c->class_index<0)
	{
		// builtin class
		LOG(LOG_NOT_IMPLEMENTED, "describeType for builtin classes not completely implemented:"<<this->class_name);
		std::map<tiny_string, pugi::xml_node*> instanceNodes;
		describeVariables(root,c,instanceNodes,Variables);
		describeVariables(root,c,instanceNodes,borrowedVariables);
	}
	while(c && c->class_index>=0)
	{
		c->describeTraits(root, c->context->instances[c->class_index].traits);
		c=c->super.getPtr();
	}
}

void Class_base::describeVariables(pugi::xml_node& root,const Class_base* c, std::map<tiny_string, pugi::xml_node*>& instanceNodes, const variables_map& map) const
{
	variables_map::const_var_iterator it=map.Variables.cbegin();
	for(;it!=map.Variables.cend();++it)
	{
		const char* nodename;
		const char* access = NULL;
		switch (it->second.kind)
		{
			case CONSTANT_TRAIT:
				nodename = "constant";
				break;
			case DECLARED_TRAIT:
			case INSTANCE_TRAIT:
				if (it->second.var)
					nodename="method";
				else
				{
					nodename="accessor";
					if (it->second.getter && it->second.setter)
						access = "readwrite";
					else if (it->second.getter)
						access = "readonly";
					else if (it->second.setter)
						access = "writeonly";
				}
				break;
			default:
				continue;
		}
		tiny_string name = getSystemState()->getStringFromUniqueId(it->first.nameId);
		auto existing=instanceNodes.find(name);
		if(existing != instanceNodes.cend())
			continue;

		pugi::xml_node node=root.append_child(nodename);
		node.append_attribute("name").set_value(name.raw_buf());
		if (access)
			node.append_attribute("access").set_value(access);
		instanceNodes[name] = &node;
	}
}
void Class_base::describeTraits(pugi::xml_node &root,
				std::vector<traits_info>& traits) const
{
	std::map<u30, pugi::xml_node> accessorNodes;
	for(unsigned int i=0;i<traits.size();i++)
	{
		traits_info& t=traits[i];
		int kind=t.kind&0xf;
		multiname* mname=context->getMultiname(t.name,NULL);
		if (mname->name_type!=multiname::NAME_STRING ||
		    (mname->ns.size()==1 && (!mname->ns[0].hasEmptyName() || mname->ns[0].kind == PRIVATE_NAMESPACE)) ||
		    mname->ns.size() > 1)
			continue;
		
		if(kind==traits_info::Slot || kind==traits_info::Const)
		{
			multiname* type=context->getMultiname(t.type_name,NULL);
			const char *nodename=kind==traits_info::Const?"constant":"variable";
			pugi::xml_node node=root.append_child(nodename);
			node.append_attribute("name").set_value(getSystemState()->getStringFromUniqueId(mname->name_s_id).raw_buf());
			node.append_attribute("type").set_value(type->qualifiedString(getSystemState()).raw_buf());

			describeMetadata(node, t);
		}
		else if (kind==traits_info::Method)
		{
			pugi::xml_node node=root.append_child("method");
			node.append_attribute("name").set_value(getSystemState()->getStringFromUniqueId(mname->name_s_id).raw_buf());
			node.append_attribute("declaredBy").set_value(getQualifiedClassName().raw_buf());

			method_info& method=context->methods[t.method];
			const multiname* rtname=method.returnTypeName();
			node.append_attribute("returnType").set_value(rtname->qualifiedString(getSystemState()).raw_buf());

			assert(method.numArgs() >= method.numOptions());
			uint32_t firstOpt=method.numArgs() - method.numOptions();
			for(uint32_t j=0;j<method.numArgs(); j++)
			{
				pugi::xml_node param=node.append_child("parameter");
				param.append_attribute("index").set_value(UInteger::toString(j+1).raw_buf());
				param.append_attribute("type").set_value(method.paramTypeName(j)->qualifiedString(getSystemState()).raw_buf());
				param.append_attribute("optional").set_value(j>=firstOpt?"true":"false");
			}

			describeMetadata(node, t);
		}
		else if (kind==traits_info::Getter || kind==traits_info::Setter)
		{
			// The getters and setters are separate
			// traits. Check if we have already created a
			// node for this multiname with the
			// complementary accessor. If we have, update
			// the access attribute to "readwrite".
			pugi::xml_node node;
			auto existing=accessorNodes.find(t.name);
			if(existing==accessorNodes.end())
			{
				node=root.append_child("accessor");
				node.append_attribute("name").set_value(getSystemState()->getStringFromUniqueId(mname->name_s_id).raw_buf());
			}
			else
			{
				node=existing->second;
			}

			const char* access=NULL;
			pugi::xml_attribute oldAttr=node.attribute("access");
			tiny_string oldAccess=oldAttr.value();

			if(kind==traits_info::Getter && oldAccess=="")
				access="readonly";
			else if(kind==traits_info::Setter && oldAccess=="")
				access="writeonly";
			else if((kind==traits_info::Getter && oldAccess=="writeonly") || 
				(kind==traits_info::Setter && oldAccess=="readonly"))
				access="readwrite";

			node.remove_attribute("access");
			node.append_attribute("access").set_value(access);

			tiny_string type;
			method_info& method=context->methods[t.method];
			if(kind==traits_info::Getter)
			{
				const multiname* rtname=method.returnTypeName();
				type=rtname->qualifiedString(getSystemState());
			}
			else if(method.numArgs()>0) // setter
			{
				type=method.paramTypeName(0)->qualifiedString(getSystemState());
			}
			if(!type.empty())
			{
				node.remove_attribute("type");
				node.append_attribute("type").set_value(type.raw_buf());
			}

			node.remove_attribute("declaredBy");
			node.append_attribute("declaredBy").set_value(getQualifiedClassName().raw_buf());
			
			describeMetadata(node, t);
			accessorNodes[t.name]=node;
		}
	}
}

void Class_base::describeMetadata(pugi::xml_node& root, const traits_info& trait) const
{
	if((trait.kind&traits_info::Metadata) == 0)
		return;

	for(unsigned int i=0;i<trait.metadata_count;i++)
	{
		pugi::xml_node metadata_node=root.append_child("metadata");
		metadata_info& minfo = context->metadata[trait.metadata[i]];
		metadata_node.append_attribute("name").set_value(context->root->getSystemState()->getStringFromUniqueId(context->getString(minfo.name)).raw_buf());

		for(unsigned int j=0;j<minfo.item_count;++j)
		{
			pugi::xml_node arg_node=metadata_node.append_child("arg");
			arg_node.append_attribute("key").set_value(context->root->getSystemState()->getStringFromUniqueId(context->getString(minfo.items[j].key)).raw_buf());
			arg_node.append_attribute("value").set_value(context->root->getSystemState()->getStringFromUniqueId(context->getString(minfo.items[j].value)).raw_buf());
		}
	}
}

void Class_base::initializeProtectedNamespace(uint32_t nameId, const namespace_info& ns)
{
	Class_inherit* cur=dynamic_cast<Class_inherit*>(super.getPtr());
	nsNameAndKind* baseNs=NULL;
	while(cur)
	{
		if(cur->use_protected)
		{
			baseNs=&cur->protected_ns;
			break;
		}
		cur=dynamic_cast<Class_inherit*>(cur->super.getPtr());
	}
	if(baseNs==NULL)
		protected_ns=nsNameAndKind(getSystemState(),nameId,(NS_KIND)(int)ns.kind);
	else
		protected_ns=nsNameAndKind(getSystemState(),nameId,baseNs->nsId,(NS_KIND)(int)ns.kind);
}

variable* Class_base::findBorrowedSettable(const multiname& name, bool* has_getter)
{
	return ASObject::findSettableImpl(getSystemState(),borrowedVariables,name,has_getter);
}

variable* Class_base::findSettableInPrototype(const multiname& name)
{
	Prototype* proto = prototype.getPtr();
	while(proto)
	{
		variable *obj = proto->getObj()->findSettable(name);
		if (obj)
			return obj;

		proto = proto->prevPrototype.getPtr();
	}

	return NULL;
}

EARLY_BIND_STATUS Class_base::resolveMultinameStatically(const multiname& name) const
{
	if(findBorrowedGettable(name)!=NULL)
		return BINDED;
	else
		return NOT_BINDED;
}

ASQName::ASQName(Class_base* c):ASObject(c,T_QNAME),uri_is_null(false),uri(0),local_name(0)
{
}
void ASQName::setByXML(XML* node)
{
	uri_is_null=false;
	local_name = getSystemState()->getUniqueStringId(node->getName());
	uri=node->getNamespaceURI();
}

void ASQName::sinit(Class_base* c)
{
	CLASS_SETUP(c, ASObject, _constructor, CLASS_SEALED | CLASS_FINAL);
	c->setDeclaredMethodByQName("uri","",Class<IFunction>::getFunction(c->getSystemState(),_getURI),GETTER_METHOD,true);
	c->setDeclaredMethodByQName("localName","",Class<IFunction>::getFunction(c->getSystemState(),_getLocalName),GETTER_METHOD,true);
	c->prototype->setVariableByQName("toString","",Class<IFunction>::getFunction(c->getSystemState(),_toString),DYNAMIC_TRAIT);
}

ASFUNCTIONBODY(ASQName,_constructor)
{
	ASQName* th=static_cast<ASQName*>(obj);
	assert_and_throw(argslen<3);

	ASObject *nameval;
	ASObject *namespaceval;

	if(argslen==0)
	{
		th->local_name=BUILTIN_STRINGS::EMPTY;
		th->uri_is_null=false;
		th->uri=obj->getSystemState()->getUniqueStringId(getVm(obj->getSystemState())->getDefaultXMLNamespace());
		return NULL;
	}
	if(argslen==1)
	{
		nameval=args[0];
		namespaceval=NULL;
	}
	else if(argslen==2)
	{
		namespaceval=args[0];
		nameval=args[1];
	}

	// Set local_name
	if(nameval->getObjectType()==T_QNAME)
	{
		ASQName *q=static_cast<ASQName*>(nameval);
		th->local_name=q->local_name;
		if(!namespaceval)
		{
			th->uri_is_null=q->uri_is_null;
			th->uri=q->uri;
			return NULL;
		}
	}
	else if(nameval->getObjectType()==T_UNDEFINED)
		th->local_name=BUILTIN_STRINGS::EMPTY;
	else
		th->local_name=nameval->toStringId();

	// Set uri
	th->uri_is_null=false;
	if(!namespaceval || namespaceval->getObjectType()==T_UNDEFINED)
	{
		if(th->local_name==BUILTIN_STRINGS::STRING_WILDCARD)
		{
			th->uri_is_null=true;
			th->uri=BUILTIN_STRINGS::EMPTY;
		}
		else
		{
			th->uri=obj->getSystemState()->getUniqueStringId(getVm(obj->getSystemState())->getDefaultXMLNamespace());
		}
	}
	else if(namespaceval->getObjectType()==T_NULL)
	{
		th->uri_is_null=true;
		th->uri=BUILTIN_STRINGS::EMPTY;
	}
	else
	{
		if(namespaceval->getObjectType()==T_QNAME && 
		   !(static_cast<ASQName*>(namespaceval)->uri_is_null))
		{
			ASQName* q=static_cast<ASQName*>(namespaceval);
			th->uri=q->uri;
		}
		else
			th->uri=namespaceval->toStringId();
	}

	return NULL;
}
ASFUNCTIONBODY(ASQName,generator)
{
	ASQName* th=Class<ASQName>::getInstanceS(getSys());
	assert_and_throw(argslen<3);

	ASObject *nameval;
	ASObject *namespaceval;

	if(argslen==0)
	{
		th->local_name=BUILTIN_STRINGS::EMPTY;
		th->uri_is_null=false;
		th->uri=th->getSystemState()->getUniqueStringId(getVm(getSys())->getDefaultXMLNamespace());
		return th;
	}
	if(argslen==1)
	{
		nameval=args[0];
		namespaceval=NULL;
	}
	else if(argslen==2)
	{
		namespaceval=args[0];
		nameval=args[1];
	}

	// Set local_name
	if(nameval->getObjectType()==T_QNAME)
	{
		ASQName *q=static_cast<ASQName*>(nameval);
		th->local_name=q->local_name;
		if(!namespaceval)
		{
			th->uri_is_null=q->uri_is_null;
			th->uri=q->uri;
			return th;
		}
	}
	else if(nameval->getObjectType()==T_UNDEFINED)
		th->local_name=BUILTIN_STRINGS::EMPTY;
	else
		th->local_name=nameval->toStringId();

	// Set uri
	th->uri_is_null=false;
	if(!namespaceval || namespaceval->getObjectType()==T_UNDEFINED)
	{
		if(th->local_name==BUILTIN_STRINGS::STRING_WILDCARD)
		{
			th->uri_is_null=true;
			th->uri=BUILTIN_STRINGS::EMPTY;
		}
		else
		{
			th->uri=th->getSystemState()->getUniqueStringId(getVm(th->getSystemState())->getDefaultXMLNamespace());
		}
	}
	else if(namespaceval->getObjectType()==T_NULL)
	{
		th->uri_is_null=true;
		th->uri=BUILTIN_STRINGS::EMPTY;
	}
	else
	{
		if(namespaceval->getObjectType()==T_QNAME && 
		   !(static_cast<ASQName*>(namespaceval)->uri_is_null))
		{
			ASQName* q=static_cast<ASQName*>(namespaceval);
			th->uri=q->uri;
		}
		else
			th->uri=namespaceval->toStringId();
	}
	return th;
}

ASFUNCTIONBODY(ASQName,_getURI)
{
	ASQName* th=static_cast<ASQName*>(obj);
	if(th->uri_is_null)
		return obj->getSystemState()->getNullRef();
	else
		return abstract_s(obj->getSystemState(),obj->getSystemState()->getStringFromUniqueId(th->uri));
}

ASFUNCTIONBODY(ASQName,_getLocalName)
{
	ASQName* th=static_cast<ASQName*>(obj);
	return abstract_s(obj->getSystemState(),obj->getSystemState()->getStringFromUniqueId(th->local_name));
}

ASFUNCTIONBODY(ASQName,_toString)
{
	if(!obj->is<ASQName>())
		throw Class<TypeError>::getInstanceS(obj->getSystemState(),"QName.toString is not generic");
	ASQName* th=static_cast<ASQName*>(obj);
	return abstract_s(obj->getSystemState(),th->toString());
}

bool ASQName::isEqual(ASObject* o)
{
	if(o->getObjectType()==T_QNAME)
	{
		ASQName *q=static_cast<ASQName *>(o);
		return uri_is_null==q->uri_is_null && uri==q->uri && local_name==q->local_name;
	}

	return ASObject::isEqual(o);
}

tiny_string ASQName::toString()
{
	tiny_string s;
	if(uri_is_null)
		s = "*::";
	else if(uri!=BUILTIN_STRINGS::EMPTY)
		s = getSystemState()->getStringFromUniqueId(uri) + "::";

	return s + getSystemState()->getStringFromUniqueId(local_name);
}

uint32_t ASQName::nextNameIndex(uint32_t cur_index)
{
	assert_and_throw(implEnable);
	if(cur_index<2)
		return cur_index+1;
	else
	{
		//Fall back on object properties
		uint32_t ret=ASObject::nextNameIndex(cur_index-2);
		if(ret==0)
			return 0;
		else
			return ret+2;

	}
}

_R<ASObject> ASQName::nextName(uint32_t index)
{
	assert_and_throw(implEnable);
	switch(index)
	{
		case 1:
			return _MR(abstract_s(getSystemState(),"uri"));
		case 2:
			return _MR(abstract_s(getSystemState(),"localName"));
		default:
			return ASObject::nextName(index-2);
	}
}

_R<ASObject> ASQName::nextValue(uint32_t index)
{
	assert_and_throw(implEnable);
	switch(index)
	{
		case 1:
			if (uri_is_null)
				return _MR(getSystemState()->getNullRef());
			else
				return _MR(abstract_s(getSystemState(),getSystemState()->getStringFromUniqueId(this->uri)));
		case 2:
			return _MR(abstract_s(getSystemState(),getSystemState()->getStringFromUniqueId(this->local_name)));
		default:
			return ASObject::nextName(index-2);
	}
}

Namespace::Namespace(Class_base* c):ASObject(c,T_NAMESPACE),nskind(NAMESPACE),prefix_is_undefined(false)
{
}

Namespace::Namespace(Class_base* c, uint32_t _uri, uint32_t _prefix, NS_KIND _nskind)
  : ASObject(c,T_NAMESPACE),nskind(_nskind),prefix_is_undefined(false),uri(_uri),prefix(_prefix)
{
}

void Namespace::sinit(Class_base* c)
{
	CLASS_SETUP(c, ASObject, _constructor, CLASS_SEALED | CLASS_FINAL);
	//c->setDeclaredMethodByQName("uri","",Class<IFunction>::getFunction(c->getSystemState(),_setURI),SETTER_METHOD,true);
	c->setDeclaredMethodByQName("uri","",Class<IFunction>::getFunction(c->getSystemState(),_getURI),GETTER_METHOD,true);
	//c->setDeclaredMethodByQName("prefix","",Class<IFunction>::getFunction(c->getSystemState(),_setPrefix),SETTER_METHOD,true);
	c->setDeclaredMethodByQName("prefix","",Class<IFunction>::getFunction(c->getSystemState(),_getPrefix),GETTER_METHOD,true);
	c->setDeclaredMethodByQName("valueOf",AS3,Class<IFunction>::getFunction(c->getSystemState(),_valueOf),NORMAL_METHOD,true);
	c->prototype->setVariableByQName("toString","",Class<IFunction>::getFunction(c->getSystemState(),_toString),DYNAMIC_TRAIT);
	c->prototype->setVariableByQName("valueOf","",Class<IFunction>::getFunction(c->getSystemState(),_ECMA_valueOf),DYNAMIC_TRAIT);
}

void Namespace::buildTraits(ASObject* o)
{
}

ASFUNCTIONBODY(Namespace,_constructor)
{
	ASObject *urival;
	ASObject *prefixval;
	Namespace* th=static_cast<Namespace*>(obj);
	assert_and_throw(argslen<3);

	if (argslen == 0)
	{
		//Return before resetting the value to preserve those eventually set by the C++ constructor
		return NULL;
	}
	else if (argslen == 1)
	{
		urival = args[0];
		prefixval = NULL;
	}
	else
	{
		prefixval = args[0];
		urival = args[1];
	}
	th->prefix_is_undefined=false;
	th->prefix = BUILTIN_STRINGS::EMPTY;
	th->uri = BUILTIN_STRINGS::EMPTY;
;

	if(!prefixval)
	{
		if(urival->getObjectType()==T_NAMESPACE)
		{
			Namespace* n=static_cast<Namespace*>(urival);
			th->uri=n->uri;
			th->prefix=n->prefix;
			th->prefix_is_undefined=n->prefix_is_undefined;
		}
		else if(urival->getObjectType()==T_QNAME && 
		   !(static_cast<ASQName*>(urival)->uri_is_null))
		{
			ASQName* q=static_cast<ASQName*>(urival);
			th->uri=q->uri;
		}
		else
		{
			th->uri=urival->toStringId();
			if(th->uri!=BUILTIN_STRINGS::EMPTY)
			{
				th->prefix_is_undefined=true;
				th->prefix=BUILTIN_STRINGS::EMPTY;
			}
		}
	}
	else // has both urival and prefixval
	{
		if(urival->getObjectType()==T_QNAME &&
		   !(static_cast<ASQName*>(urival)->uri_is_null))
		{
			ASQName* q=static_cast<ASQName*>(urival);
			th->uri=q->uri;
		}
		else
		{
			th->uri=urival->toStringId();
		}

		if(th->uri==BUILTIN_STRINGS::EMPTY)
		{
			if(prefixval->getObjectType()==T_UNDEFINED ||
			   prefixval->toString()=="")
				th->prefix=BUILTIN_STRINGS::EMPTY;
			else
				throw Class<TypeError>::getInstanceS(obj->getSystemState(),"Namespace prefix for empty uri not allowed");
		}
		else if(prefixval->getObjectType()==T_UNDEFINED ||
			!isXMLName(prefixval))
		{
			th->prefix_is_undefined=true;
			th->prefix=BUILTIN_STRINGS::EMPTY;
		}
		else
		{
			th->prefix=prefixval->toStringId();
		}
	}

	return NULL;
}
ASFUNCTIONBODY(Namespace,generator)
{
	Namespace* th=Class<Namespace>::getInstanceS(getSys());
	ASObject *urival;
	ASObject *prefixval;
	assert_and_throw(argslen<3);

	if (argslen == 0)
	{
		th->prefix_is_undefined=false;
		th->prefix = BUILTIN_STRINGS::EMPTY;
		th->uri = BUILTIN_STRINGS::EMPTY;
		return th;
	}
	else if (argslen == 1)
	{
		urival = args[0];
		prefixval = NULL;
	}
	else
	{
		prefixval = args[0];
		urival = args[1];
	}
	th->prefix_is_undefined=false;
	th->prefix = BUILTIN_STRINGS::EMPTY;
	th->uri = BUILTIN_STRINGS::EMPTY;

	if(!prefixval)
	{
		if(urival->getObjectType()==T_NAMESPACE)
		{
			Namespace* n=static_cast<Namespace*>(urival);
			th->uri=n->uri;
			th->prefix=n->prefix;
			th->prefix_is_undefined=n->prefix_is_undefined;
		}
		else if(urival->getObjectType()==T_QNAME && 
		   !(static_cast<ASQName*>(urival)->uri_is_null))
		{
			ASQName* q=static_cast<ASQName*>(urival);
			th->uri=q->uri;
		}
		else
		{
			th->uri=urival->toStringId();
			if(th->uri!=BUILTIN_STRINGS::EMPTY)
			{
				th->prefix_is_undefined=true;
				th->prefix=BUILTIN_STRINGS::EMPTY;
			}
		}
	}
	else // has both urival and prefixval
	{
		if(urival->getObjectType()==T_QNAME &&
		   !(static_cast<ASQName*>(urival)->uri_is_null))
		{
			ASQName* q=static_cast<ASQName*>(urival);
			th->uri=q->uri;
		}
		else
		{
			th->uri=urival->toStringId();
		}

		if(th->uri==BUILTIN_STRINGS::EMPTY)
		{
			if(prefixval->getObjectType()==T_UNDEFINED ||
			   prefixval->toString()=="")
				th->prefix=BUILTIN_STRINGS::EMPTY;
			else
				throw Class<TypeError>::getInstanceS(getSys(),"Namespace prefix for empty uri not allowed");
		}
		else if(prefixval->getObjectType()==T_UNDEFINED ||
			!isXMLName(prefixval))
		{
			th->prefix_is_undefined=true;
			th->prefix=BUILTIN_STRINGS::EMPTY;
		}
		else
		{
			th->prefix=prefixval->toStringId();
		}
	}
	return th;
}
/*
ASFUNCTIONBODY(Namespace,_setURI)
{
	Namespace* th=static_cast<Namespace*>(obj);
	th->uri=args[0]->toString();
	return NULL;
}
*/
ASFUNCTIONBODY(Namespace,_getURI)
{
	Namespace* th=static_cast<Namespace*>(obj);
	return abstract_s(obj->getSystemState(),th->uri);
}
/*
ASFUNCTIONBODY(Namespace,_setPrefix)
{
	Namespace* th=static_cast<Namespace*>(obj);
	if(args[0]->getObjectType()==T_UNDEFINED)
	{
		th->prefix_is_undefined=true;
		th->prefix="";
	}
	else
	{
		th->prefix_is_undefined=false;
		th->prefix=args[0]->toString();
	}
	return NULL;
}
*/
ASFUNCTIONBODY(Namespace,_getPrefix)
{
	Namespace* th=static_cast<Namespace*>(obj);
	if(th->prefix_is_undefined)
		return obj->getSystemState()->getUndefinedRef();
	else
		return abstract_s(obj->getSystemState(),th->prefix);
}

ASFUNCTIONBODY(Namespace,_toString)
{
	if(!obj->is<Namespace>())
		throw Class<TypeError>::getInstanceS(obj->getSystemState(),"Namespace.toString is not generic");
	Namespace* th=static_cast<Namespace*>(obj);
	return abstract_s(obj->getSystemState(),th->uri);
}

ASFUNCTIONBODY(Namespace,_valueOf)
{
	return abstract_s(obj->getSystemState(),obj->as<Namespace>()->uri);
}

ASFUNCTIONBODY(Namespace,_ECMA_valueOf)
{
	if(!obj->is<Namespace>())
		throw Class<TypeError>::getInstanceS(obj->getSystemState(),"Namespace.valueOf is not generic");
	Namespace* th=static_cast<Namespace*>(obj);
	return abstract_s(obj->getSystemState(),th->uri);
}

bool Namespace::isEqual(ASObject* o)
{
	if(o->getObjectType()==T_NAMESPACE)
	{
		Namespace *n=static_cast<Namespace *>(o);
		return uri==n->uri;
	}

	return ASObject::isEqual(o);
}

uint32_t Namespace::nextNameIndex(uint32_t cur_index)
{
	assert_and_throw(implEnable);
	if(cur_index<2)
		return cur_index+1;
	else
	{
		//Fall back on object properties
		uint32_t ret=ASObject::nextNameIndex(cur_index-2);
		if(ret==0)
			return 0;
		else
			return ret+2;

	}
}

_R<ASObject> Namespace::nextName(uint32_t index)
{
	assert_and_throw(implEnable);
	switch(index)
	{
		case 1:
			return _MR(abstract_s(getSystemState(),"uri"));
		case 2:
			return _MR(abstract_s(getSystemState(),"prefix"));
		default:
			return ASObject::nextName(index-2);
	}
}

_R<ASObject> Namespace::nextValue(uint32_t index)
{
	assert_and_throw(implEnable);
	switch(index)
	{
		case 1:
			return _MR(abstract_s(getSystemState(),this->uri));
		case 2:
			if(prefix_is_undefined)
				return _MR(getSystemState()->getUndefinedRef());
			else
				return _MR(abstract_s(getSystemState(),this->prefix));
		default:
			return ASObject::nextName(index-2);
	}
}

ASObject* ASNop(ASObject* obj, ASObject* const* args, const unsigned int argslen)
{
	return obj->getSystemState()->getUndefinedRef();
}

IFunction* Class<IFunction>::getNopFunction()
{
	IFunction* ret=new (this->memoryAccount) Function(this, ASNop);
	//Similarly to newFunction, we must create a prototype object
	ret->prototype = _MR(new_asobject(ret->getSystemState()));
	return ret;
}

ASObject* Class<IFunction>::getInstance(bool construct, ASObject* const* args, const unsigned int argslen, Class_base* realClass)
{
	if (argslen > 0)
		throwError<EvalError>(kFunctionConstructorError);
	ASObject* ret = getNopFunction();
	if (construct)
		ret->setConstructIndicator();
	return ret;
}

Class<IFunction>* Class<IFunction>::getClass(SystemState* sys)
{
	uint32_t classId=ClassName<IFunction>::id;
	Class<IFunction>* ret=NULL;
	SystemState* s = sys ? sys : getSys();
	Class_base** retAddr=&s->builtinClasses[classId];
	if(*retAddr==NULL)
	{
		//Create the class
		ret=new (s->unaccountedMemory) Class<IFunction>(s->unaccountedMemory);
		ret->setSystemState(s);
		//This function is called from Class<ASObject>::getRef(),
		//so the Class<ASObject> we obtain will not have any
		//declared methods yet! Therefore, set super will not copy
		//up any borrowed traits from there. We do that by ourself.
		ret->setSuper(Class<ASObject>::getRef(s));
		//The prototype for Function seems to be a function object. Use the special FunctionPrototype
		ret->prototype = _MNR(new_functionPrototype(ret, ret->super->prototype));
		ret->incRef();
		ret->prototype->getObj()->setVariableByQName("constructor","",ret,DECLARED_TRAIT);
		ret->prototype->getObj()->setConstructIndicator();
		ret->incRef();
		*retAddr = ret;

		//we cannot use sinit, as we need to setup 'this_class' before calling
		//addPrototypeGetter and setDeclaredMethodByQName.
		//Thus we make sure that everything is in order when getFunction() below is called
		ret->addPrototypeGetter();
		IFunction::sinit(ret);
		ret->constructorprop = _NR<ObjectConstructor>(new_objectConstructor(ret,ret->length));
		ret->constructorprop->incRef();

		ret->addConstructorGetter();

		ret->setDeclaredMethodByQName("prototype","",Class<IFunction>::getFunction(ret->getSystemState(),IFunction::_getter_prototype),GETTER_METHOD,true);
		ret->setDeclaredMethodByQName("prototype","",Class<IFunction>::getFunction(ret->getSystemState(),IFunction::_setter_prototype),SETTER_METHOD,true);
	}
	else
		ret=static_cast<Class<IFunction>*>(*retAddr);

	return ret;
}


Global::Global(Class_base* cb, ABCContext* c, int s):ASObject(cb,T_OBJECT,SUBTYPE_GLOBAL),scriptId(s),context(c)
{
}

void Global::sinit(Class_base* c)
{
	c->setSuper(Class<ASObject>::getRef(c->getSystemState()));
}

_NR<ASObject> Global::getVariableByMultinameOpportunistic(const multiname& name)
{
	_NR<ASObject> ret = ASObject::getVariableByMultiname(name, NONE);
	//Do not attempt to define the variable now in any case
	return ret;
}

_NR<ASObject> Global::getVariableByMultiname(const multiname& name, GET_VARIABLE_OPTION opt)
{
	_NR<ASObject> ret = ASObject::getVariableByMultiname(name, opt);
	/*
	 * All properties are registered by now, even if the script init has
	 * not been run. Thus if ret == NULL, we don't have to run the script init.
	 */
	if(ret.isNull() || !context || context->hasRunScriptInit[scriptId])
		return ret;
	LOG_CALL("Access to " << name << ", running script init");
	context->runScriptInit(scriptId, this);
	return ASObject::getVariableByMultiname(name, opt);
}

void Global::registerBuiltin(const char* name, const char* ns, _R<ASObject> o)
{
	o->incRef();
	setVariableByQName(name,nsNameAndKind(getSystemState(),ns,NAMESPACE),o.getPtr(),DECLARED_TRAIT);
	//setVariableByQName(name,nsNameAndKind(ns,PACKAGE_NAMESPACE),o.getPtr(),DECLARED_TRAIT);
}

ASFUNCTIONBODY(lightspark,eval)
{
    // eval is not allowed in AS3, but an exception should be thrown
	throw Class<EvalError>::getInstanceS(getSys(),"EvalError");
}

ASFUNCTIONBODY(lightspark,parseInt)
{
	tiny_string str;
	int radix;
	ARG_UNPACK (str, "") (radix, 0);

	if(radix != 0 && (radix < 2 || radix > 36))
		return abstract_d(obj->getSystemState(),numeric_limits<double>::quiet_NaN());

	const char* cur=str.raw_buf();
	int64_t ret;
	bool valid=Integer::fromStringFlashCompatible(cur,ret,radix);

	if(valid==false)
		return abstract_d(obj->getSystemState(),numeric_limits<double>::quiet_NaN());
	if(ret==INT64_MAX)
		return abstract_d(obj->getSystemState(),numeric_limits<double>::infinity());
	if(ret==INT64_MIN)
		return abstract_d(obj->getSystemState(),-numeric_limits<double>::infinity());
	return abstract_d(obj->getSystemState(),ret);
}

ASFUNCTIONBODY(lightspark,parseFloat)
{
	tiny_string str;
	ARG_UNPACK (str, "");

	return abstract_d(obj->getSystemState(),parseNumber(str));
}
number_t lightspark::parseNumber(const tiny_string str)
{
	const char *p;
	char *end;

	// parsing of hex numbers is not allowed
	char* p1 = str.strchr('x');
	if (p1) *p1='y';
	p1 = str.strchr('X');
	if (p1) *p1='Y';

	p=str.raw_buf();
	double d=strtod(p, &end);

	if (end==p)
		return numeric_limits<double>::quiet_NaN();

	return d;
}

ASFUNCTIONBODY(lightspark,isNaN)
{
	if(argslen==0)
		return abstract_b(getSys(),true);
	else if(args[0]->getObjectType()==T_UNDEFINED)
		return abstract_b(args[0]->getSystemState(),true);
	else if(args[0]->getObjectType()==T_INTEGER)
		return abstract_b(args[0]->getSystemState(),false);
	else if(args[0]->getObjectType()==T_BOOLEAN)
		return abstract_b(args[0]->getSystemState(),false);
	else if(args[0]->getObjectType()==T_NULL)
		return abstract_b(args[0]->getSystemState(),false); // because Number(null) == 0
	else
		return abstract_b(args[0]->getSystemState(),std::isnan(args[0]->toNumber()));
}

ASFUNCTIONBODY(lightspark,isFinite)
{
	if(argslen==0)
		return abstract_b(getSys(),false);
	else
		return abstract_b(args[0]->getSystemState(),isfinite(args[0]->toNumber()));
}

ASFUNCTIONBODY(lightspark,encodeURI)
{
	tiny_string str;
	ARG_UNPACK (str, "undefined");
	return abstract_s(getSys(),URLInfo::encode(str, URLInfo::ENCODE_URI));
}

ASFUNCTIONBODY(lightspark,decodeURI)
{
	tiny_string str;
	ARG_UNPACK (str, "undefined");
	return abstract_s(getSys(),URLInfo::decode(str, URLInfo::ENCODE_URI));
}

ASFUNCTIONBODY(lightspark,encodeURIComponent)
{
	tiny_string str;
	ARG_UNPACK (str, "undefined");
	return abstract_s(getSys(),URLInfo::encode(str, URLInfo::ENCODE_URICOMPONENT));
}

ASFUNCTIONBODY(lightspark,decodeURIComponent)
{
	tiny_string str;
	ARG_UNPACK (str, "undefined");
	return abstract_s(getSys(),URLInfo::decode(str, URLInfo::ENCODE_URICOMPONENT));
}

ASFUNCTIONBODY(lightspark,escape)
{
	tiny_string str;
	ARG_UNPACK (str, "undefined");
	if (argslen > 0 && args[0]->is<Undefined>())
		return abstract_s(getSys(),"null");
	return abstract_s(getSys(),URLInfo::encode(str, URLInfo::ENCODE_ESCAPE));
}

ASFUNCTIONBODY(lightspark,unescape)
{
	tiny_string str;
	ARG_UNPACK (str, "undefined");
	if (argslen > 0 && args[0]->is<Undefined>())
		return abstract_s(getSys(),"null");
	return abstract_s(getSys(),URLInfo::decode(str, URLInfo::ENCODE_ESCAPE));
}

ASFUNCTIONBODY(lightspark,print)
{
	if(args[0]->getObjectType() == T_STRING)
	{
		ASString* str = static_cast<ASString*>(args[0]);
		Log::print(str->getData());
	}
	else
		Log::print(args[0]->toString());
	return NULL;
}

ASFUNCTIONBODY(lightspark,trace)
{
	stringstream s;
	for(uint32_t i = 0; i< argslen;i++)
	{
		if(i > 0)
			s << " ";

		if(args[i]->getObjectType() == T_STRING)
		{
			ASString* str = static_cast<ASString*>(args[i]);
			s << str->getData();
		}
		else
			s << args[i]->toString();
	}
	Log::print(s.str());
	return NULL;
}

bool lightspark::isXMLName(ASObject *obj)
{
	tiny_string name;

	if(obj->getObjectType()==lightspark::T_QNAME)
	{
		ASQName *q=static_cast<ASQName*>(obj);
		name=obj->getSystemState()->getStringFromUniqueId(q->getLocalName());
	}
	else if(obj->getObjectType()==lightspark::T_UNDEFINED ||
		obj->getObjectType()==lightspark::T_NULL)
		name="";
	else
		name=obj->toString();

	if(name.empty())
		return false;

	// http://www.w3.org/TR/2006/REC-xml-names-20060816/#NT-NCName
	// Note: Flash follows the second edition (20060816) of the
	// standard. The character range definitions were changed in
	// the newer edition.
	#define NC_START_CHAR(x) \
	  ((0x0041 <= x && x <= 0x005A) || x == 0x5F || \
	  (0x0061 <= x && x <= 0x007A) || (0x00C0 <= x && x <= 0x00D6) || \
	  (0x00D8 <= x && x <= 0x00F6) || (0x00F8 <= x && x <= 0x00FF) || \
	  (0x0100 <= x && x <= 0x0131) || (0x0134 <= x && x <= 0x013E) || \
	  (0x0141 <= x && x <= 0x0148) || (0x014A <= x && x <= 0x017E) || \
	  (0x0180 <= x && x <= 0x01C3) || (0x01CD <= x && x <= 0x01F0) || \
	  (0x01F4 <= x && x <= 0x01F5) || (0x01FA <= x && x <= 0x0217) || \
	  (0x0250 <= x && x <= 0x02A8) || (0x02BB <= x && x <= 0x02C1) || \
	  x == 0x0386 || (0x0388 <= x && x <= 0x038A) || x == 0x038C || \
	  (0x038E <= x && x <= 0x03A1) || (0x03A3 <= x && x <= 0x03CE) || \
	  (0x03D0 <= x && x <= 0x03D6) || x == 0x03DA || x == 0x03DC || \
	  x == 0x03DE || x == 0x03E0 || (0x03E2 <= x && x <= 0x03F3) || \
	  (0x0401 <= x && x <= 0x040C) || (0x040E <= x && x <= 0x044F) || \
	  (0x0451 <= x && x <= 0x045C) || (0x045E <= x && x <= 0x0481) || \
	  (0x0490 <= x && x <= 0x04C4) || (0x04C7 <= x && x <= 0x04C8) || \
	  (0x04CB <= x && x <= 0x04CC) || (0x04D0 <= x && x <= 0x04EB) || \
	  (0x04EE <= x && x <= 0x04F5) || (0x04F8 <= x && x <= 0x04F9) || \
	  (0x0531 <= x && x <= 0x0556) || x == 0x0559 || \
	  (0x0561 <= x && x <= 0x0586) || (0x05D0 <= x && x <= 0x05EA) || \
	  (0x05F0 <= x && x <= 0x05F2) || (0x0621 <= x && x <= 0x063A) || \
	  (0x0641 <= x && x <= 0x064A) || (0x0671 <= x && x <= 0x06B7) || \
	  (0x06BA <= x && x <= 0x06BE) || (0x06C0 <= x && x <= 0x06CE) || \
	  (0x06D0 <= x && x <= 0x06D3) || x == 0x06D5 || \
	  (0x06E5 <= x && x <= 0x06E6) || (0x0905 <= x && x <= 0x0939) || \
	  x == 0x093D || (0x0958 <= x && x <= 0x0961) || \
	  (0x0985 <= x && x <= 0x098C) || (0x098F <= x && x <= 0x0990) || \
	  (0x0993 <= x && x <= 0x09A8) || (0x09AA <= x && x <= 0x09B0) || \
	  x == 0x09B2 || (0x09B6 <= x && x <= 0x09B9) || \
	  (0x09DC <= x && x <= 0x09DD) || (0x09DF <= x && x <= 0x09E1) || \
	  (0x09F0 <= x && x <= 0x09F1) || (0x0A05 <= x && x <= 0x0A0A) || \
	  (0x0A0F <= x && x <= 0x0A10) || (0x0A13 <= x && x <= 0x0A28) || \
	  (0x0A2A <= x && x <= 0x0A30) || (0x0A32 <= x && x <= 0x0A33) || \
	  (0x0A35 <= x && x <= 0x0A36) || (0x0A38 <= x && x <= 0x0A39) || \
	  (0x0A59 <= x && x <= 0x0A5C) || x == 0x0A5E || \
	  (0x0A72 <= x && x <= 0x0A74) || (0x0A85 <= x && x <= 0x0A8B) || \
	  x == 0x0A8D || (0x0A8F <= x && x <= 0x0A91) || \
	  (0x0A93 <= x && x <= 0x0AA8) || (0x0AAA <= x && x <= 0x0AB0) || \
	  (0x0AB2 <= x && x <= 0x0AB3) || (0x0AB5 <= x && x <= 0x0AB9) || \
	  x == 0x0ABD || x == 0x0AE0 || (0x0B05 <= x && x <= 0x0B0C) || \
	  (0x0B0F <= x && x <= 0x0B10) || (0x0B13 <= x && x <= 0x0B28) || \
	  (0x0B2A <= x && x <= 0x0B30) || (0x0B32 <= x && x <= 0x0B33) || \
	  (0x0B36 <= x && x <= 0x0B39) || x == 0x0B3D || \
	  (0x0B5C <= x && x <= 0x0B5D) || (0x0B5F <= x && x <= 0x0B61) || \
	  (0x0B85 <= x && x <= 0x0B8A) || (0x0B8E <= x && x <= 0x0B90) || \
	  (0x0B92 <= x && x <= 0x0B95) || (0x0B99 <= x && x <= 0x0B9A) || \
	  x == 0x0B9C || (0x0B9E <= x && x <= 0x0B9F) || \
	  (0x0BA3 <= x && x <= 0x0BA4) || (0x0BA8 <= x && x <= 0x0BAA) || \
	  (0x0BAE <= x && x <= 0x0BB5) || (0x0BB7 <= x && x <= 0x0BB9) || \
	  (0x0C05 <= x && x <= 0x0C0C) || (0x0C0E <= x && x <= 0x0C10) || \
	  (0x0C12 <= x && x <= 0x0C28) || (0x0C2A <= x && x <= 0x0C33) || \
	  (0x0C35 <= x && x <= 0x0C39) || (0x0C60 <= x && x <= 0x0C61) || \
	  (0x0C85 <= x && x <= 0x0C8C) || (0x0C8E <= x && x <= 0x0C90) || \
	  (0x0C92 <= x && x <= 0x0CA8) || (0x0CAA <= x && x <= 0x0CB3) || \
	  (0x0CB5 <= x && x <= 0x0CB9) || x == 0x0CDE || \
	  (0x0CE0 <= x && x <= 0x0CE1) || (0x0D05 <= x && x <= 0x0D0C) || \
	  (0x0D0E <= x && x <= 0x0D10) || (0x0D12 <= x && x <= 0x0D28) || \
	  (0x0D2A <= x && x <= 0x0D39) || (0x0D60 <= x && x <= 0x0D61) || \
	  (0x0E01 <= x && x <= 0x0E2E) || x == 0x0E30 || \
	  (0x0E32 <= x && x <= 0x0E33) || (0x0E40 <= x && x <= 0x0E45) || \
	  (0x0E81 <= x && x <= 0x0E82) || x == 0x0E84 || \
	  (0x0E87 <= x && x <= 0x0E88) || x == 0x0E8A || x == 0x0E8D || \
	  (0x0E94 <= x && x <= 0x0E97) || (0x0E99 <= x && x <= 0x0E9F) || \
	  (0x0EA1 <= x && x <= 0x0EA3) || x == 0x0EA5 || x == 0x0EA7 || \
	  (0x0EAA <= x && x <= 0x0EAB) || (0x0EAD <= x && x <= 0x0EAE) || \
	  x == 0x0EB0 || (0x0EB2 <= x && x <= 0x0EB3) || x == 0x0EBD || \
	  (0x0EC0 <= x && x <= 0x0EC4) || (0x0F40 <= x && x <= 0x0F47) || \
	  (0x0F49 <= x && x <= 0x0F69) || (0x10A0 <= x && x <= 0x10C5) || \
	  (0x10D0 <= x && x <= 0x10F6) || x == 0x1100 || \
	  (0x1102 <= x && x <= 0x1103) || (0x1105 <= x && x <= 0x1107) || \
	  x == 0x1109 || (0x110B <= x && x <= 0x110C) || \
	  (0x110E <= x && x <= 0x1112) || x == 0x113C || x == 0x113E || \
	  x == 0x1140 || x == 0x114C || x == 0x114E || x == 0x1150 || \
	  (0x1154 <= x && x <= 0x1155) || x == 0x1159 || \
	  (0x115F <= x && x <= 0x1161) || x == 0x1163 || x == 0x1165 || \
	  x == 0x1167 || x == 0x1169 || (0x116D <= x && x <= 0x116E) || \
	  (0x1172 <= x && x <= 0x1173) || x == 0x1175 || x == 0x119E || \
	  x == 0x11A8 || x == 0x11AB || (0x11AE <= x && x <= 0x11AF) || \
	  (0x11B7 <= x && x <= 0x11B8) || x == 0x11BA || \
	  (0x11BC <= x && x <= 0x11C2) || x == 0x11EB || x == 0x11F0 || \
	  x == 0x11F9 || (0x1E00 <= x && x <= 0x1E9B) || \
	  (0x1EA0 <= x && x <= 0x1EF9) || (0x1F00 <= x && x <= 0x1F15) || \
	  (0x1F18 <= x && x <= 0x1F1D) || (0x1F20 <= x && x <= 0x1F45) || \
	  (0x1F48 <= x && x <= 0x1F4D) || (0x1F50 <= x && x <= 0x1F57) || \
	  x == 0x1F59 || x == 0x1F5B || x == 0x1F5D || \
	  (0x1F5F <= x && x <= 0x1F7D) || (0x1F80 <= x && x <= 0x1FB4) || \
	  (0x1FB6 <= x && x <= 0x1FBC) || x == 0x1FBE || \
	  (0x1FC2 <= x && x <= 0x1FC4) || (0x1FC6 <= x && x <= 0x1FCC) || \
	  (0x1FD0 <= x && x <= 0x1FD3) || (0x1FD6 <= x && x <= 0x1FDB) || \
	  (0x1FE0 <= x && x <= 0x1FEC) || (0x1FF2 <= x && x <= 0x1FF4) || \
	  (0x1FF6 <= x && x <= 0x1FFC) || x == 0x2126 || \
	  (0x212A <= x && x <= 0x212B) || x == 0x212E || \
	  (0x2180 <= x && x <= 0x2182) || (0x3041 <= x && x <= 0x3094) || \
	  (0x30A1 <= x && x <= 0x30FA) || (0x3105 <= x && x <= 0x312C) || \
	  (0xAC00 <= x && x <= 0xD7A3) || (0x4E00 <= x && x <= 0x9FA5) || \
	  x == 0x3007 || (0x3021 <= x && x <= 0x3029))
	#define NC_CHAR(x) \
	  (NC_START_CHAR(x) || x == 0x2E || x == 0x2D || x == 0x5F || \
	  (0x0030 <= x && x <= 0x0039) || (0x0660 <= x && x <= 0x0669) || \
	  (0x06F0 <= x && x <= 0x06F9) || (0x0966 <= x && x <= 0x096F) || \
	  (0x09E6 <= x && x <= 0x09EF) || (0x0A66 <= x && x <= 0x0A6F) || \
	  (0x0AE6 <= x && x <= 0x0AEF) || (0x0B66 <= x && x <= 0x0B6F) || \
	  (0x0BE7 <= x && x <= 0x0BEF) || (0x0C66 <= x && x <= 0x0C6F) || \
	  (0x0CE6 <= x && x <= 0x0CEF) || (0x0D66 <= x && x <= 0x0D6F) || \
	  (0x0E50 <= x && x <= 0x0E59) || (0x0ED0 <= x && x <= 0x0ED9) || \
	  (0x0F20 <= x && x <= 0x0F29) || (0x0300 <= x && x <= 0x0345) || \
	  (0x0360 <= x && x <= 0x0361) || (0x0483 <= x && x <= 0x0486) || \
	  (0x0591 <= x && x <= 0x05A1) || (0x05A3 <= x && x <= 0x05B9) || \
	  (0x05BB <= x && x <= 0x05BD) || x == 0x05BF || \
	  (0x05C1 <= x && x <= 0x05C2) || x == 0x05C4 || \
	  (0x064B <= x && x <= 0x0652) || x == 0x0670 || \
	  (0x06D6 <= x && x <= 0x06DC) || (0x06DD <= x && x <= 0x06DF) || \
	  (0x06E0 <= x && x <= 0x06E4) || (0x06E7 <= x && x <= 0x06E8) || \
	  (0x06EA <= x && x <= 0x06ED) || (0x0901 <= x && x <= 0x0903) || \
	  x == 0x093C || (0x093E <= x && x <= 0x094C) || x == 0x094D || \
	  (0x0951 <= x && x <= 0x0954) || (0x0962 <= x && x <= 0x0963) || \
	  (0x0981 <= x && x <= 0x0983) || x == 0x09BC || x == 0x09BE || \
	  x == 0x09BF || (0x09C0 <= x && x <= 0x09C4) || \
	  (0x09C7 <= x && x <= 0x09C8) || (0x09CB <= x && x <= 0x09CD) || \
	  x == 0x09D7 || (0x09E2 <= x && x <= 0x09E3) || x == 0x0A02 || \
	  x == 0x0A3C || x == 0x0A3E || x == 0x0A3F || \
	  (0x0A40 <= x && x <= 0x0A42) || (0x0A47 <= x && x <= 0x0A48) || \
	  (0x0A4B <= x && x <= 0x0A4D) || (0x0A70 <= x && x <= 0x0A71) || \
	  (0x0A81 <= x && x <= 0x0A83) || x == 0x0ABC || \
	  (0x0ABE <= x && x <= 0x0AC5) || (0x0AC7 <= x && x <= 0x0AC9) || \
	  (0x0ACB <= x && x <= 0x0ACD) || (0x0B01 <= x && x <= 0x0B03) || \
	  x == 0x0B3C || (0x0B3E <= x && x <= 0x0B43) || \
	  (0x0B47 <= x && x <= 0x0B48) || (0x0B4B <= x && x <= 0x0B4D) || \
	  (0x0B56 <= x && x <= 0x0B57) || (0x0B82 <= x && x <= 0x0B83) || \
	  (0x0BBE <= x && x <= 0x0BC2) || (0x0BC6 <= x && x <= 0x0BC8) || \
	  (0x0BCA <= x && x <= 0x0BCD) || x == 0x0BD7 || \
	  (0x0C01 <= x && x <= 0x0C03) || (0x0C3E <= x && x <= 0x0C44) || \
	  (0x0C46 <= x && x <= 0x0C48) || (0x0C4A <= x && x <= 0x0C4D) || \
	  (0x0C55 <= x && x <= 0x0C56) || (0x0C82 <= x && x <= 0x0C83) || \
	  (0x0CBE <= x && x <= 0x0CC4) || (0x0CC6 <= x && x <= 0x0CC8) || \
	  (0x0CCA <= x && x <= 0x0CCD) || (0x0CD5 <= x && x <= 0x0CD6) || \
	  (0x0D02 <= x && x <= 0x0D03) || (0x0D3E <= x && x <= 0x0D43) || \
	  (0x0D46 <= x && x <= 0x0D48) || (0x0D4A <= x && x <= 0x0D4D) || \
	  x == 0x0D57 || x == 0x0E31 || (0x0E34 <= x && x <= 0x0E3A) || \
	  (0x0E47 <= x && x <= 0x0E4E) || x == 0x0EB1 || \
	  (0x0EB4 <= x && x <= 0x0EB9) || (0x0EBB <= x && x <= 0x0EBC) || \
	  (0x0EC8 <= x && x <= 0x0ECD) || (0x0F18 <= x && x <= 0x0F19) || \
	  x == 0x0F35 || x == 0x0F37 || x == 0x0F39 || x == 0x0F3E || \
	  x == 0x0F3F || (0x0F71 <= x && x <= 0x0F84) || \
	  (0x0F86 <= x && x <= 0x0F8B) || (0x0F90 <= x && x <= 0x0F95) || \
	  x == 0x0F97 || (0x0F99 <= x && x <= 0x0FAD) || \
	  (0x0FB1 <= x && x <= 0x0FB7) || x == 0x0FB9 || \
	  (0x20D0 <= x && x <= 0x20DC) || x == 0x20E1 || \
	  (0x302A <= x && x <= 0x302F) || x == 0x3099 || x == 0x309A || \
	  x == 0x00B7 || x == 0x02D0 || x == 0x02D1 || x == 0x0387 || \
	  x == 0x0640 || x == 0x0E46 || x == 0x0EC6 || x == 0x3005 || \
	  (0x3031 <= x && x <= 0x3035) || (0x309D <= x && x <= 0x309E) || \
	  (0x30FC <= x && x <= 0x30FE))

	auto it=name.begin();
	if(!NC_START_CHAR(*it))
		return false;
	++it;

	for(;it!=name.end(); ++it)
	{
		if(!(NC_CHAR(*it)))
			return false;
	}

	#undef NC_CHAR
	#undef NC_START_CHAR

	return true;
}

ASFUNCTIONBODY(lightspark,_isXMLName)
{
	assert_and_throw(argslen <= 1);
	if(argslen==0)
		return abstract_b(getSys(),false);

	return abstract_b(args[0]->getSystemState(),isXMLName(args[0]));
}

ObjectPrototype::ObjectPrototype(Class_base* c) : ASObject(c)
{
	traitsInitialized = true;
	constructIndicator = true;
	constructorCallComplete = true;
	obj = this;
}
bool ObjectPrototype::isEqual(ASObject* r)
{
	if (r->is<ObjectPrototype>())
		return this->getClass() == r->getClass();
	return ASObject::isEqual(r);
}

_NR<ASObject> ObjectPrototype::getVariableByMultiname(const multiname& name, GET_VARIABLE_OPTION opt)
{
	_NR<ASObject> ret=ASObject::getVariableByMultiname(name, opt);
	if(!ret.isNull() || prevPrototype.isNull())
		return ret;

	return prevPrototype->getObj()->getVariableByMultiname(name, opt);
}

void ObjectPrototype::setVariableByMultiname(const multiname &name, ASObject *o, ASObject::CONST_ALLOWED_FLAG allowConst)
{
	if (this->isSealed && this->hasPropertyByMultiname(name,false,true))
		throwError<ReferenceError>(kCannotAssignToMethodError, name.normalizedNameUnresolved(o->getSystemState()), "");
	ASObject::setVariableByMultiname(name, o, allowConst);
}


ObjectConstructor::ObjectConstructor(Class_base* c,uint32_t length) : ASObject(c,T_OBJECT,SUBTYPE_OBJECTCONSTRUCTOR),_length(length)
{
	Class<ASObject>::getRef(c->getSystemState())->prototype->incRef();
	this->prototype = Class<ASObject>::getRef(c->getSystemState())->prototype.getPtr();
}

_NR<ASObject> ObjectConstructor::getVariableByMultiname(const multiname& name, GET_VARIABLE_OPTION opt)
{
	if (name.normalizedName(getSystemState()) == "prototype")
	{
		prototype->getObj()->incRef();
		return _NR<ASObject>(prototype->getObj());
	}
	if (name.normalizedName(getSystemState()) == "length")
	{
		return _NR<ASObject>(abstract_di(getSystemState(),_length));
	}
	return getClass()->getVariableByMultiname(name, opt);
}
bool ObjectConstructor::isEqual(ASObject* r)
{
	return this == r || getClass() == r;
}

FunctionPrototype::FunctionPrototype(Class_base* c, _NR<Prototype> p) : Function(c, ASNop)
{
	prevPrototype=p;
	//Add the prototype to the Nop function
	this->prototype = _MR(new_asobject(c->getSystemState()));
	obj = this;
}

_NR<ASObject> FunctionPrototype::getVariableByMultiname(const multiname& name, GET_VARIABLE_OPTION opt)
{
	_NR<ASObject> ret=Function::getVariableByMultiname(name, opt);
	if(!ret.isNull() || prevPrototype.isNull())
		return ret;

	return prevPrototype->getObj()->getVariableByMultiname(name, opt);
}

Function_object::Function_object(Class_base* c, _R<ASObject> p) : ASObject(c,T_OBJECT,SUBTYPE_FUNCTIONOBJECT), functionPrototype(p)
{
	traitsInitialized = true;
	constructIndicator = true;
	constructorCallComplete = true;
}

_NR<ASObject> Function_object::getVariableByMultiname(const multiname& name, GET_VARIABLE_OPTION opt)
{
	_NR<ASObject> ret=ASObject::getVariableByMultiname(name, opt);
	assert(!functionPrototype.isNull());
	if(!ret.isNull())
		return ret;

	return functionPrototype->getVariableByMultiname(name, opt);
}

EARLY_BIND_STATUS ActivationType::resolveMultinameStatically(const multiname& name) const
{
	std::cerr << "Looking for " << name << std::endl;
	for(unsigned int i=0;i<mi->body->trait_count;i++)
	{
		const traits_info* t=&mi->body->traits[i];
		multiname* mname=mi->context->getMultiname(t->name,NULL);
		std::cerr << "\t in " << *mname << std::endl;
		assert_and_throw(mname->ns.size()==1 && mname->name_type==multiname::NAME_STRING);
		if(mname->name_s_id!=name.normalizedNameId(mi->context->root->getSystemState()))
			continue;
		if(find(name.ns.begin(),name.ns.end(),mname->ns[0])==name.ns.end())
			continue;
		return BINDED;
	}
	return NOT_BINDED;
}

const multiname* ActivationType::resolveSlotTypeName(uint32_t slotId) const
{
	std::cerr << "Resolving type at id " << slotId << std::endl;
	for(unsigned int i=0;i<mi->body->trait_count;i++)
	{
		const traits_info* t=&mi->body->traits[i];
		if(t->slot_id!=slotId)
			continue;

		multiname* tname=mi->context->getMultiname(t->type_name,NULL);
		return tname;
	}
	return NULL;
}
