#include <quickjs.h>
#include <cutils.h>
#include <string.h>
#include "defines.h"
#include <portmidi.h>
#include <porttime.h>

/* ---- PmError -> JS exception ------------------------------------------ */

static JSValue
js_portmidi_throw(JSContext* ctx, PmError err) {
  JSValue e = JS_NewError(ctx);
  char msg[512];

  if(err == pmHostError) {
    char host[PM_HOST_ERROR_MSG_LEN];
    Pm_GetHostErrorText(host, sizeof(host));
    snprintf(msg, sizeof(msg), "%s: %s", Pm_GetErrorText(err), host);
  } else {
    snprintf(msg, sizeof(msg), "%s", Pm_GetErrorText(err));
  }

  JS_SetPropertyStr(ctx, e, "name", JS_NewString(ctx, "PmError"));
  JS_SetPropertyStr(ctx, e, "message", JS_NewString(ctx, msg));
  JS_SetPropertyStr(ctx, e, "code", JS_NewInt32(ctx, err));
  return JS_Throw(ctx, e);
}

/* ---- shared ArrayBuffer/TypedArray-or-view argument helper ------------ */

static uint8_t*
js_portmidi_get_buffer(JSContext* ctx, JSValueConst val, size_t* plen) {
  size_t byte_offset = 0, byte_length = 0, bytes_per_element = 0;
  JSValue buf = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bytes_per_element);

  if(!JS_IsException(buf)) {
    size_t ab_size = 0;
    uint8_t* ab_data = JS_GetArrayBuffer(ctx, &ab_size, buf);
    JS_FreeValue(ctx, buf);
    if(!ab_data)
      return NULL;
    *plen = byte_length;
    return ab_data + byte_offset;
  }

  /* not a typed array/DataView: discard the probe exception, try a plain ArrayBuffer */
  JS_FreeValue(ctx, JS_GetException(ctx));
  return JS_GetArrayBuffer(ctx, plen, val);
}

/* ---- module-level functions -------------------------------------------- */

enum {
  FUNC_INITIALIZE = 0,
  FUNC_TERMINATE,
  FUNC_COUNTDEVICES,
  FUNC_GETDEFAULTINPUTDEVICEID,
  FUNC_GETDEFAULTOUTPUTDEVICEID,
  FUNC_GETDEVICEINFO,
  FUNC_TIME,
  FUNC_GETERRORTEXT,
  FUNC_CHANNEL,
};

static JSClassID js_pmdeviceinfo_class_id;
static JSValue pmdeviceinfo_proto, pmdeviceinfo_ctor;

static JSValue js_pmdeviceinfo_wrap(JSContext* ctx, const PmDeviceInfo* info);

static JSValue
js_portmidi_function(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSValue ret = JS_UNDEFINED;

  switch(magic) {
    case FUNC_INITIALIZE: {
      PmError err = Pm_Initialize();
      if(err != pmNoError)
        return js_portmidi_throw(ctx, err);
      break;
    }
    case FUNC_TERMINATE: {
      PmError err = Pm_Terminate();
      if(err != pmNoError)
        return js_portmidi_throw(ctx, err);
      break;
    }
    case FUNC_COUNTDEVICES: {
      ret = JS_NewInt32(ctx, Pm_CountDevices());
      break;
    }
    case FUNC_GETDEFAULTINPUTDEVICEID: {
      ret = JS_NewInt32(ctx, Pm_GetDefaultInputDeviceID());
      break;
    }
    case FUNC_GETDEFAULTOUTPUTDEVICEID: {
      ret = JS_NewInt32(ctx, Pm_GetDefaultOutputDeviceID());
      break;
    }
    case FUNC_GETDEVICEINFO: {
      int32_t id = -1;
      const PmDeviceInfo* info;

      if(argc > 0)
        JS_ToInt32(ctx, &id, argv[0]);

      if(!(info = Pm_GetDeviceInfo(id)))
        return JS_NULL;

      ret = js_pmdeviceinfo_wrap(ctx, info);
      break;
    }
    case FUNC_TIME: {
      ret = JS_NewInt32(ctx, Pt_Time());
      break;
    }
    case FUNC_GETERRORTEXT: {
      int32_t code = 0;

      if(argc > 0)
        JS_ToInt32(ctx, &code, argv[0]);

      ret = JS_NewString(ctx, Pm_GetErrorText((PmError)code));
      break;
    }
    case FUNC_CHANNEL: {
      int32_t n = 0;

      if(argc > 0)
        JS_ToInt32(ctx, &n, argv[0]);

      ret = JS_NewInt32(ctx, Pm_Channel(n));
      break;
    }
  }

  return ret;
}

