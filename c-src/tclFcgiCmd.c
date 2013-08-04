/*
 * tclFcgiCmd.c
 * FastCGI support for Tcl 8.0
 * Copyright 1998 Tom Poindexter, see ../LICENSE.TERMS for copyright and
 * licensing info
 *
 * based on  tclFCGI.c (fcgi developer's kit for tcl7.4)
 *           and tclUnixChan.c (tcl 8.0 )
 */

/*
 * tclFCGI.c --
 *
 *	TCL functions needed to set up FastCGI commands
 *
 * Copyright (c) 1996 Open Market, Inc.
 *
 * See the file "LICENSE.TERMS" in ../examples directory
 * for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 */

/* Changes by Christoph Bauer 2013 */
/* Changes by Nagarajan Chinnasamy 2013 */

#define FCGI_VERSION "0.8"
#define DLL_BUILD 1
#define USE_TCL_STUBS 1

#include <stdlib.h>
#include <string.h>
#ifdef __WIN32__
#include <windows.h>
#include <winbase.h>
#else
#include <unistd.h>
#include <sys/fcntl.h>
#endif
#include "tcl.h"
#include "fcgiapp.h"


#ifndef FALSE
#define FALSE (0)
#endif

#ifndef TRUE
#define TRUE  (1)
#endif


static int acceptCalled = FALSE;
static int isCGI = FALSE;
static FCGX_Stream *in, *out, *err;
static FCGX_ParamArray envp = NULL;

/*
 * The following defines how much buffer space the kernel should maintain
 * for an fcgi stream
 */

#define FCGI_BUFSIZE (128*1024)

static int fcgi_bufsize = FCGI_BUFSIZE;
static int out_bufsize = 0;
static int err_bufsize = 0;

/*
 * Static routines for this file:
 */

static int	FcgiInputProc _ANSI_ARGS_((ClientData instanceData,
			char *buf, int toRead,  int *errorCode));
static int	FcgiOutputProc _ANSI_ARGS_((ClientData instanceData,
			CONST84 char *buf, int toWrite, int *errorCodePtr));
static void	FcgiWatchProc _ANSI_ARGS_((
			ClientData instanceData, int mask));
static int	FcgiFlushProc _ANSI_ARGS_((ClientData instanceData));

static int	FcgiCloseProc _ANSI_ARGS_((ClientData instanceData,
			Tcl_Interp *interp));
static int	FcgiGetHandleProc _ANSI_ARGS_((
			ClientData instanceData, int direction, ClientData *handlePtr));
static void	DoTclEnv _ANSI_ARGS_((Tcl_Interp *interp,
			char **envp, int set));

static void	UnRegStdio _ANSI_ARGS_((Tcl_Interp *interp));

/*
 * This structure describes the channel type structure for FCGI
 * based IO:
 */

static Tcl_ChannelType fcgiChannelType = {
    "fcgi",                     /* Type name. */
    TCL_CHANNEL_VERSION_5,      /* Set blocking/nonblocking mode.*/
    FcgiCloseProc,              /* Close proc. */
    FcgiInputProc,              /* Input proc. */
    FcgiOutputProc,             /* Output proc. */
    NULL,                       /* Seek proc. */
    NULL,                       /* Set option proc. */
    NULL,                       /* Get option proc. */
    FcgiWatchProc,              /* Initialize notifier. */
    FcgiGetHandleProc,          /* Get OS handles out of channel. */
	NULL,                       /* close2 */
	NULL,                       /* blockmode */
	FcgiFlushProc,              /* flush */
	NULL,                       /* handler */
	NULL,                       /* Wide Seek */
	NULL,                       /* threadaction */
	NULL                        /* truncate */
};

/*
 * For each variable in the array envp, either set or unset it
 * in the interpreter interp.
 */
