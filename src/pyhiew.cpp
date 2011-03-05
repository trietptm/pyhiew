#include <Python.h>
#include <string.h>
#include <stdio.h>
#include <conio.h>
#include <Windows.h>
#include <stdarg.h>
#include "..\sdk\hem.h"
#include "util.h"

//--------------------------------------------------------------------------
// Test script
//#define TESTMODE

#ifdef TESTMODE
  #define PYHIEW_TESTSCRIPT "\\test.py"
#endif

//--------------------------------------------------------------------------
// Consts
#define   HEM_PYHIEW_VERSION_MAJOR     0
#define   HEM_PYHIEW_VERSION_MINOR     1
#define   HEM_PYHIEW_MODNAME           "pyhiew.hem"
#define   HEM_PYHIEW_VERSION           "VERSION"

#define   HEM_MAX_DUALSTR_LEN          20 // from the docs
#define   HEM_MAX_FINDSTR_LEN          20 // from the docs

#define   PYHIEW_CMODNAME               "_hiew" // The C++ module name
#define   PYPYHIEW_MODNAME             "hiew" // The Python module counterpart name
#define   PYHIEW_PATH                  "pyhiew" // Path of the hiew scripts in hiew32 folder
#define   PYHIEW_INIT_SCRIPT           "init.py" // pyhiew's init script
#define   PYHIEW_DATA_CLASS            "Data" // Helper class for GetData()
#define   PYHIEW_VAR_MAIN_FUNC         "PyHiew_Main" // Startup function
#define   PYHIEW_VAR_PYHIEWPATH        "PYHIEW_PATH" // Hiew32\pyhiew path
#define   PYHIEW_VAR_VERSION           "PYHIEW_VERSION" // pyhiew version
#define   PYHIEW_VER_MAJOR             "0"
#define   PYHIEW_VER_MINOR             "2"
#define   PYHIEW_VER_BUILD             "1"

//--------------------------------------------------------------------------
extern HEMINFO_TAG hem_pyhiew;
extern PyMethodDef pyhiew_methods[];
static char pyhiew_path[MAX_PATH];
static char pyhiew_init_file[MAX_PATH];
static bool g_init_executed = false;
static HEMCALL_TAG *g_pHemCall = NULL;
static int g_HemEntryPointReturn;
static HEM_BYTE hiewSdkVerMajor, hiewSdkVerMinor, hiewVerMajor, hiewVerMinor;

// --- hiew.py
// Reference to the Hiew module
static PyObject *py_hiewmod = NULL;

// Reference to Hiew.Data class
static PyObject *py_hiewdata_cls = NULL;

// Reference to Hiew.PyHiew_ShowScripts
static PyObject *py_hiewmain_func = NULL;

// --- _hiew
// Reference to the Hiew module dictionary
static PyObject *py_hiewcmod = NULL;

// Reference to the Hiew C module dictionary
static PyObject *py_hiewcmod_dict = NULL;


//--------------------------------------------------------------------------
#define _HiewGate_Message(title, msg) HiewGate_Message((HEM_BYTE *)title, (HEM_BYTE *)msg);

//--------------------------------------------------------------------------
void ErrBox(const char *msg, ...)
{
  char tmp[1024];
  va_list va;
  va_start(va, msg);
  _vsnprintf(tmp, sizeof(tmp), msg, va);
  MessageBox(GetConsoleWindow(), tmp, "PyHiew error", MB_OK | MB_ICONERROR);
  va_end(msg);
}