/* ---- PmDeviceInfo: read-only value class -------------------------------
 *
 * Owns copies of the interf/name strings: PortMidi's own PmDeviceInfo
 * pointer is only guaranteed valid between Pm_Initialize()/Pm_Terminate(),
 * so a wrapped instance must not keep pointing into it.
 */

typedef struct {
  char* interf;
  char* name;
  int input;
  int output;
  int opened;
} JSPmDeviceInfo;

static JSValue
js_pmdeviceinfo_wrap(JSContext* ctx, const PmDeviceInfo* info) {
  JSPmDeviceInfo* di;
  JSValue obj;

  if(!(di = js_mallocz(ctx, sizeof(JSPmDeviceInfo))))
    return JS_EXCEPTION;

  if(info->interf)
    di->interf = js_strdup(ctx, info->interf);
  if(info->name)
    di->name = js_strdup(ctx, info->name);

  di->input = info->input;
  di->output = info->output;
  di->opened = info->opened;

  obj = JS_NewObjectProtoClass(ctx, pmdeviceinfo_proto, js_pmdeviceinfo_class_id);

  if(JS_IsException(obj)) {
    js_free(ctx, di->interf);
    js_free(ctx, di->name);
    js_free(ctx, di);
    return obj;
  }

  JS_SetOpaque(obj, di);
  return obj;
}

enum {
  PMDEVICEINFO_INTERF = 0,
  PMDEVICEINFO_NAME,
  PMDEVICEINFO_INPUT,
  PMDEVICEINFO_OUTPUT,
  PMDEVICEINFO_OPENED,
};

static JSValue
js_pmdeviceinfo_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSPmDeviceInfo* di;

  if(!(di = JS_GetOpaque2(ctx, this_val, js_pmdeviceinfo_class_id)))
    return JS_EXCEPTION;

  switch(magic) {
    case PMDEVICEINFO_INTERF: return di->interf ? JS_NewString(ctx, di->interf) : JS_NULL;
    case PMDEVICEINFO_NAME: return di->name ? JS_NewString(ctx, di->name) : JS_NULL;
    case PMDEVICEINFO_INPUT: return JS_NewBool(ctx, di->input);
    case PMDEVICEINFO_OUTPUT: return JS_NewBool(ctx, di->output);
    case PMDEVICEINFO_OPENED: return JS_NewBool(ctx, di->opened);
  }

  return JS_UNDEFINED;
}

static void
js_pmdeviceinfo_finalizer(JSRuntime* rt, JSValue val) {
  JSPmDeviceInfo* di;

  if((di = JS_GetOpaque(val, js_pmdeviceinfo_class_id))) {
    js_free_rt(rt, di->interf);
    js_free_rt(rt, di->name);
    js_free_rt(rt, di);
  }
}

static JSClassDef js_pmdeviceinfo_class = {
    .class_name = "PmDeviceInfo",
    .finalizer = js_pmdeviceinfo_finalizer,
};