static void DoTclEnv(Tcl_Interp *interp, char **envp, int set)
{
	char **eptr, *p, *p1;
	Tcl_Obj *value_obj;

	for (eptr = envp; *eptr != NULL; eptr++) {
		p=*eptr;
		p1 = strchr(p, '=');
		*p1 = '\0';
		if(set) {
			value_obj = Tcl_NewStringObj( p1+1, strlen( p1+1 ) );
			Tcl_SetVar2Ex(interp, "env", p, value_obj, TCL_GLOBAL_ONLY);
		} else {
			Tcl_UnsetVar2(interp, "env", p, TCL_GLOBAL_ONLY);
		}
		*p1 = '=';
	}
}


/*
 * unregister and close stdio channels
 */

static void UnRegStdio (Tcl_Interp *interp)
{
    Tcl_Channel chan;

    chan = Tcl_GetChannel(interp, "fcgi0", NULL);
    if (chan != (Tcl_Channel) NULL) {
        Tcl_UnregisterChannel(interp, chan);
    }

    chan = Tcl_GetChannel(interp, "fcgi1", NULL);
    if (chan != (Tcl_Channel) NULL) {
        Tcl_UnregisterChannel(interp, chan);
    }

    chan = Tcl_GetChannel(interp, "fcgi2", NULL);
    if (chan != (Tcl_Channel) NULL) {
        Tcl_UnregisterChannel(interp, chan);
    }
}

static int FcgiAcceptCmd(
	ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj *const*objv)
{

	int acceptResult, createChannels = 0;
	Tcl_Obj * result;
	Tcl_Channel chan;

	if(!acceptCalled) {
		/*
		* First call to FCGX_Accept.  Is application running
		* as FastCGI or as CGI?
		*/
		isCGI = FCGX_IsCGI();
		acceptCalled = TRUE;
		createChannels = 1;
	} else if(isCGI) {
		/*
		* Not first call to FCGX_Accept and running as CGI means
		* application is done.
		*/

		result = Tcl_NewIntObj( EOF );
		Tcl_SetObjResult(interp, result);
		return TCL_OK;
	}

	if( isCGI) {
		/* leave alone stdin, stdout, stderr */
		acceptResult = 1;
	} else {
		/* unregister previous fcgi channels */
		/*	UnRegStdio(interp); */

		/*
		* Unmake Tcl variable settings for the request just completed.
		*/
		if (envp != NULL) {
			DoTclEnv(interp, (char **) envp, FALSE);
		}

		Tcl_Flush(Tcl_GetStdChannel(TCL_STDOUT));

		/*
		* Call FCGX_Accept but preserve environ.
		*/

		acceptResult = FCGX_Accept(&in, &out, &err, &envp);
		if(acceptResult >=  0 && createChannels ) {

			/* make in, out, error into Tcl channels */
			chan = Tcl_CreateChannel(&fcgiChannelType, "fcgi0",
				(ClientData) &in, (TCL_READABLE));
			if (chan != (Tcl_Channel) NULL) {

				Tcl_SetStdChannel(chan, TCL_STDIN);
				Tcl_RegisterChannel(interp, chan);
			}

			chan = Tcl_CreateChannel(&fcgiChannelType, "fcgi1",
				(ClientData) &out, (TCL_WRITABLE));
			if (chan != (Tcl_Channel) NULL) {
				Tcl_SetStdChannel(chan, TCL_STDOUT);
				Tcl_RegisterChannel(interp, chan);
			}

			chan = Tcl_CreateChannel(&fcgiChannelType, "fcgi2",
				(ClientData) &err, (TCL_WRITABLE));
			if (chan != (Tcl_Channel) NULL) {
				Tcl_SetStdChannel(chan, TCL_STDERR);
				Tcl_RegisterChannel(interp, chan);
			}
		}

		/*
		* Make Tcl variable settings for the new request.
		*/
		  if(acceptResult >= 0) {
			  DoTclEnv(interp, (char **) envp, TRUE);
		  }
	}

	result = Tcl_NewIntObj( acceptResult );
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
}


