#include "log_average_power_cf.h"

int LogAveragePower_traverse(LogAveragePower* self, visitproc visit, void* arg) {
    return 0;
}

int LogAveragePower_clear(LogAveragePower* self) {
    self->run = false;
    void* retval = NULL;
    if (self->worker != 0) pthread_join(self->worker, retval);
    self->worker = 0;
    if (self->inputBuffer != NULL) Py_DECREF(self->inputBuffer);
    self->inputBuffer = NULL;
    if (self->buffer != NULL) Py_DECREF(self->buffer);
    self->buffer = NULL;
    return 0;
}

void LogAveragePower_dealloc(LogAveragePower* self) {
    PyObject_GC_UnTrack(self);
    LogAveragePower_clear(self);
    Py_TYPE(self)->tp_free((PyObject*) self);
}

PyObject* LogAveragePower_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    LogAveragePower* self;
    self = (LogAveragePower*) type->tp_alloc(type, 0);
    if (self != NULL) {
        self->buffer = NULL;
        self->inputBuffer = NULL;
        self->read_pos = 0;
        self->fft_size = 0;
        self->run = true;
        self->worker = 0;
        self->add_db = 0;
        self->avg_number = 0;
    }
    return (PyObject*) self;
}

void* LogAveragePower_worker(void* ctx) {
    LogAveragePower* self = (LogAveragePower*) ctx;
    Py_INCREF(self);

    complexf* input = malloc(sizeof(complexf) * self->fft_size);
    float* output = malloc(sizeof(float) * self->fft_size);

    float add_db = self->add_db - 10.0 * log10(self->avg_number);
    while (self->run) {
        memset(output, 0, sizeof(float) * self->fft_size);
        for (int i = 0; i < self->avg_number; i++) {
            self->read_pos = Buffer_read_n(self->inputBuffer, input, self->read_pos, self->fft_size);
            accumulate_power_cf(input, output, self->fft_size);
        }
        log_ff(output, output, self->fft_size, add_db);
        Buffer_write(self->buffer, output, self->fft_size);
    }

    Py_DECREF(self);
    return NULL;
}

int LogAveragePower_init(LogAveragePower* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {"add_db", "fft_size", "avg_number", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|iii", kwlist,
                                     &self->add_db, &self->fft_size, &self->avg_number))
        return -1;

    // we output 32-bit float real samples
    PyObject* bufferArgs = Py_BuildValue("()");
    if (bufferArgs == NULL) return -1;
    PyObject* bufferKwargs = Py_BuildValue("{s:i}", "item_size", sizeof(float));
    if (bufferKwargs == NULL) return -1;
    self->buffer = (Buffer*) PyObject_Call((PyObject*) &BufferType, bufferArgs, bufferKwargs);
    Py_DECREF(args);
    Py_DECREF(bufferKwargs);

    if (self->buffer == NULL) return -1;
    Py_INCREF(self->buffer);

    return 0;
}

PyObject* LogAveragePower_setInput(LogAveragePower* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {"input", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O!", kwlist,
                                     &BufferType, &self->inputBuffer))
        return NULL;

    if (Buffer_getItemSize(self->inputBuffer) != sizeof(complexf)) {
        self->inputBuffer = NULL;
        PyErr_SetString(PyExc_ValueError, "Input buffer item size mismatch");
        return NULL;
    }

    Py_INCREF(self->inputBuffer);

    if (pthread_create(&self->worker, NULL, LogAveragePower_worker, self) != 0) {
        PyErr_SetFromErrno(PyExc_OSError);
        return NULL;
    }

    Py_RETURN_NONE;
}

PyObject* LogAveragePower_getBuffer(LogAveragePower* self, PyObject* Py_UNUSED(ignored)) {
    if (self->buffer == NULL) {
        Py_RETURN_NONE;
    }
    Py_INCREF(self->buffer);
    return (PyObject*) self->buffer;
}

PyMethodDef LogAveragePower_methods[] = {
    {"getBuffer", (PyCFunction) LogAveragePower_getBuffer, METH_NOARGS,
     "get the output buffer"
    },
    {"setInput", (PyCFunction) LogAveragePower_setInput, METH_VARARGS | METH_KEYWORDS,
     "set the input buffer"
    },
    {NULL}  /* Sentinel */
};

PyTypeObject LogAveragePowerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "pycsdr.LogAveragePower",
    .tp_doc = "Custom objects",
    .tp_basicsize = sizeof(LogAveragePower),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,
    .tp_new = LogAveragePower_new,
    .tp_init = (initproc) LogAveragePower_init,
    .tp_dealloc = (destructor) LogAveragePower_dealloc,
    .tp_traverse = (traverseproc) LogAveragePower_traverse,
    .tp_clear = (inquiry) LogAveragePower_clear,
    .tp_methods = LogAveragePower_methods,
};