static const JSCFunctionListEntry js_pmdeviceinfo_funcs[] = {
    JS_CGETSET_MAGIC_DEF("interf", js_pmdeviceinfo_get, 0, PMDEVICEINFO_INTERF),
    JS_CGETSET_MAGIC_FLAGS_DEF("name", js_pmdeviceinfo_get, 0, PMDEVICEINFO_NAME, JS_PROP_ENUMERABLE),
    JS_CGETSET_MAGIC_DEF("input", js_pmdeviceinfo_get, 0, PMDEVICEINFO_INPUT),
    JS_CGETSET_MAGIC_DEF("output", js_pmdeviceinfo_get, 0, PMDEVICEINFO_OUTPUT),
    JS_CGETSET_MAGIC_DEF("opened", js_pmdeviceinfo_get, 0, PMDEVICEINFO_OPENED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PmDeviceInfo", JS_PROP_CONFIGURABLE),
};

/* ---- devices: live exotic array view over Pm_GetDeviceInfo() ---------- */

static JSClassID js_pmdevices_class_id;
static JSValue pmdevices_proto, pmdevices_obj;

static BOOL
js_pmdevices_get_own_property(JSContext* ctx, JSPropertyDescriptor* pdesc, JSValueConst obj, JSAtom prop) {

  if(prop & (1 << 31)) {
    int32_t index;

    if((index = prop & (~(1 << 31))) >= 0) {
      const PmDeviceInfo* info = Pm_GetDeviceInfo(index);

      if(info)
        if(pdesc) {
          pdesc->flags = JS_PROP_ENUMERABLE;
          pdesc->value = js_pmdeviceinfo_wrap(ctx, info);
          pdesc->getter = JS_UNDEFINED;
          pdesc->setter = JS_UNDEFINED;
        }

      return info != NULL;
    }
  }

  const char* key;
  BOOL ret = FALSE;

  if((key = JS_AtomToCString(ctx, prop))) {
    if(!strcmp(key, "length")) {
      if(pdesc) {
        pdesc->flags = JS_PROP_ENUMERABLE;
        pdesc->value = JS_NewUint32(ctx, Pm_CountDevices());
        pdesc->getter = JS_UNDEFINED;
        pdesc->setter = JS_UNDEFINED;
      }

      ret = TRUE;
    }

    JS_FreeCString(ctx, key);
  }

  return ret;
}

static int
js_pmdevices_get_own_property_names(JSContext* ctx, JSPropertyEnum** ptab, uint32_t* plen, JSValueConst obj) {
  uint32_t i, len = Pm_CountDevices();
  JSPropertyEnum* props;

  if((props = js_malloc(ctx, sizeof(JSPropertyEnum) * len))) {
    for(i = 0; i < len; i++) {
      props[i].is_enumerable = TRUE;
      props[i].atom = JS_NewAtomUInt32(ctx, i);
    }

    *ptab = props;
    *plen = len;
  }

  return 0;
}

static void
js_pmdevices_finalizer(JSRuntime* rt, JSValue val) {
  /* no opaque data: state lives entirely in PortMidi's own device table */
}

static JSClassExoticMethods js_pmdevices_exotic_methods = {
    .get_own_property = js_pmdevices_get_own_property,
    .get_own_property_names = js_pmdevices_get_own_property_names,
};

static JSClassDef js_pmdevices_class = {
    .class_name = "PmDevices",
    .finalizer = js_pmdevices_finalizer,
    .exotic = &js_pmdevices_exotic_methods,
};

static const JSCFunctionListEntry js_pmdevices_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PmDevices", JS_PROP_CONFIGURABLE),
};

/* ---- PortMidiStream: opaque-pointer class, opened via static factories */

static JSClassID js_portmidistream_class_id;
static JSValue portmidistream_proto, portmidistream_ctor;

typedef struct {
  PortMidiStream* stream;
  int32_t deviceId;
  BOOL isOutput;
} JSPortMidiStream;

enum {
  STATIC_OPENINPUT = 0,
  STATIC_OPENOUTPUT,
};

static JSValue
js_portmidistream_open(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  int32_t deviceId = -1;
  int32_t bufferSize = magic == STATIC_OPENOUTPUT ? 256 : 4096;
  int32_t latency = 0;
  PortMidiStream* stream = NULL;
  PmError err;
  JSPortMidiStream* s;
  JSValue obj;

  if(argc > 0)
    JS_ToInt32(ctx, &deviceId, argv[0]);
  if(argc > 1)
    JS_ToInt32(ctx, &bufferSize, argv[1]);
  if(magic == STATIC_OPENOUTPUT && argc > 2)
    JS_ToInt32(ctx, &latency, argv[2]);

  if(magic == STATIC_OPENOUTPUT)
    err = Pm_OpenOutput(&stream, deviceId, NULL, bufferSize, NULL, NULL, latency);
  else
    err = Pm_OpenInput(&stream, deviceId, NULL, bufferSize, NULL, NULL);

  if(err != pmNoError)
    return js_portmidi_throw(ctx, err);

  if(!(s = js_mallocz(ctx, sizeof(JSPortMidiStream)))) {
    Pm_Close(stream);
    return JS_EXCEPTION;
  }

  s->stream = stream;
  s->deviceId = deviceId;
  s->isOutput = magic == STATIC_OPENOUTPUT;

  obj = JS_NewObjectProtoClass(ctx, portmidistream_proto, js_portmidistream_class_id);

  if(JS_IsException(obj)) {
    Pm_Close(stream);
    js_free(ctx, s);
    return obj;
  }

  JS_SetOpaque(obj, s);
  return obj;
}