//--------------------------------------------------------------------------
bool InitPyHiew()
{
  // Get HIEW path
  if (!GetModulePath(NULL, pyhiew_path, sizeof(pyhiew_path)))
  {
    ErrBox("Failed to get module handle!");
    return false;
  }

  // Check pyhiew folder
  strncat(pyhiew_path, "\\" PYHIEW_PATH, sizeof(pyhiew_path));
  if (!DirExists(pyhiew_path))
  {
    ErrBox("<%s> directory does not exist!", pyhiew_path);
    return false;
  }

  // Initialize Python library
  Py_Initialize();
  if (!Py_IsInitialized())
  {
    ErrBox("Could not initialize Python!");
    return false;
  }

  // Create the Python module
  py_hiewcmod = Py_InitModule(PYHIEW_CMODNAME, pyhiew_methods);
  if (py_hiewcmod == NULL)
  {
    ErrBox("Failed to initialize PyHiew C module!");
    return false;
  }

  // Get reference to Hiew module dictionary
  py_hiewcmod_dict = PyModule_GetDict(py_hiewcmod);
  if (py_hiewcmod_dict == NULL)
    return false;

  // Add path PyHiew path
  if (!PyAddSysPath(pyhiew_path))
  {
    ErrBox("Could not add <%s> path to sys.path", pyhiew_path);
    return false;
  }

  // Store init.py file path
  strncpy(pyhiew_init_file, pyhiew_path, sizeof(pyhiew_init_file));
  strncat(pyhiew_init_file, "\\" PYHIEW_INIT_SCRIPT, sizeof(pyhiew_init_file));

  // Set the version info in the Hiew module
  PyObject *py_val;
  py_val = Py_BuildValue(
    "{" "s:i" "s:i" "s:i" "s:i" "}",
    "major", hiewVerMajor,
    "minor", hiewVerMinor,
    "sdkmajor", hiewSdkVerMajor,
    "sdkminor", hiewSdkVerMinor);

  PyDict_SetItemString(py_hiewcmod_dict, HEM_PYHIEW_VERSION, py_val);
  Py_DECREF(py_val);

  // Set hiew path
  py_val = PyString_FromString(pyhiew_path);
  PyDict_SetItemString(py_hiewcmod_dict, PYHIEW_VAR_PYHIEWPATH, py_val);
  Py_DECREF(py_val);

  // Set pyhiew version
  py_val = Py_BuildValue(
    "{" "s:s" "s:s" "s:s" "}",
    "major", PYHIEW_VER_MAJOR,
    "minor", PYHIEW_VER_MINOR,
    "build", PYHIEW_VER_BUILD);
  PyDict_SetItemString(py_hiewcmod_dict, PYHIEW_VAR_VERSION, py_val);
  Py_DECREF(py_val);

  return true;
}

//--------------------------------------------------------------------------
// After init is executed and the Python module 'hiew' is imported
// we need to take some references from that helper module
bool PostInitPyHiew()
{
  // Get references to Hiew module
  py_hiewmod = PyImport_ImportModule(PYPYHIEW_MODNAME);
  if (py_hiewmod == NULL)
    return false;

  // Get reference to the Data module
  py_hiewdata_cls = PyObject_GetAttrString(py_hiewmod, PYHIEW_DATA_CLASS);
  if (py_hiewdata_cls == NULL)
    return false;

  // Get reference to the PyHiew_Main()
  py_hiewmain_func = PyObject_GetAttrString(py_hiewmod, PYHIEW_VAR_MAIN_FUNC);
  if (py_hiewmain_func == NULL)
    return false;

  return true;
}

