/* While looking for a timing example for another problem, I came across
   http://msdn.microsoft.com/en-us/magazine/cc163996.aspx, which is an
   example of a common high-resolution solution on Windows, namely, 
   synchronizing performance counter values with system time changes.

   What this does is loop trying to catch the difference between two
   calls to get the system time, then saves off the performance counter
   value at that instant, along with the current time, which are combined
   to be the synchronization point. When requesting the current time,
   the synchronization counter is used to find the elapsed
   ticks between now and then, then add that to the synchronization timestamp,
   and you have the difference in microseconds.

   The implementation is basically the same as what is listed in the article,
   save for a few small quirks, and the fact that it's being used in C instead
   of C++. Johan Nilsson is the author of the article, and implementer of the
   timing code, so the credit for that goes to him. If the use of this
   violates any sort of license, please contact me and I will remove the code
   in question.

   Mac/Linux:
   I added support for Mac/Linux by using pthreads and a similar loop idea
   that uses gettimeofday. If there's a better and/or more accurate way 
   of doing this on these platforms, I'd love to know about it.
*/

#include "include/Python.h"
#include "include/structmember.h"

//#pragma comment(lib, "python27.lib")

#if PY_MAJOR_VERSION == 3
#define PYTHON3
#else
#define PYTHON2
#endif

#ifdef MS_WINDOWS
/* Py_XDECREF causes "conditional expression is constant" warnings */
#pragma warning(disable: 4127)
#include <windows.h>
#endif /* MS_WINDOWS */

#ifndef MS_WINDOWS
#define UNIX
#endif

#ifdef UNIX
#include <pthread.h>
#include <sys/time.h>
#endif

#ifndef BOOL
typedef int BOOL;
#define FALSE 0
#define TRUE 1
#endif

#ifdef PYTHON3
#define GET_DURATION(args) (PyLong_AsSsize_t(PyTuple_GET_ITEM(args, 0)))
#else
#define GET_DURATION(args) (PyInt_AsSsize_t(PyTuple_GET_ITEM(args, 0)))
#endif


#define TIMER_VERSION "0.1"
#define AUTHOR "Brian Curtin"


#ifdef MS_WINDOWS
typedef struct {
    FILETIME time;
    LARGE_INTEGER perf_counter;
} reference_point;

void
synchronize(reference_point *ref)
{
    FILETIME before = {0, 0};
    FILETIME now = {0, 0};
    LARGE_INTEGER counter;

    /* Catch the first difference in time in the loop, then store the
       accompanying performance counter result and current time to be used
       as a reference point when requesting a timestamp. */
    GetSystemTimeAsFileTime(&before);
    do {
        GetSystemTimeAsFileTime(&now);
        /* This isn't entirely fool-proof. Our thread could get switched out
           here, which could result in a gap in time between the previous
           and the next function calls. There are ways to protect against
           this or be smarter about it, but the side effects are relatively
           rare and not currently worth the effort to protect against.
           Don't use this code if lives depend on it. */
        QueryPerformanceCounter(&counter);
    } while ((before.dwHighDateTime == now.dwHighDateTime) &&
             (before.dwLowDateTime == now.dwLowDateTime));

    ref->time = now;
    ref->perf_counter = counter;
}

void
timestamp(LARGE_INTEGER frequency, const reference_point *reference,
          FILETIME *current_time)
{
    LARGE_INTEGER current_counter, ticks_elapsed;
    ULARGE_INTEGER now, now_ticks;

    /* Snapshot of the current counter value to get the duration since
       we synchronized. */
    QueryPerformanceCounter(&current_counter);
    ticks_elapsed.QuadPart =
        current_counter.QuadPart - reference->perf_counter.QuadPart;

    /* Divide by the current frequency and convert to microseconds */
    now_ticks.QuadPart = 
        (ULONGLONG)((((double)ticks_elapsed.QuadPart/
                      (double)frequency.QuadPart)*1000000.0));

    now.HighPart = reference->time.dwHighDateTime;
    now.LowPart = reference->time.dwLowDateTime;
    /* Add the elapsed microseconds to the reference time to get the
       current time */
    now.QuadPart += now_ticks.QuadPart;

    /* The final result - elapsed microseconds */
    current_time->dwHighDateTime = now.HighPart;
    current_time->dwLowDateTime = now.LowPart;
}
#endif /* MS_WINDOWS */