static JSPortMidiStream*
js_portmidistream_data2(JSContext* ctx, JSValueConst val) {
  return JS_GetOpaque2(ctx, val, js_portmidistream_class_id);
}

static JSValue
js_pmevent_wrap(JSContext* ctx, const PmEvent* ev) {
  JSValue obj = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, obj, "status", JS_NewInt32(ctx, Pm_MessageStatus(ev->message)));
  JS_SetPropertyStr(ctx, obj, "data1", JS_NewInt32(ctx, Pm_MessageData1(ev->message)));
  JS_SetPropertyStr(ctx, obj, "data2", JS_NewInt32(ctx, Pm_MessageData2(ev->message)));
  JS_SetPropertyStr(ctx, obj, "timestamp", JS_NewInt32(ctx, ev->timestamp));
  return obj;
}

static int
js_pmevent_unwrap(JSContext* ctx, PmEvent* ev, JSValueConst val) {
  JSValue v;
  int32_t status = 0, data1 = 0, data2 = 0, timestamp = 0;

  v = JS_GetPropertyStr(ctx, val, "status");
  JS_ToInt32(ctx, &status, v);
  JS_FreeValue(ctx, v);

  v = JS_GetPropertyStr(ctx, val, "data1");
  JS_ToInt32(ctx, &data1, v);
  JS_FreeValue(ctx, v);

  v = JS_GetPropertyStr(ctx, val, "data2");
  JS_ToInt32(ctx, &data2, v);
  JS_FreeValue(ctx, v);

  v = JS_GetPropertyStr(ctx, val, "timestamp");
  JS_ToInt32(ctx, &timestamp, v);
  JS_FreeValue(ctx, v);

  ev->message = Pm_Message(status, data1, data2);
  ev->timestamp = timestamp;
  return 0;
}

enum {
  METHOD_READ = 0,
  METHOD_WRITE,
  METHOD_WRITESHORT,
  METHOD_WRITESYSEX,
  METHOD_POLL,
  METHOD_ABORT,
  METHOD_CLOSE,
  METHOD_SETFILTER,
  METHOD_SETCHANNELMASK,
  METHOD_SYNCHRONIZE,
};