//--------------------------------------------------------------------------
void DeinitPyHiew()
{
  //
  // hiew.py
  //
  if (py_hiewdata_cls != NULL)
  {
    Py_DECREF(py_hiewdata_cls);
    py_hiewdata_cls = NULL;
  }

  if (py_hiewmain_func != NULL)
  {
    Py_DECREF(py_hiewmain_func);
    py_hiewmain_func = NULL;
  }

  if (py_hiewmod != NULL)
  {
    Py_DECREF(py_hiewmod);
    py_hiewmod = NULL;
  }

  //
  // _hiew
  //
  py_hiewcmod_dict = NULL;
  py_hiewcmod = NULL;

  Py_Finalize();
  g_init_executed = false;
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_Message(PyObject *, PyObject *args)
{
  HEM_BYTE *title, *msg;
  if (!PyArg_ParseTuple(args, "ss", &title, &msg))
    Py_RETURN_NONE;

  return PyInt_FromLong(HiewGate_Message(title, msg));
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_GetData(PyObject *, PyObject *args)
{
  HIEWGATE_GETDATA data;
  if (HiewGate_GetData(&data) != HEM_OK)
    Py_RETURN_NONE;

  return PyObject_CallFunctionObjArgs(py_hiewdata_cls,
    PyInt_FromLong(data.hemHandle),
    PyInt_FromLong(data.callId),
    PyString_FromString((char *)data.filename),
    PyLong_FromUnsignedLongLong(data.offsetCurrent),
    PyLong_FromUnsignedLongLong(data.offsetCurrent),
    PyLong_FromUnsignedLongLong(data.offsetMark1),
    PyLong_FromUnsignedLongLong(data.offsetMark2),
    PyLong_FromUnsignedLongLong(data.sizeMark),
    NULL);
}

//--------------------------------------------------------------------------
// Wraps the HiewGate_Menu() function
struct py_hiewcontrol
{
  PyObject *pycontrol;
  char *title;
  int width;
  HEM_FNKEYS keys;
  char **lines;
  int linescount;
  bool is_window;

  void reset()
  {
    pycontrol = NULL;
    title = 0;
    linescount = 0;
    width = 0;
    memset(&keys, 0, sizeof(keys));
    lines = 0;
  }

  py_hiewcontrol()
  {
    reset();
  }

  bool create(
    PyObject *_pycontrol,
    char *_title,
    int _width,
    char *_main_keys,
    char *_alt_keys,
    char *_ctrl_keys,
    char *_shift_keys,
    PyObject *pylines,
    bool _is_window)
  {
    do
    {
      // copy control type
      is_window = _is_window;

      // Build the lines
      if (   !PyList_Check(pylines)
          || (linescount = (int)PyList_Size(pylines)) == 0 )
      {
        return false;
      }

      // 1. Allocate first dimention
      lines = (char **) malloc(sizeof(char **) * linescount);

      // 2. Allocate individual lines
      for (int i=0;i<linescount;i++)
        lines[i] = strdup(PyString_AS_STRING(PyList_GetItem(pylines, i)));

      // Title
      title = strdup(_title);
      width = _width;

      // Keys
      keys.main  = (HEM_BYTE *) strdup(_main_keys);
      keys.alt   = (HEM_BYTE *) strdup(_alt_keys);
      keys.ctrl  = (HEM_BYTE *) strdup(_ctrl_keys);
      keys.shift = (HEM_BYTE *) strdup(_shift_keys);

      // Take reference to the object
      pycontrol = _pycontrol;
      Py_INCREF(pycontrol);

      return true;
    } while (false);
    return false;
  }

  void clear()
  {
    // Clear lines
    if (lines != NULL)
    {
      for (int i=0;i<linescount;i++)
        free(lines[i]);

      free(lines);
      lines = NULL;
    }

    // Release reference to the menu class
    if (pycontrol != NULL)
    {
      Py_DECREF(pycontrol);
      pycontrol = NULL;
    }

    // Free key lines
    if (keys.main != NULL)
      free(keys.main);
    if (keys.alt != NULL)
      free(keys.alt);
    if (keys.ctrl != NULL)
      free(keys.ctrl);
    if (keys.shift != NULL)
      free(keys.shift);

    // Free title
    if (title != NULL)
    {
      free(title);
      title = NULL;
    }
  }

  int show(int sel_line, HEM_UINT *fnKey)
  {
    // Just check the title to see if the menu is setup already
    if (title == NULL)
      return HEM_ERR_INVALID_ARGUMENT;

    if (is_window)
    {
      return HiewGate_Window(
        (HEM_BYTE *)title,
        (HEM_BYTE **)lines,
        linescount,
        width,
        &keys,
        fnKey);
    }
    else
    {
      return HiewGate_Menu(
        (HEM_BYTE *)title,
        (HEM_BYTE **)lines,
        linescount,
        width,
        sel_line + 1,
        &keys,
        fnKey,
        NULL,
        NULL);
    }
  }
};

//--------------------------------------------------------------------------
// Compiles a control
static PyObject *py_ControlCreate(PyObject *, PyObject *args)
{
  char *title, *main_keys, *alt_keys, *ctrl_keys, *shift_keys;
  int width, is_window;
  PyObject *py_lines, *py_obj;

  // ControlCreate(self, title, lines, width, is_window, main_keys, alt_keys, ctrl_keys, shift_keys)
  if (!PyArg_ParseTuple(args,
                       "OsOiissss",
                       &py_obj,
                       &title,
                       &py_lines,
                       &width,
                       &is_window,
                       &main_keys,
                       &alt_keys,
                       &ctrl_keys,
                       &shift_keys))
  {
    Py_RETURN_NONE;
  }
  py_hiewcontrol *control = new py_hiewcontrol();
  if (!control->create(py_obj, title, width, main_keys, alt_keys, ctrl_keys, shift_keys, py_lines, is_window == 1))
  {
    delete control;
    Py_RETURN_NONE;
  }
  return PyCObject_FromVoidPtr(control, NULL);
}

//--------------------------------------------------------------------------
static PyObject *py_ControlShow(PyObject *, PyObject *args)
{
  // ControlShow(menu_obj, sel_line) -> (r, fnKey)
  PyObject *py_control;
  int sel_line;
  if (!PyArg_ParseTuple(args, "Oi", &py_control, &sel_line) || !PyCObject_Check(py_control))
    Py_RETURN_NONE;

  // Get the menu pointer
  py_hiewcontrol *control = (py_hiewcontrol *) PyCObject_AsVoidPtr(py_control);

  HEM_UINT fnKey;
  int r = control->show(sel_line, &fnKey);
  return Py_BuildValue("iI", r, fnKey);
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_GetLastResult(PyObject *, PyObject *args)
{
  // HiewGate_GetLastResult() -> err
  return PyInt_FromLong(HiewGate_GetLastResult());
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_FileOpenForWrite(PyObject *, PyObject *args)
{
  // HiewGate_FileOpenForWrite() -> err
  return PyInt_FromLong(HiewGate_FileOpenForWrite());
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_MarkBlock(PyObject *, PyObject *args)
{
  HEM_QWORD offset1, offset2;
  // HiewGate_MarkBlock(offset1, offset2) -> err
  if (!PyArg_ParseTuple(args, "KK", &offset1, &offset2))
    Py_RETURN_NONE;

  return PyInt_FromLong(HiewGate_MarkBlock(offset1, offset2));
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_UnmarkBlock(PyObject *, PyObject *)
{
  return PyInt_FromLong(HiewGate_UnmarkBlock());
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_Find(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  int flags;
  PyObject *py_data;
  // Find(flags, offset, data) -> None | offset
  if (!PyArg_ParseTuple(args, "iKO", &flags, &offset, &py_data))
    Py_RETURN_NONE;

  // Get data buffer and length
  Py_ssize_t data_len;
  char *data;
  if (PyString_AsStringAndSize(py_data, &data, &data_len) == -1)
    Py_RETURN_NONE;

  // Sanitize
  data_len = min(data_len, HEM_MAX_FINDSTR_LEN);

  // Read
  offset = HiewGate_Find(
              flags,
              offset,
              (HEM_BYTE *)data,
              data_len,
              NULL);
  if (offset == HEM_OFFSET_NOT_FOUND)
     Py_RETURN_NONE;
   else
     return PyLong_FromUnsignedLongLong(offset);
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_FindNext(PyObject *, PyObject *)
{
  // HiewGate_FindNext() -> None | offset
  HEM_QWORD offset = HiewGate_FindNext();
  if (offset == HEM_OFFSET_NOT_FOUND)
    Py_RETURN_NONE;
  else
    return PyLong_FromUnsignedLongLong(offset);
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_FileRead(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  HEM_UINT bytes;
  // FileRead(offset, bytes) -> (>0 ok|<0 bad, buffer|None)
  if (!PyArg_ParseTuple(args, "KI", &offset, &bytes))
    Py_RETURN_NONE;

  // Allocate buffer or return None if no size / no mem
  HEM_BYTE *buf;
  if (bytes == 0 || (buf = (HEM_BYTE *) malloc(bytes)) == NULL)
  {
    Py_INCREF(Py_None);
    return Py_BuildValue("(iO)", 0, Py_None);
  }

  // Read
  int rc = HiewGate_FileRead(offset, bytes, buf);

  PyObject *py_buf;
  if (rc <= 0)
  {
    py_buf = Py_None;
    Py_INCREF(Py_None);
  }
  else
    py_buf = PyString_FromStringAndSize((char *)buf, rc);

  free(buf);

  return Py_BuildValue("(iO)", rc, py_buf);
}

//--------------------------------------------------------------------------
static PyObject *py_ReturnOffset(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  // ReturnOffset(offset) -> None
  if (PyArg_ParseTuple(args, "K", &offset))
  {
    g_pHemCall->returnActionFlag = HEM_RETURN_SETOFFSET;
    g_pHemCall->returnOffset = offset;
  }
  Py_RETURN_NONE;
}

//--------------------------------------------------------------------------
static PyObject *py_ReturnMode(PyObject *, PyObject *args)
{
  int mode;
  // ReturnMode(mode) -> None
  if (PyArg_ParseTuple(args, "i", &mode))
  {
    g_pHemCall->returnActionFlag = HEM_RETURN_SETMODE;
    g_pHemCall->returnMode = mode;
  }
  Py_RETURN_NONE;
}

//--------------------------------------------------------------------------
static PyObject *py_ReturnCode(PyObject *, PyObject *args)
{
  int rc;
  // ReturnMode(mode) -> None
  if (PyArg_ParseTuple(args, "i", &rc))
    g_HemEntryPointReturn = rc;

  Py_RETURN_NONE;
}

//--------------------------------------------------------------------------
static PyObject *py_ReturnReload(PyObject *, PyObject *args)
{
  // ReturnReload() -> None
  g_pHemCall->returnActionFlag = HEM_RETURN_FILERELOAD;
  Py_RETURN_NONE;
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_IsKeyBreak(PyObject *, PyObject *args)
{
  return PyInt_FromLong(HiewGate_IsKeyBreak() == HEM_KEYBREAK);
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_MessageWaitOpen(PyObject *, PyObject *args)
{
  HEM_BYTE *msg;
  if (!PyArg_ParseTuple(args, "s", &msg))
    Py_RETURN_NONE;
  else
    return PyInt_FromLong(HiewGate_MessageWaitOpen(msg[0] == '\0' ? NULL : msg));
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_MessageWaitClose(PyObject *, PyObject *args)
{
  HiewGate_MessageWaitClose();
  Py_RETURN_NONE;
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_SetErrorMsg(PyObject *, PyObject *args)
{
  HEM_BYTE *error_msg;
  if (!PyArg_ParseTuple(args, "s", &error_msg))
    Py_RETURN_NONE;
  return PyInt_FromLong(HiewGate_SetErrorMsg(error_msg));
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_FileWrite(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  PyObject *py_buf;
  // FileWrite(offset, buffer) -> nbwritten
  if (!PyArg_ParseTuple(args, "KO", &offset, &py_buf))
    Py_RETURN_NONE;

  // Extract internal buffer
  Py_ssize_t bytes;
  char *buf;
  if (PyString_AsStringAndSize(py_buf, &buf, &bytes)==-1)
    Py_RETURN_NONE;

  // Write
  int rc = HiewGate_FileWrite(offset, bytes, (HEM_BYTE *)buf);

  return PyInt_FromLong(rc);
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_GetString(PyObject *, PyObject *args)
{
  int str_len;
  HEM_BYTE *title;
  char *in_str;
  // GetString(title, max_input, init_val) -> string or None (if user pressed ESC)
  if (!PyArg_ParseTuple(args, "sis", &title, &str_len, &in_str) || str_len == 0)
    Py_RETURN_NONE;

  // Allocate memory
  char *buf = (char *)malloc(str_len + 1);
  // Copy initial value
  strncpy(buf, in_str, str_len);

  // Call hiew
  PyObject *py_ret;
  int rc = HiewGate_GetString(title, (HEM_BYTE *)buf, str_len);
  if (rc == HEM_INPUT_ESC)
  {
    py_ret = Py_None;
    Py_INCREF(py_ret);
  }
  else
  {
    py_ret = PyString_FromString(buf);
  }
  free(buf);
  return py_ret;
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_GetStringDual(PyObject *, PyObject *args)
{
  int max_input;
  HEM_BYTE *title;
  PyObject *init_val;
  int bOnHexLine;
  // GetStringDual(title, max_input, init_val, on_hex_line) ->
  //    None (if user pressed ESC)
  // or
  //    (OnHex=Boolean, string)
  if (!PyArg_ParseTuple(args, "siOi", &title, &max_input, &init_val, &bOnHexLine) || max_input == 0)
    Py_RETURN_NONE;

  // Get the input string and its length
  char buf[HEM_MAX_DUALSTR_LEN + 1];
  Py_ssize_t str_len;
  char *in_str;
  if (PyString_AsStringAndSize(init_val, &in_str, &str_len) == -1)
    Py_RETURN_NONE;

  // Normalize values
  max_input = min(HEM_MAX_DUALSTR_LEN, max_input);
  str_len = min(HEM_MAX_DUALSTR_LEN, str_len);

  // Copy initial value
  memcpy(buf, in_str, str_len);

  // Call hiew
  int rc = HiewGate_GetStringDual(
              title,
              (HEM_BYTE *)buf,
              max_input,
              str_len,
              &bOnHexLine);

  if (rc == HEM_INPUT_ESC)
    Py_RETURN_NONE;
  else
    return Py_BuildValue("(iO)", bOnHexLine != 0, PyString_FromStringAndSize(buf, rc));
}

//--------------------------------------------------------------------------
static PyObject *py_MessageBox(PyObject *, PyObject *args)
{
  char *title, *message;
  if (PyArg_ParseTuple(args, "ss", &title, &message))
    MessageBox(GetConsoleWindow(), message, title, MB_OK | MB_ICONINFORMATION);
  Py_RETURN_NONE;
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_GetFilename(PyObject *, PyObject *args)
{
  HEM_BYTE *title, *file_name;
  // GetFilename(title, filename) -> string or None (if user pressed ESC)
  if (!PyArg_ParseTuple(args, "ss", &title, &file_name))
    Py_RETURN_NONE;

  char buf[HEM_FILENAME_MAXLEN];
  strncpy(buf, (char *) file_name, HEM_FILENAME_MAXLEN);

  // Call hiew
  int rc = HiewGate_GetFilename(title, (HEM_BYTE *)buf);
  if (rc == HEM_INPUT_ESC)
    Py_RETURN_NONE;
  else
    return PyString_FromString(buf);
}

//--------------------------------------------------------------------------
static PyObject *py_ControlClear(PyObject *, PyObject *args)
{
  // ControlClear(control_obj) -> None
  PyObject *py_control;
  if (PyArg_ParseTuple(args, "O", &py_control) && PyCObject_Check(py_control))
  {
    // Get the menu pointer
    py_hiewcontrol *control = (py_hiewcontrol *) PyCObject_AsVoidPtr(py_control);
    control->clear();
    delete control;
  }
  Py_RETURN_NONE;
}

//--------------------------------------------------------------------------
//int HiewGate_Names_Clear();
static PyObject *py_HiewGate_Names_Clear(PyObject *, PyObject *)
{
  return PyInt_FromLong(HiewGate_Names_Clear());
}

//--------------------------------------------------------------------------
//int HiewGate_Names_CountName();
static PyObject *py_HiewGate_Names_CountName(PyObject *, PyObject *)
{
  return PyInt_FromLong(HiewGate_Names_CountName());
}

//--------------------------------------------------------------------------
//int HiewGate_Names_FindName(HEM_BYTE *name, int *bLocal)
static PyObject *py_HiewGate_Names_FindName(PyObject *, PyObject *args)
{
  HEM_BYTE *name;
  if (!PyArg_ParseTuple(args, "s", &name))
    Py_RETURN_NONE;
  // Returns a tuple(offset, bIsLocal)
  int bLocal;
  HEM_QWORD offs = HiewGate_Names_FindName(name, &bLocal);
  if (offs == HEM_OFFSET_NOT_FOUND)
    Py_RETURN_NONE;
  else
    return Py_BuildValue("Ki", offs, bLocal != 0);
}

//--------------------------------------------------------------------------
//int HiewGate_Names_AddGlobalComment(HEM_QWORD offset, HEM_BYTE *comment);
static PyObject *py_HiewGate_Names_AddGlobalComment(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  HEM_BYTE *name;
  if (!PyArg_ParseTuple(args, "Ks", &offset, &name))
    Py_RETURN_NONE;

  return PyInt_FromLong(HiewGate_Names_AddGlobalComment(offset, name));
}

//--------------------------------------------------------------------------
//int HiewGate_Names_AddLocalComment(HEM_QWORD offset, HEM_BYTE *comment);
static PyObject *py_HiewGate_Names_AddLocalComment(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  HEM_BYTE *name;
  if (!PyArg_ParseTuple(args, "Ks", &offset, &name))
    Py_RETURN_NONE;

  return PyInt_FromLong(HiewGate_Names_AddLocalComment(offset, name));
}

//--------------------------------------------------------------------------
//int HiewGate_Names_AddLocal(HEM_QWORD offset, HEM_BYTE *name);
static PyObject *py_HiewGate_Names_AddLocal(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  HEM_BYTE *name;
  if (!PyArg_ParseTuple(args, "Ks", &offset, &name))
    Py_RETURN_NONE;

  return PyInt_FromLong(HiewGate_Names_AddLocal(offset, name));
}

//--------------------------------------------------------------------------
//int HiewGate_Names_AddGlobal(HEM_QWORD offset, HEM_BYTE *name);
static PyObject *py_HiewGate_Names_AddGlobal(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  HEM_BYTE *name;
  if (!PyArg_ParseTuple(args, "Ks", &offset, &name))
    Py_RETURN_NONE;

  return PyInt_FromLong(HiewGate_Names_AddGlobal(offset, name));
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_Names_DelGlobalComment(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  if (!PyArg_ParseTuple(args, "K", &offset))
    return NULL;
  return PyInt_FromLong(HiewGate_Names_DelGlobalComment(offset));
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_Names_DelLocalComment(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  if (!PyArg_ParseTuple(args, "K", &offset))
    return NULL;
  return PyInt_FromLong(HiewGate_Names_DelLocalComment(offset));
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_Names_DelLocal(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  if (!PyArg_ParseTuple(args, "K", &offset))
    return NULL;
  return PyInt_FromLong(HiewGate_Names_DelLocal(offset));
}

//--------------------------------------------------------------------------
static PyObject *py_HiewGate_Names_DelGlobal(PyObject *, PyObject *args)
{
  HEM_QWORD offset;
  if (!PyArg_ParseTuple(args, "K", &offset))
    return NULL;
  return PyInt_FromLong(HiewGate_Names_DelGlobal(offset));
}

//--------------------------------------------------------------------------
//int HiewGate_Names_CountLocal();
static PyObject *py_HiewGate_Names_CountLocal(PyObject *, PyObject *)
{
  return PyInt_FromLong(HiewGate_Names_CountLocal());
}

//--------------------------------------------------------------------------
//int HiewGate_Names_CountGlobal();
static PyObject *py_HiewGate_Names_CountGlobal(PyObject *, PyObject *)
{
  return PyInt_FromLong(HiewGate_Names_CountGlobal());
}

//--------------------------------------------------------------------------
static PyObject *py_od(PyObject *, PyObject *args)
{
  // od(msg) -> None
  char *msg;
  if (PyArg_ParseTuple(args, "s", &msg))
    OutputDebugString(msg);
  Py_RETURN_NONE;
}


//--------------------------------------------------------------------------
//                       HIEW callbacks
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
// Called when Hiew loads the module
int HEM_EXPORT Hem_Load(HIEWINFO_TAG *hiewInfo)
{
  // Get Hiew version
  hiewVerMajor    = hiewInfo->hiewVerMajor;
  hiewVerMinor    = hiewInfo->hiewVerMinor;
  hiewSdkVerMajor = hiewInfo->sdkVerMajor;
  hiewSdkVerMinor = hiewInfo->sdkVerMinor;

  if (!InitPyHiew())
    return HEM_ERROR;

  // Initialize Hiew
  HiewGate_Set(hiewInfo);
  hiewInfo->hemInfo= &hem_pyhiew;

  return HEM_OK;
}

//--------------------------------------------------------------------------
int HEM_API Hem_EntryPoint(HEMCALL_TAG *hemCall)
{
  if (hemCall->cbSize < sizeof(HEMCALL_TAG))
    return HEM_ERROR;

  // Save HEMMCALLTAG
  g_pHemCall = hemCall;

  // Clear return value code
  g_HemEntryPointReturn = HEM_OK;

  // Execute the init file once
  if (!g_init_executed)
  {
    // Run init file and post initialize
    if (!PyRunFile(pyhiew_init_file) || !PostInitPyHiew())
    {
      _getch();
      _HiewGate_Message("Error", "Failed while executing init script!");
      return HEM_OK;
    }
    g_init_executed = true;
  }

#ifndef TESTMODE
  PyObject *py_result = PyObject_CallFunction(py_hiewmain_func, NULL);
  Py_XDECREF(py_result);
#else
  char fn[MAX_PATH] = {0};
  strncpy(fn, pyhiew_path, sizeof(fn));
  strncat(fn, PYHIEW_TESTSCRIPT, sizeof(fn));
  if (!PyRunFile(fn))
    _getch();
#endif
  return g_HemEntryPointReturn;
}

//--------------------------------------------------------------------------
int HEM_API Hem_Unload()
{
  DeinitPyHiew();
  return HEM_OK;
}

//--------------------------------------------------------------------------
static HEMINFO_TAG hem_pyhiew =
{
  // Structure size
  sizeof(HEMINFO_TAG),
  sizeof(int),
  0, // reserved
  // Version info
  HEM_SDK_VERSION_MAJOR,
  HEM_SDK_VERSION_MINOR,
  HEM_PYHIEW_VERSION_MAJOR,
  HEM_PYHIEW_VERSION_MINOR,
  HEM_FLAG_MODEMASK|HEM_FLAG_FILEMASK, // For all files
  0, // reserved
  Hem_EntryPoint, // Entry
  Hem_Unload, // Unload
  NULL, // Hem2HemGate
  0, // reserved
  0, // reserved
  0, // reserved
  0, // reserved
  "PyHiew", // Short name
  "Python Hiew", // Name
  "******************************************",
  "PyHiew v" PYHIEW_VER_MAJOR "." PYHIEW_VER_MINOR "." PYHIEW_VER_BUILD " (c) Elias Bachaalany",
  "******************************************"
};

//--------------------------------------------------------------------------
#define DEF_PY_METHOD(name) {#name, py_##name, METH_VARARGS, ""}
static PyMethodDef pyhiew_methods[] =
{
  // Control functionality: Menu / Window
  DEF_PY_METHOD(ControlCreate),
  DEF_PY_METHOD(ControlShow),
  DEF_PY_METHOD(ControlClear),

  // Debugging
  DEF_PY_METHOD(od),
  DEF_PY_METHOD(MessageBox),

  // Information
  DEF_PY_METHOD(HiewGate_GetData),
  DEF_PY_METHOD(HiewGate_GetLastResult),

  // Block
  DEF_PY_METHOD(HiewGate_MarkBlock),
  DEF_PY_METHOD(HiewGate_UnmarkBlock),

  // Input / Output
  DEF_PY_METHOD(HiewGate_Message),
  DEF_PY_METHOD(HiewGate_GetString),
  DEF_PY_METHOD(HiewGate_GetStringDual),
  DEF_PY_METHOD(HiewGate_GetFilename),
  DEF_PY_METHOD(HiewGate_SetErrorMsg),
  DEF_PY_METHOD(HiewGate_MessageWaitClose),
  DEF_PY_METHOD(HiewGate_MessageWaitOpen),
  DEF_PY_METHOD(HiewGate_IsKeyBreak),

  // File I/O
  DEF_PY_METHOD(HiewGate_FileOpenForWrite),
  DEF_PY_METHOD(HiewGate_FileRead),
  DEF_PY_METHOD(HiewGate_FileWrite),
  DEF_PY_METHOD(HiewGate_Find),
  DEF_PY_METHOD(HiewGate_FindNext),

  // Names
  DEF_PY_METHOD(HiewGate_Names_Clear),
  DEF_PY_METHOD(HiewGate_Names_AddLocal),
  DEF_PY_METHOD(HiewGate_Names_AddGlobal),
  DEF_PY_METHOD(HiewGate_Names_AddLocalComment),
  DEF_PY_METHOD(HiewGate_Names_AddGlobalComment),
  DEF_PY_METHOD(HiewGate_Names_CountLocal),
  DEF_PY_METHOD(HiewGate_Names_CountGlobal),
  DEF_PY_METHOD(HiewGate_Names_CountName),
  DEF_PY_METHOD(HiewGate_Names_DelGlobalComment),
  DEF_PY_METHOD(HiewGate_Names_DelLocalComment),
  DEF_PY_METHOD(HiewGate_Names_DelLocal),
  DEF_PY_METHOD(HiewGate_Names_DelGlobal),
  DEF_PY_METHOD(HiewGate_Names_FindName),

  // Return
  DEF_PY_METHOD(ReturnOffset),
  DEF_PY_METHOD(ReturnMode),
  DEF_PY_METHOD(ReturnCode),
  DEF_PY_METHOD(ReturnReload),

  {NULL, NULL, 0, NULL}
};