typedef struct {
	PyObject_HEAD
    PyObject *callback;
    PyObject *args; /* Sent to the callback */
    PyObject *kwargs;
    BOOL expired;
    BOOL started;
    time_t duration; /* Microseconds */
    time_t elapsed; /* Microseconds */
 
#ifdef MS_WINDOWS
    HANDLE thread;
    DWORD thread_id;
#elif defined(UNIX)
    pthread_t thread;
    int thread_rv;
#endif
} Timer;

static volatile BOOL die = FALSE;

static PyObject *
Timer_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    Timer *self;
    PyObject *callback = NULL;
    Py_ssize_t num_args, duration;

    /* Don't use PyArg_Parse* functions here.
       We want to take kwargs without specifying named arguments. We'll
       manually pick items out of the args tuple, then take a slice of the
       remaining ones as our callback's args, then take all kwargs. */
    num_args = PyTuple_GET_SIZE(args);

    if (num_args < 2) {
        PyErr_SetString(PyExc_TypeError, "Timer takes at least 2 arguments");
        return NULL;
    }

    duration = GET_DURATION(args);
    if (duration == -1) {
        /* OverflowError gets set in this case. */
        return NULL;
    }

    callback = PyTuple_GET_ITEM(args, 1);
    if (!PyCallable_Check(callback)) {
        PyErr_Format(PyExc_TypeError, "callback parameter must be callable");
        return NULL;
    }
    
    self = (Timer*)type->tp_alloc(type, 0);
    if (self == NULL)
        return NULL;

    Py_INCREF(callback);
    self->callback = callback;

    /* Optional attributes from here on. */
    if (num_args > 2)
        self->args = PyTuple_GetSlice(args, 2, num_args);
    else
        self->args = PyTuple_New(0);

    /* kwargs might be NULL, which is fine for PyObject_Call.
       If we get kwargs, use them, nothing special to do here. */
    self->kwargs = kwargs;
    Py_XINCREF(kwargs);

    self->duration = duration;
    self->elapsed = 0;
    self->expired = FALSE;

    return (PyObject*)self;
}