static JSValue
js_portmidistream_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  JSPortMidiStream* s;
  JSValue ret = JS_UNDEFINED;

  if(!(s = js_portmidistream_data2(ctx, this_val)))
    return JS_EXCEPTION;

  if(!s->stream && magic != METHOD_CLOSE)
    return JS_ThrowTypeError(ctx, "PortMidiStream is closed");

  switch(magic) {
    case METHOD_READ: {
      int32_t maxEvents = 1024;
      PmEvent* buf;
      int n;

      if(argc > 0)
        JS_ToInt32(ctx, &maxEvents, argv[0]);
      if(maxEvents <= 0)
        maxEvents = 1;

      if(!(buf = js_mallocz(ctx, sizeof(PmEvent) * maxEvents)))
        return JS_EXCEPTION;

      if((n = Pm_Read(s->stream, buf, maxEvents)) < 0) {
        js_free(ctx, buf);
        return js_portmidi_throw(ctx, (PmError)n);
      }

      ret = JS_NewArray(ctx);

      for(int i = 0; i < n; i++)
        JS_SetPropertyUint32(ctx, ret, i, js_pmevent_wrap(ctx, &buf[i]));

      js_free(ctx, buf);
      break;
    }

    case METHOD_WRITE: {
      int64_t len = 0;
      PmEvent* buf;
      JSValue lenv;

      if(argc < 1)
        return JS_ThrowTypeError(ctx, "argument 1 must be an array of events");

      lenv = JS_GetPropertyStr(ctx, argv[0], "length");
      JS_ToInt64(ctx, &len, lenv);
      JS_FreeValue(ctx, lenv);

      if(!(buf = js_mallocz(ctx, sizeof(PmEvent) * (len > 0 ? len : 1))))
        return JS_EXCEPTION;

      for(int64_t i = 0; i < len; i++) {
        JSValue ev = JS_GetPropertyUint32(ctx, argv[0], i);
        js_pmevent_unwrap(ctx, &buf[i], ev);
        JS_FreeValue(ctx, ev);
      }

      PmError err = Pm_Write(s->stream, buf, len);
      js_free(ctx, buf);

      if(err != pmNoError)
        return js_portmidi_throw(ctx, err);
      break;
    }

    case METHOD_WRITESHORT: {
      int32_t status = 0, data1 = 0, data2 = 0, when = 0;
      PmError err;

      if(argc > 0)
        JS_ToInt32(ctx, &status, argv[0]);
      if(argc > 1)
        JS_ToInt32(ctx, &data1, argv[1]);
      if(argc > 2)
        JS_ToInt32(ctx, &data2, argv[2]);
      if(argc > 3)
        JS_ToInt32(ctx, &when, argv[3]);

      err = Pm_WriteShort(s->stream, when, Pm_Message(status, data1, data2));

      if(err != pmNoError)
        return js_portmidi_throw(ctx, err);
      break;
    }

    case METHOD_WRITESYSEX: {
      int32_t when = 0;
      size_t len = 0;
      uint8_t* ptr;
      PmError err;

      if(argc > 0)
        JS_ToInt32(ctx, &when, argv[0]);

      if(argc < 2 || !(ptr = js_portmidi_get_buffer(ctx, argv[1], &len)))
        return JS_ThrowTypeError(ctx, "argument 2 must be a Uint8Array/DataView or ArrayBuffer");

      /* Pm_WriteSysEx scans for the EOX (0xF7) byte itself; it has no
       * explicit length parameter, so the buffer must already end with it. */
      err = Pm_WriteSysEx(s->stream, when, ptr);

      if(err != pmNoError)
        return js_portmidi_throw(ctx, err);
      break;
    }

    case METHOD_POLL: {
      PmError r = Pm_Poll(s->stream);

      if(r < 0)
        return js_portmidi_throw(ctx, r);

      ret = JS_NewBool(ctx, r == TRUE);
      break;
    }

    case METHOD_ABORT: {
      PmError err = Pm_Abort(s->stream);

      if(err != pmNoError)
        return js_portmidi_throw(ctx, err);
      break;
    }

    case METHOD_CLOSE: {
      if(s->stream) {
        PmError err = Pm_Close(s->stream);
        s->stream = NULL;

        if(err != pmNoError)
          return js_portmidi_throw(ctx, err);
      }
      break;
    }

    case METHOD_SETFILTER: {
      int32_t mask = 0;
      PmError err;

      if(argc > 0)
        JS_ToInt32(ctx, &mask, argv[0]);

      err = Pm_SetFilter(s->stream, mask);

      if(err != pmNoError)
        return js_portmidi_throw(ctx, err);
      break;
    }

    case METHOD_SETCHANNELMASK: {
      int32_t mask = 0;
      PmError err;

      if(argc > 0)
        JS_ToInt32(ctx, &mask, argv[0]);

      err = Pm_SetChannelMask(s->stream, mask);

      if(err != pmNoError)
        return js_portmidi_throw(ctx, err);
      break;
    }

    case METHOD_SYNCHRONIZE: {
      Pm_Synchronize(s->stream);
      break;
    }
  }

  return ret;
}

enum {
  PROP_HASHOSTERROR = 0,
  PROP_DEVICEID,
  PROP_ISOUTPUT,
};

static JSValue
js_portmidistream_get(JSContext* ctx, JSValueConst this_val, int magic) {
  JSPortMidiStream* s;

  if(!(s = js_portmidistream_data2(ctx, this_val)))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_HASHOSTERROR: return JS_NewBool(ctx, s->stream ? Pm_HasHostError(s->stream) : FALSE);
    case PROP_DEVICEID: return JS_NewInt32(ctx, s->deviceId);
    case PROP_ISOUTPUT: return JS_NewBool(ctx, s->isOutput);
  }

  return JS_UNDEFINED;
}