static int FcgiFinishCmd(
	ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj *const*objv)
{
	Tcl_Obj * result;

	/*
	* Unmake Tcl variable settings for the completed request.
	*/
	if(envp != NULL) {
		DoTclEnv(interp, (char **) envp, FALSE);
		envp = NULL;
	}

	/* unregister fcgi channels */
	UnRegStdio(interp);

	if(!acceptCalled || isCGI) {
		/* don't do anything if no socket accepted or running as CGI */
	} else {
		FCGX_Finish();
	}

	result = Tcl_NewIntObj( 0 );
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
}


static int FcgiSetExitStatusCmd(
	ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj *const*objv)
{
	int exitStatus;

	if (objc != 2) {
		Tcl_AppendResult(interp, "wrong # args", NULL);
		return TCL_ERROR;
	}

	if( Tcl_GetIntFromObj( interp, objv[1], &exitStatus) != TCL_OK )
		return TCL_ERROR;

	FCGX_SetExitStatus(exitStatus, in);

	Tcl_SetObjResult(interp, Tcl_NewIntObj( 0 ));
	return TCL_OK;
}


static int FcgiStartFilterDataCmd(
	ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj*const*objv)
{
	Tcl_Obj * result;

	Tcl_Channel chan;

	/* stdin may be marked as EOF, so unregister & re-register stdin */
	chan = Tcl_GetChannel(interp, "fcgi0", NULL);
	if (chan != (Tcl_Channel) NULL) {
		Tcl_UnregisterChannel(interp, chan);
	}
	chan = Tcl_CreateChannel(&fcgiChannelType, "fcgi0",
		(ClientData) in, (TCL_READABLE));
	if (chan != (Tcl_Channel) NULL) {
		Tcl_SetStdChannel(chan, TCL_STDIN);
		Tcl_RegisterChannel(interp, chan);
	}

	result = Tcl_NewIntObj( FCGX_StartFilterData(in));
	Tcl_SetObjResult(interp, result);
	return TCL_OK;
}


static int FcgiSetBufSizeCmd(
	ClientData dummy, Tcl_Interp *interp, int objc,  Tcl_Obj *const*objv)
{

	Tcl_Obj * result;

	if (objc > 1) {
		if( Tcl_GetIntFromObj(interp, objv[1], &fcgi_bufsize) != TCL_OK )
			return TCL_ERROR;
		result = objv[1];
	} else {
		result = Tcl_NewIntObj( fcgi_bufsize );
	}

	Tcl_SetObjResult(interp, result );
	return TCL_OK;
}



#ifdef __cplusplus
EXTERN "C"
#endif

DLLEXPORT
int Tclfcgi_Init(Tcl_Interp *interp) {

		/* source the cgi.tcl cleanup code */
		/* Tcl_Eval(interp, cgi_cleanup); */

#ifdef USE_TCL_STUBS
		Tcl_InitStubs(interp, TCL_VERSION, 0);
#endif

		acceptCalled = FALSE;
		Tcl_CreateObjCommand(
			interp, "FCGI_Accept", FcgiAcceptCmd, 0, NULL);
		Tcl_CreateObjCommand(
			interp, "FCGI_Finish", FcgiFinishCmd, 0, NULL);
		Tcl_CreateObjCommand(
			interp, "FCGI_SetExitStatus", FcgiSetExitStatusCmd, 0, NULL);
		Tcl_CreateObjCommand(
			interp, "FCGI_StartFilterData", FcgiStartFilterDataCmd, 0, NULL);
		Tcl_CreateObjCommand(
			interp, "FCGI_SetBufSize", FcgiSetBufSizeCmd, 0, NULL);

		if (Tcl_PkgProvide(interp, "Fcgi", FCGI_VERSION) != TCL_OK) {
			return TCL_ERROR;
		}

		return TCL_OK;
}



/*
*----------------------------------------------------------------------
*
* FcgiInputProc --
*
*	This procedure is invoked by the generic IO level to read input
*	from a Fcgi based channel.
*
*	NOTE: We cannot share code with FilePipeInputProc because here
*	we must use recv to obtain the input from the channel, not read.
*
* Results:
*	The number of bytes read is returned or -1 on error. An output
*	argument contains the POSIX error code on error, or zero if no
*	error occurred.
*
* Side effects:
*	Reads input from the input device of the channel.
*
*----------------------------------------------------------------------
*/

