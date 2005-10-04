#include <stdio.h>
#include <dlfcn.h>
#include "opensrf/utils.h"
#include "opensrf/logging.h"
#include "objson/object.h"
#include "osrf_app_session.h"
#include "osrf_hash.h"


/**
  All OpenSRF methods take the signature
  int methodName( osrfMethodContext* );
  If a negative number is returned, it means an unknown error occured and an exception
  will be returned to the client automatically.
  If a positive number is returned, it means that libopensrf should send a 'Request Complete'
  message following any messages sent by the method.
  If 0 is returned, it tells libopensrf that the method completed successfully and 
  there is no need to send any further data to the client.
  */



/** 
  This macro verifies methods receive the correct parameters */
#define _OSRF_METHOD_VERIFY_CONTEXT(d) \
	if(!d) return -1; \
	if(!d->session) { osrfLog( OSRF_ERROR, "Session is NULL in app reqeust" ); return -1; }\
	if(!d->method) { osrfLog( OSRF_ERROR, "Method is NULL in app reqeust" ); return -1; }\
	if(!d->params) { osrfLog( OSRF_ERROR, "Params is NULL in app reqeust %s", d->method->name ); return -1; }\
	if( d->params->type != JSON_ARRAY ) { \
		osrfLog( OSRF_ERROR, "'params' is not a JSON array for method %s", d->method->name);\
		return -1; }\
	if( !d->method->name ) { osrfLog(OSRF_ERROR, "Method name is NULL"); return -1; } 

#ifdef OSRF_LOG_PARAMS 
#define OSRF_METHOD_VERIFY_CONTEXT(d) \
	_OSRF_METHOD_VERIFY_CONTEXT(d); \
	char* __j = jsonObjectToJSON(d->params);\
	if(__j) { \
		osrfLog( OSRF_INFO, "%s %s - %s", d->session->remote_service, d->method->name, __j);\
		free(__j); \
	} 
#else
#define OSRF_METHOD_VERIFY_CONTEXT(d) _OSRF_METHOD_VERIFY_CONTEXT(d); 
#endif



/* used internally to make sure the method description provided is OK */
#define OSRF_METHOD_VERIFY_DESCRIPTION(app, d) \
	if(!app) return -1; \
	if(!d) return -1;\
	if(!d->name) { osrfLog( OSRF_ERROR, "No method name provided in description" ), return -1; } \
	if(!d->symbol) { osrfLog( OSRF_ERROR, "No method symbol provided in description" ), return -1; } \
	if(!d->notes) d->notes = ""; \
	if(!d->paramNotes) d->paramNotes = "";\
	if(!d->returnNotes) d->returnNotes = "";




/* Some well known parameters */
#define OSRF_SYSMETHOD_INTROSPECT				"opensrf.system.method"
#define OSRF_SYSMETHOD_INTROSPECT_ATOMIC		"opensrf.system.method.atomic"
#define OSRF_SYSMETHOD_INTROSPECT_ALL			"opensrf.system.method.all"
#define OSRF_SYSMETHOD_INTROSPECT_ALL_ATOMIC	"opensrf.system.method.all.atomic"
#define OSRF_SYSMETHOD_ECHO						"opensrf.system.echo"
#define OSRF_SYSMETHOD_ECHO_ATOMIC				"opensrf.system.echo.atomic"

//#define OSRF_METHOD_ATOMIC 1
//#define OSRF_METHOD_CACHABLE 2

	

struct _osrfApplicationStruct {
	//char* name;										/* the name of our application */
	void* handle;									/* the lib handle */
	osrfHash* methods;
	//struct _osrfMethodStruct* methods;		/* list of methods */
//	struct _osrfApplicationStruct* next;	/* next application */
};
typedef struct _osrfApplicationStruct osrfApplication;


struct _osrfMethodStruct {
	char* name;					/* the method name */
	char* symbol;				/* the symbol name (function) */
	char* notes;				/* public method documentation */
	int argc;					/* how many args this method expects */
	char* paramNotes;			/* Description of the params expected for this method */
//	struct _osrfMethodStruct* next; /* nest method in the list */
	int sysmethod;				/* true if this is a system method */
	int streaming;				/* true if this is a streamable method */
	int atomic;					/* true if the method is an atomic method */
	int cachable;				/* true if the method is cachable */
}; 
typedef struct _osrfMethodStruct osrfMethod;