static void
js_portmidistream_finalizer(JSRuntime* rt, JSValue val) {
  JSPortMidiStream* s;

  if((s = JS_GetOpaque(val, js_portmidistream_class_id))) {
    if(s->stream)
      Pm_Close(s->stream);
    js_free_rt(rt, s);
  }
}

static JSClassDef js_portmidistream_class = {
    .class_name = "PortMidiStream",
    .finalizer = js_portmidistream_finalizer,
};

static const JSCFunctionListEntry js_portmidistream_funcs[] = {
    JS_CGETSET_MAGIC_DEF("hasHostError", js_portmidistream_get, 0, PROP_HASHOSTERROR),
    JS_CGETSET_MAGIC_DEF("deviceId", js_portmidistream_get, 0, PROP_DEVICEID),
    JS_CGETSET_MAGIC_DEF("isOutput", js_portmidistream_get, 0, PROP_ISOUTPUT),
    JS_CFUNC_MAGIC_DEF("read", 0, js_portmidistream_method, METHOD_READ),
    JS_CFUNC_MAGIC_DEF("write", 1, js_portmidistream_method, METHOD_WRITE),
    JS_CFUNC_MAGIC_DEF("writeShort", 1, js_portmidistream_method, METHOD_WRITESHORT),
    JS_CFUNC_MAGIC_DEF("writeSysEx", 2, js_portmidistream_method, METHOD_WRITESYSEX),
    JS_CFUNC_MAGIC_DEF("poll", 0, js_portmidistream_method, METHOD_POLL),
    JS_CFUNC_MAGIC_DEF("abort", 0, js_portmidistream_method, METHOD_ABORT),
    JS_CFUNC_MAGIC_DEF("close", 0, js_portmidistream_method, METHOD_CLOSE),
    JS_CFUNC_MAGIC_DEF("setFilter", 1, js_portmidistream_method, METHOD_SETFILTER),
    JS_CFUNC_MAGIC_DEF("setChannelMask", 1, js_portmidistream_method, METHOD_SETCHANNELMASK),
    JS_CFUNC_MAGIC_DEF("synchronize", 0, js_portmidistream_method, METHOD_SYNCHRONIZE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "PortMidiStream", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_portmidistream_static_funcs[] = {
    JS_CFUNC_MAGIC_DEF("openInput", 1, js_portmidistream_open, STATIC_OPENINPUT),
    JS_CFUNC_MAGIC_DEF("openOutput", 1, js_portmidistream_open, STATIC_OPENOUTPUT),
};

/* ---- module functions/constants ---------------------------------------- */

static const JSCFunctionListEntry js_portmidi_funcs[] = {
    JS_CFUNC_MAGIC_DEF("initialize", 0, js_portmidi_function, FUNC_INITIALIZE),
    JS_CFUNC_MAGIC_DEF("terminate", 0, js_portmidi_function, FUNC_TERMINATE),
    JS_CFUNC_MAGIC_DEF("countDevices", 0, js_portmidi_function, FUNC_COUNTDEVICES),
    JS_CFUNC_MAGIC_DEF("getDefaultInputDeviceID", 0, js_portmidi_function, FUNC_GETDEFAULTINPUTDEVICEID),
    JS_CFUNC_MAGIC_DEF("getDefaultOutputDeviceID", 0, js_portmidi_function, FUNC_GETDEFAULTOUTPUTDEVICEID),
    JS_CFUNC_MAGIC_DEF("getDeviceInfo", 1, js_portmidi_function, FUNC_GETDEVICEINFO),
    JS_CFUNC_MAGIC_DEF("time", 0, js_portmidi_function, FUNC_TIME),
    JS_CFUNC_MAGIC_DEF("getErrorText", 1, js_portmidi_function, FUNC_GETERRORTEXT),
    JS_CFUNC_MAGIC_DEF("channel", 1, js_portmidi_function, FUNC_CHANNEL),

    JS_PROP_INT32_DEF("pmNoError", pmNoError, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmGotData", pmGotData, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmHostError", pmHostError, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmInvalidDeviceId", pmInvalidDeviceId, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmInsufficientMemory", pmInsufficientMemory, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmBufferTooSmall", pmBufferTooSmall, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmBufferOverflow", pmBufferOverflow, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmBadPtr", pmBadPtr, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmBadData", pmBadData, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmInternalError", pmInternalError, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmBufferMaxSize", pmBufferMaxSize, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("pmNoDevice", pmNoDevice, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("PM_FILT_ACTIVE", PM_FILT_ACTIVE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_SYSEX", PM_FILT_SYSEX, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_CLOCK", PM_FILT_CLOCK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_PLAY", PM_FILT_PLAY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_TICK", PM_FILT_TICK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_FD", PM_FILT_FD, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_UNDEFINED", PM_FILT_UNDEFINED, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_RESET", PM_FILT_RESET, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_REALTIME", PM_FILT_REALTIME, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_NOTE", PM_FILT_NOTE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_CHANNEL_AFTERTOUCH", PM_FILT_CHANNEL_AFTERTOUCH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_POLY_AFTERTOUCH", PM_FILT_POLY_AFTERTOUCH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_AFTERTOUCH", PM_FILT_AFTERTOUCH, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_PROGRAM", PM_FILT_PROGRAM, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_CONTROL", PM_FILT_CONTROL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_PITCHBEND", PM_FILT_PITCHBEND, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_MTC", PM_FILT_MTC, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_SONG_POSITION", PM_FILT_SONG_POSITION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_SONG_SELECT", PM_FILT_SONG_SELECT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_TUNE", PM_FILT_TUNE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("PM_FILT_SYSTEMCOMMON", PM_FILT_SYSTEMCOMMON, JS_PROP_CONFIGURABLE),
};

int
js_portmidi_init(JSContext* ctx, JSModuleDef* m) {
  JS_NewClassID(&js_portmidistream_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_portmidistream_class_id, &js_portmidistream_class);

  portmidistream_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, portmidistream_proto, js_portmidistream_funcs, countof(js_portmidistream_funcs));
  JS_SetClassProto(ctx, js_portmidistream_class_id, portmidistream_proto);

  /* no public constructor: instances are only produced by openInput()/openOutput() */
  portmidistream_ctor = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetConstructor(ctx, portmidistream_ctor, portmidistream_proto);
  JS_SetPropertyFunctionList(ctx, portmidistream_ctor, js_portmidistream_static_funcs, countof(js_portmidistream_static_funcs));

  JS_NewClassID(&js_pmdeviceinfo_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pmdeviceinfo_class_id, &js_pmdeviceinfo_class);

  pmdeviceinfo_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, pmdeviceinfo_proto, js_pmdeviceinfo_funcs, countof(js_pmdeviceinfo_funcs));
  JS_SetClassProto(ctx, js_pmdeviceinfo_class_id, pmdeviceinfo_proto);

  /* no public constructor either: only produced by getDeviceInfo()/devices[i] */
  pmdeviceinfo_ctor = JS_NewObjectProto(ctx, JS_NULL);
  JS_SetConstructor(ctx, pmdeviceinfo_ctor, pmdeviceinfo_proto);

  JS_NewClassID(&js_pmdevices_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_pmdevices_class_id, &js_pmdevices_class);

  pmdevices_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, pmdevices_proto, js_pmdevices_funcs, countof(js_pmdevices_funcs));
  JS_SetClassProto(ctx, js_pmdevices_class_id, pmdevices_proto);

  pmdevices_obj = JS_NewObjectProtoClass(ctx, pmdevices_proto, js_pmdevices_class_id);

  if(m) {
    JS_SetModuleExport(ctx, m, "PortMidiStream", portmidistream_ctor);
    JS_SetModuleExport(ctx, m, "PmDeviceInfo", pmdeviceinfo_ctor);
    JS_SetModuleExport(ctx, m, "devices", pmdevices_obj);
    JS_SetModuleExportList(ctx, m, js_portmidi_funcs, countof(js_portmidi_funcs));
  }

  return 0;
}

VISIBLE void
js_init_module_portmidi(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "PortMidiStream");
  JS_AddModuleExport(ctx, m, "PmDeviceInfo");
  JS_AddModuleExport(ctx, m, "devices");
  JS_AddModuleExportList(ctx, m, js_portmidi_funcs, countof(js_portmidi_funcs));
}

VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_portmidi_init))) {
    js_init_module_portmidi(ctx, m);
  }

  return m;
}