static  int
	FcgiInputProc(ClientData instanceData, /* Socket state. */
	char *buf, /* Where to store data read. */
	int bufSize, /* How much space is available in the buffer? */
	int *errorCodePtr) /* Where to store error code. */
{
	*errorCodePtr = 0;
	FCGX_Stream *fcgx = *(FCGX_Stream **) instanceData;
	if( fcgx == NULL || buf == NULL ) {
		return 0;
	}

	return (FCGX_GetStr(buf, bufSize, fcgx));
}



/*
*----------------------------------------------------------------------
*
* FcgiOutputProc --
*
*	This procedure is invoked by the generic IO level to write output
*	to a Fcgi based channel.
*
*	NOTE: We cannot share code with FilePipeOutputProc because here
*	we must use send, not write, to get reliable error reporting.
*
* Results:
*	The number of bytes written is returned. An output argument is
*	set to a POSIX error code if an error occurred, or zero.
*
* Side effects:
*	Writes output on the output device of the channel.
*
*----------------------------------------------------------------------
*/

static int
	FcgiOutputProc(ClientData instanceData, /* Socket state. */
	const char *buf,                        /* The data buffer. */
	int toWrite,                            /* How many bytes to write? */
	int *errorCodePtr)                      /* Where to store error code. */
{
	FCGX_Stream *fcgx = *(FCGX_Stream **) instanceData;
	int written = 0;
	int flushit = 0;

	*errorCodePtr = 0;

	if( fcgx->isReader != 0 || fcgx->isClosed != 0 ) {
		return 0;
	}

	if (fcgx == out) {
		out_bufsize += toWrite;
		if (out_bufsize > fcgi_bufsize) flushit = 1;
	} else if (fcgx == err) {
		err_bufsize += toWrite;
		if (err_bufsize > fcgi_bufsize) flushit = 1;
	}

	while( written < toWrite ) {
		int block =  FCGX_PutStr(buf+written, toWrite-written, fcgx);
		if( block == -1 ) {
			*errorCodePtr = fcgx->FCGI_errno;
			written = -1;
			break;
		}
		written += block;
	}

	if (flushit) {
		FCGX_FFlush(fcgx);
		if (fcgx == out) {
			out_bufsize = 0;
		} else {
			err_bufsize = 0;
		}
	}

	return written;
}



/*
*----------------------------------------------------------------------
*
* FcgiCloseProc --
*
*	This procedure is invoked by the generic IO level to perform
*	channel-type-specific cleanup when a Fcgi based channel
*	is closed.
*
* Results:
*	0 if successful, the value of errno if failed.
*
* Side effects:
*	Closes the socket of the channel.
*
*----------------------------------------------------------------------
*/



static int FcgiCloseProc(
	ClientData instanceData,	/* The socket to close. */
	Tcl_Interp *interp		/* For error reporting - unused. */)
{
	FCGX_Stream *fcgx = *(FCGX_Stream **) instanceData;
	if (fcgx == out) {
		out_bufsize = 0;
	} else {
		err_bufsize = 0;
	}
	return 0;
}

static int FcgiGetHandleProc (
	ClientData instanceData,
	int direction,
	ClientData *handlePtr)

{
	return TCL_ERROR;
#if 0
	if( direction == TCL_READABLE )
#ifdef __WIN32__
		*handlePtr =  (ClientData) GetStdHandle(STD_INPUT_HANDLE);
#else
		*handlePtr = 0
#endif
	else
#ifdef __WIN32__
		*handlePtr =  (ClientData) GetStdHandle( STD_OUTPUT_HANDLE);
#else
		*handlePtr = 1
#endif

	return TCL_OK;
#endif
}

static int FcgiFlushProc( ClientData instanceData)
{
	FCGX_Stream *fcgx = *(FCGX_Stream **) instanceData;
	FCGX_FFlush(fcgx);

	return TCL_OK;
}

 static void FcgiWatchProc(
        ClientData instanceData,
        int mask)
 {
 }