struct _osrfMethodContextStruct {
	osrfAppSession* session;	/* the current session */
	osrfMethod* method;			/* the requested method */	
	jsonObject* params;			/* the params to the method */
	int request;					/* request id */
	jsonObject* responses;		/* array of cached responses. */
};
typedef struct _osrfMethodContextStruct osrfMethodContext;



/** 
  Register an application
  @param appName The name of the application
  @param soFile The library (.so) file that implements this application
  @return 0 on success, -1 on error
  */
int osrfAppRegisterApplication( char* appName, char* soFile );

/**
  Register a method
  @param appName The name of the application that implements the method
  @param methodName The fully qualified name of the method
  @param symbolName The symbol name (function) that implements the method
  @param notes Public documentation for this method.
  @params params String description description of the params expected
  @params argc The number of arguments this method expects 
  @param streaming True if this is a streaming method that requires an atomic version
  @return 0 on success, -1 on error
  */
int osrfAppRegisterMethod( char* appName, char* methodName, 
		char* symbolName, char* notes, char* params, int argc, int streaming );

int _osrfAppRegisterMethod( char* appName, char* methodName, 
		char* symbolName, char* notes, char* params, int argc, int streaming, int system );

osrfMethod* _osrfAppBuildMethod( char* methodName, 
		char* symbolName, char* notes, char* params, int argc, int sysmethod, int streaming );

/**
  Registher a method
  @param appName The name of the application that implements the method
  @params desc The method description
  @return 0 on success, -1 on error
  */
/*
int osrfAppRegisterMethod( char* appName, osrfMethodDescription* desc );
*/

/**
  Finds the given app in the list of apps
  @param name The name of the application
  @return The application pointer or NULL if there is no such application
  */
osrfApplication* _osrfAppFindApplication( char* name );

/**
  Finds the given method for the given app
  @param appName The application
  @param methodName The method to find
  @return A method pointer or NULL if no such method 
  exists for the given application
  */
osrfMethod* _osrfAppFindMethod( char* appName, char* methodName );

/**
  Finds the given method for the given app
  @param app The application object
  @param methodName The method to find
  @return A method pointer or NULL if no such method 
  exists for the given application
  */
osrfMethod* __osrfAppFindMethod( osrfApplication* app, char* methodName );


/**
  Runs the specified method for the specified application.
  @param appName The name of the application who's method to run
  @param methodName The name of the method to run
  @param ses The app session attached to this request
  @params reqId The request id for this request
  @param params The method parameters
  */
int osrfAppRunMethod( char* appName, char* methodName, 
		osrfAppSession* ses, int reqId, jsonObject* params );


/**
  Trys to run the requested method as a system method.
  A system method is a well known method that all
  servers implement.  
  @param context The current method context
  @return 0 if the method is run successfully, return < 0 means
  the method was not run, return > 0 means the method was run
  and the application code now needs to send a 'request complete' 
  message
  */
int __osrfAppRunSystemMethod(osrfMethodContext* context);

/**
  Registers all of the system methods for this app so that they may be
  treated the same as other methods */
int __osrfAppRegisterSysMethods( char* app );



/**
  Responds to the client with a method exception
  @param ses The current session
  @param request The request id
  @param msg The debug message to send to the client
  @return 0 on successfully sending of the message, -1 otherwise
  */
int osrfAppRequestRespondException( osrfAppSession* ses, int request, char* msg, ... );

int __osrfAppPostProcess( osrfMethodContext* context, int retcode );


int osrfAppRespond( osrfMethodContext* context, jsonObject* data );
int _osrfAppRespond( osrfMethodContext* context, jsonObject* data, int complete );
int osrfAppRespondComplete( osrfMethodContext* context, jsonObject* data );

/* OSRF_METHOD_ATOMIC and/or OSRF_METHOD_CACHABLE and/or 0 for no special options */
//int osrfAppProcessMethodOptions( char* method );

int osrfAppIntrospect( osrfMethodContext* ctx );
int osrfAppIntrospectAll( osrfMethodContext* ctx );