static void
Timer_dealloc(Timer *self)
{
    Py_XDECREF(self->callback);
    Py_XDECREF(self->args);
    Py_XDECREF(self->kwargs);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject *
Timer_str(Timer* self)
{
    return PyUnicode_FromFormat(
        "<%s at %p duration=%d, expired=%d, started=%d>",
        Py_TYPE(self)->tp_name, self,
        self->duration, self->expired, self->started);
}

/*static PyObject *
Timer_repr(Timer* self)
{
    return PyUnicode_FromFormat("%s(%d, %s)",
            Py_TYPE(self)->tp_name, self->duration,
            Py_TYPE(self->callback)->tp_name);
}*/

#ifdef MS_WINDOWS
void WINAPI
Timer_win32_thread(Timer *self)
{
    PyGILState_STATE gil_state;
    PyObject *call_rslt;

    reference_point ref_point;
    LARGE_INTEGER frequency;
    FILETIME start_time, current_time;
    time_t start, cur, val;

    QueryPerformanceFrequency(&frequency);
    synchronize(&ref_point);

    timestamp(frequency, &ref_point, &start_time);
    start = ((__int64)start_time.dwHighDateTime << 32) + start_time.dwLowDateTime;

    while(1) {
        timestamp(frequency, &ref_point, &current_time);
        cur = ((__int64)current_time.dwHighDateTime << 32) + current_time.dwLowDateTime;
        val = cur - start;
        if (val > self->duration) {
            /* Grab the GIL, call out into the user callback, then give the
               GIL back. If we're given a method as opposed to a function
               we have to do things slightly different. */
            gil_state = PyGILState_Ensure();
            /* This will work for free functions and bound methods. */
            call_rslt = PyObject_Call(self->callback,
                                      self->args, self->kwargs);
            Py_DECREF(self->args); /* XXX: needed? */
            PyGILState_Release(gil_state);

            /* Flag denoting timer expiration rather than being stopped */
            self->expired = TRUE;
            self->started = FALSE;
            /* Don't use val here since it is likely to be larger than the
               requested duration. Using duration is another way to signal
               that a timeout occurred. */
            self->elapsed = self->duration;
            
            if (call_rslt == NULL) {
                /* These calls also require the GIL */
                gil_state = PyGILState_Ensure();
                PyErr_SetString(PyExc_RuntimeError, "Unable to call callback");
                PyErr_Print();
                PyGILState_Release(gil_state);
            } else
                Py_DECREF(call_rslt);

            break;
        } else if (die) {
            self->elapsed = val;
            break;
        }
    }
}
#endif /* MS_WINDOWS */

#ifdef UNIX
void *
Timer_posix_thread(void *data)
{
    Timer *self = (Timer*)data;

    PyGILState_STATE gil_state;
    PyObject *call_rslt;
    struct timeval start, cur;
    time_t val;

    gettimeofday(&start, NULL);

    while(1) {
        gettimeofday(&cur, NULL);
        /* Seconds to microseconds */
        val = (cur.tv_sec - start.tv_sec) * 1000000;
        val += (cur.tv_usec - start.tv_usec);

        if (val > self->duration) {
            gil_state = PyGILState_Ensure();
            call_rslt = PyObject_Call(self->callback,
                                      self->args, self->kwargs);
            Py_DECREF(self->args); /* XXX: needed? */
            PyGILState_Release(gil_state);

            self->expired = TRUE;
            self->started = FALSE;
            self->elapsed = self->duration;

            if (call_rslt == NULL) {
                /* These calls also require the GIL */
                gil_state = PyGILState_Ensure();
                PyErr_SetString(PyExc_RuntimeError, "Unable to call callback");
                PyErr_Print();
                PyGILState_Release(gil_state);
            } else
                Py_DECREF(call_rslt);

            break;
        } else if(die) {
            self->elapsed = val;
            break;
        }
    }

    /* pthread_exit(NULL); */
}
#endif /* UNIX */

/* Handle all thread cleanup here. Used by reset and stop functionality.
   In any error cases, set an exception and return FALSE, then the caller
   should check the return value and return NULL, thus raising the exception.*/
BOOL
cleanup_Timer_thread(Timer *self)
{
#ifdef MS_WINDOWS
    if (self->thread != NULL) {
        if (WaitForSingleObject(self->thread, INFINITE) != WAIT_OBJECT_0)
            goto fail;
        CloseHandle(self->thread);
        self->thread = NULL;
        self->thread_id = 0;
    }
#elif defined(UNIX)
    if (self->thread_rv == 0) {
        if (pthread_join(self->thread, NULL) != 0)
            goto fail;
        self->thread = 0;
    }
#endif
    return TRUE;

fail:
    PyErr_SetString(PyExc_RuntimeError, "Error stopping timer thread");
    return FALSE;
}

PyDoc_STRVAR(Timer_start_doc,
"start()\n"
"\n"
"Start a Timer object.");

static PyObject *
Timer_start(Timer *self)
{
    if (self->started)
        Py_RETURN_NONE;
    die = FALSE;

    self->elapsed = 0;

#ifdef MS_WINDOWS
    self->thread = CreateThread(NULL, 0,
                                (LPTHREAD_START_ROUTINE)Timer_win32_thread,
                                self, 0, &self->thread_id);
    if (self->thread == NULL) {
        PyErr_SetString(PyExc_WindowsError,
                        "CreateThread error. Unable to start timer thread");
        return NULL;
    }
#elif defined(UNIX)
    self->thread_rv = pthread_create(&self->thread, NULL, Timer_posix_thread,
                                     (void*)self);
    if (self->thread_rv != 0) {
        PyErr_SetString(PyExc_OSError,
                        "pthread_create error. Unable to start timer thread");
        return NULL;
    }
#endif

    self->started = TRUE;

    Py_RETURN_NONE;
}

PyDoc_STRVAR(Timer_reset_doc,
"reset()\n"
"\n"
"Reset a Timer object. Sets the expired and running members to False\n"
"and sets the elapsed member to 0.");

static PyObject *
Timer_reset(Timer *self)
{
    self->started = FALSE;
    self->expired = FALSE;
    self->elapsed = 0;

    if (!cleanup_Timer_thread(self))
        return NULL;
    
    Py_RETURN_NONE;
}

PyDoc_STRVAR(Timer_stop_doc,
"stop()\n"
"\n"
"Stop a timer object. Returns the current elapsed time.");

static PyObject *
Timer_stop(Timer *self)
{
    if (!self->started)
        goto done;

    /* All thread stoppage and cleanup code is in cleanup_Timer_thread, so
       set the die flag, which will interrupt the thread, then let the
       cleanup handle the rest. */
    die = TRUE;

    if (!cleanup_Timer_thread(self))
        return NULL;

    self->started = FALSE;

done:
    return PyLong_FromUnsignedLongLong(self->elapsed);
}

static PyMethodDef Timer_methods[] = {
    {"start", (PyCFunction)Timer_start, METH_NOARGS, Timer_start_doc},
    {"stop", (PyCFunction)Timer_stop, METH_NOARGS, Timer_stop_doc},
    {"reset", (PyCFunction)Timer_reset, METH_NOARGS, Timer_reset_doc},
    {NULL, NULL}
};

PyDoc_STRVAR(Timer_elapsed_doc,
"Integer representing the current elapsed time in microseconds");

PyDoc_STRVAR(Timer_expired_doc,
"Boolean representing whether or not the timer finished on it's \n"
"own or if it reached the maximum duration.");

PyDoc_STRVAR(Timer_running_doc,
"Boolean representing whether or not the timer is currently running.");

static PyMemberDef Timer_members[] = {
    {"elapsed", T_INT, offsetof(Timer, elapsed), 0, Timer_elapsed_doc},
    {"expired", T_BOOL, offsetof(Timer, expired), 0, Timer_expired_doc},
    {"running", T_BOOL, offsetof(Timer, started), 0, Timer_running_doc},
    {NULL}
};

PyDoc_STRVAR(Timer_class_doc,
"Timer(duration, callback, args)\n"
"\n"
"Creates a timer object which can be started, stopped, or reset.\n"
"This provides a microsecond resolution timer outside of the Python GIL.");

static PyTypeObject Timer_type = {
	PyVarObject_HEAD_INIT(NULL, 0)
    "_timer.Timer",                             /*tp_name*/
    sizeof(Timer),                              /*tp_basicsize*/
    0,                                          /*tp_itemsize*/
    (destructor)Timer_dealloc,                  /*tp_dealloc*/
    0,                                          /*tp_print*/
    0,                                          /*tp_getattr*/
    0,                                          /*tp_setattr*/
    0,                                          /*tp_compare*/
    (reprfunc)Timer_str,                        /*tp_repr*/
    0,                                          /*tp_as_number*/
    0,                                          /*tp_as_sequence*/
    0,                                          /*tp_as_mapping*/
    0,                                          /*tp_hash*/
    0,                                          /*tp_call*/
    (reprfunc)Timer_str,                        /*tp_str*/
    0,                                          /*tp_getattro*/
    0,                                          /*tp_setattro*/
    0,                                          /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /*tp_flags*/
    Timer_class_doc,                            /*tp_doc*/
    0,		                                    /*tp_traverse*/
    0,		                                    /*tp_clear*/
    0,		                                    /*tp_richcompare*/
    0,		                                    /*tp_weaklistoffset*/
    0,		                                    /*tp_iter*/
    0,		                                    /*tp_iternext*/
    Timer_methods,                              /*tp_methods*/
    Timer_members,                              /*tp_members*/
    0,                                          /*tp_getset*/
    0,                                          /*tp_base*/
    0,                                          /*tp_dict*/
    0,                                          /*tp_descr_get*/
    0,                                          /*tp_descr_set*/
    0,                                          /*tp_dictoffset*/
    0,                                          /*tp_init*/
    PyType_GenericAlloc,                        /*tp_alloc*/
    Timer_new,                                  /*tp_new*/
};


PyDoc_STRVAR(module_doc, "A simple timer module implemented in C.");

#ifdef PYTHON3
static struct PyModuleDef _timer_module = {
    PyModuleDef_HEAD_INIT,
    "_timer",
    module_doc,
    -1,
    NULL /*functions*/,
    NULL, NULL, NULL, NULL
};
#endif

#ifdef PYTHON3
PyMODINIT_FUNC PyInit__timer(void)
#else
PyMODINIT_FUNC init_timer()
#endif
{
    PyObject *module;
    
#ifdef PYTHON3
    module = PyModule_Create(&_timer_module);
#else
    module = Py_InitModule3("_timer", NULL, module_doc);
#endif

    if (module == NULL)
        goto fail;

    if (PyType_Ready(&Timer_type) < 0)
        goto fail;
    Py_INCREF(&Timer_type);
    PyModule_AddObject(module, "Timer", (PyObject*)&Timer_type);
    
    PyModule_AddStringConstant(module, "__version__", TIMER_VERSION);
    PyModule_AddStringConstant(module, "__author__", AUTHOR);

#ifdef PYTHON3
    return module;
#endif

fail:
#ifdef PYTHON3
    return NULL;
#else
    return;
#endif
}
