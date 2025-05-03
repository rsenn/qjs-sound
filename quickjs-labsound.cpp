#include <quickjs.h>
#include <cutils.h>
#include "defines.h"
#include "LabSound/LabSound.h"
#include "LabSound/backends/AudioDevice_RtAudio.h"
#include "cpputils.h"
#include <array>

static ClassId js_audiocontext_class_id, js_audiolistener_class_id, js_audiodevice_class_id, js_audionode_class_id,
    js_audiodestinationnode_class_id, js_audioscheduledsourcenode_class_id, js_oscillatornode_class_id;
static JSValue audiocontext_proto, audiocontext_ctor, audiolistener_proto, audiolistener_ctor, audiodevice_proto,
    audiodevice_ctor, audionode_proto, audionode_ctor, audiodestinationnode_proto, audiodestinationnode_ctor,
    audioscheduledsourcenode_proto, audioscheduledsourcenode_ctor, oscillatornode_proto, oscillatornode_ctor;

typedef std::shared_ptr<lab::AudioContext> AudioContextPtr;
typedef std::shared_ptr<lab::AudioDestinationNode> AudioDestinationNodePtr;
typedef std::shared_ptr<lab::AudioListener> AudioListenerPtr;
typedef std::shared_ptr<lab::AudioDevice> AudioDevicePtr;
typedef ClassPtr<lab::AudioNode> AudioNodePtr;
typedef ClassPtr<lab::AudioScheduledSourceNode> AudioScheduledSourceNodePtr;

typedef std::shared_ptr<lab::OscillatorNode> OscillatorNodePtr;

static JSValue
js_audiocontext_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  bool isOffline = false, autoDispatchEvents = true;

  if(argc > 0)
    isOffline = JS_ToBool(ctx, argv[0]);
  if(argc > 1)
    autoDispatchEvents = JS_ToBool(ctx, argv[1]);

  AudioContextPtr* ac = static_cast<AudioContextPtr*>(js_mallocz(ctx, sizeof(AudioContextPtr)));

  new(ac) AudioContextPtr(std::make_shared<lab::AudioContext>(isOffline, autoDispatchEvents));

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiocontext_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiocontext_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ac);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_SAMPLERATE,
  PROP_DESTINATION_NODE,
  PROP_LISTENER,
  PROP_CURRENTTIME,
  PROP_CURRENTSAMPLEFRAME,
  PROP_PREDICTED_CURRENTTIME,
};

static JSValue
js_audiocontext_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioContextPtr* ac;
  JSValue ret = JS_UNDEFINED;

  if(!(ac = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, this_val, js_audiocontext_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_SAMPLERATE: {
      ret = JS_NewFloat64(ctx, (*ac)->sampleRate());
      break;
    }
    case PROP_DESTINATION_NODE: {
      AudioDestinationNodePtr sadn = (*ac)->destinationNode();

      ret = JS_NewObjectProtoClass(ctx, audiodestinationnode_proto, js_audiodestinationnode_class_id);

      AudioDestinationNodePtr* ptr =
          static_cast<AudioDestinationNodePtr*>(js_mallocz(ctx, sizeof(AudioDestinationNodePtr)));

      new(ptr) AudioDestinationNodePtr(sadn);

      JS_SetOpaque(ret, ptr);
      break;
    }
    case PROP_LISTENER: {
      AudioListenerPtr sal = (*ac)->listener();

      ret = JS_NewObjectProtoClass(ctx, audiolistener_proto, js_audiolistener_class_id);

      AudioListenerPtr* ptr = static_cast<AudioListenerPtr*>(js_mallocz(ctx, sizeof(AudioListenerPtr)));

      new(ptr) AudioListenerPtr(sal);

      JS_SetOpaque(ret, ptr);
      break;
    }
    case PROP_CURRENTTIME: {
      ret = JS_NewFloat64(ctx, (*ac)->currentTime());
      break;
    }
    case PROP_CURRENTSAMPLEFRAME: {
      ret = JS_NewInt64(ctx, (*ac)->currentSampleFrame());
      break;
    }
    case PROP_PREDICTED_CURRENTTIME: {
      ret = JS_NewFloat64(ctx, (*ac)->predictedCurrentTime());
      break;
    }
  }

  return ret;
}

static JSValue
js_audiocontext_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioContextPtr* ac;
  JSValue ret = JS_UNDEFINED;

  if(!(ac = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, this_val, js_audiocontext_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_DESTINATION_NODE: {
      AudioDestinationNodePtr* sadn;

      if(!(sadn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque2(ctx, value, js_audiodestinationnode_class_id))))
        return JS_ThrowInternalError(ctx, "value must be AudioDestinationNode");

      (*ac)->setDestinationNode(*sadn);

      break;
    }
  }

  return ret;
}

static void
js_audiocontext_finalizer(JSRuntime* rt, JSValue val) {
  AudioContextPtr* ac;

  if((ac = static_cast<AudioContextPtr*>(JS_GetOpaque(val, js_audiocontext_class_id)))) {
    ac->~AudioContextPtr();
    js_free_rt(rt, ac);
  }
}

static JSClassDef js_audiocontext_class = {
    .class_name = "AudioContext",
    .finalizer = js_audiocontext_finalizer,
};

static const JSCFunctionListEntry js_audiocontext_funcs[] = {
    JS_CGETSET_MAGIC_DEF("sampleRate", js_audiocontext_get, 0, PROP_SAMPLERATE),
    JS_CGETSET_MAGIC_DEF("destinationNode", js_audiocontext_get, js_audiocontext_set, PROP_DESTINATION_NODE),
    JS_CGETSET_MAGIC_DEF("listener", js_audiocontext_get, 0, PROP_LISTENER),
    JS_CGETSET_MAGIC_DEF("currentTime", js_audiocontext_get, 0, PROP_CURRENTTIME),
    JS_CGETSET_MAGIC_DEF("currentSampleFrame", js_audiocontext_get, 0, PROP_CURRENTSAMPLEFRAME),
    JS_CGETSET_MAGIC_DEF("predictedCurrentTime", js_audiocontext_get, 0, PROP_PREDICTED_CURRENTTIME),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioContext", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiolistener_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  AudioListenerPtr* al = static_cast<AudioListenerPtr*>(js_mallocz(ctx, sizeof(AudioListenerPtr)));

  new(al) AudioListenerPtr(std::make_shared<lab::AudioListener>());

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiolistener_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiolistener_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, al);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_POSITION_X,
  PROP_POSITION_Y,
  PROP_POSITION_Z,
};

static JSValue
js_audiolistener_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioListenerPtr* al;
  JSValue ret = JS_UNDEFINED;

  if(!(al = static_cast<AudioListenerPtr*>(JS_GetOpaque2(ctx, this_val, js_audiolistener_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_POSITION_X: {
      ret = JS_NewFloat64(ctx, (*al)->positionX()->value());
      break;
    }
    case PROP_POSITION_Y: {
      ret = JS_NewFloat64(ctx, (*al)->positionY()->value());
      break;
    }
    case PROP_POSITION_Z: {
      ret = JS_NewFloat64(ctx, (*al)->positionZ()->value());
      break;
    }
  }

  return ret;
}
static JSValue
js_audiolistener_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioListenerPtr* al;
  JSValue ret = JS_UNDEFINED;

  if(!(al = static_cast<AudioListenerPtr*>(JS_GetOpaque2(ctx, this_val, js_audiolistener_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case PROP_POSITION_X: {
      (*al)->positionX()->setValue(from_js<double>(ctx, value));
      break;
    }
    case PROP_POSITION_Y: {
      (*al)->positionY()->setValue(from_js<double>(ctx, value));
      break;
    }
    case PROP_POSITION_Z: {
      (*al)->positionZ()->setValue(from_js<double>(ctx, value));
      break;
    }
  }

  return ret;
}

static void
js_audiolistener_finalizer(JSRuntime* rt, JSValue val) {
  AudioListenerPtr* al;

  if((al = static_cast<AudioListenerPtr*>(JS_GetOpaque(val, js_audiolistener_class_id)))) {
    al->~AudioListenerPtr();
    js_free_rt(rt, al);
  }
}

static JSClassDef js_audiolistener_class = {
    .class_name = "AudioListener",
    .finalizer = js_audiolistener_finalizer,
};

static const JSCFunctionListEntry js_audiolistener_funcs[] = {
    JS_CGETSET_MAGIC_DEF("positionX", js_audiolistener_get, js_audiolistener_set, PROP_POSITION_X),
    JS_CGETSET_MAGIC_DEF("positionY", js_audiolistener_get, js_audiolistener_set, PROP_POSITION_Y),
    JS_CGETSET_MAGIC_DEF("positionZ", js_audiolistener_get, js_audiolistener_set, PROP_POSITION_Z),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioListener", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiodevice_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;

  lab::AudioStreamConfig in_config, out_config;
  AudioDevicePtr* ad = static_cast<AudioDevicePtr*>(js_mallocz(ctx, sizeof(AudioDevicePtr)));

  in_config.device_index = 0;
  in_config.desired_channels = 2;
  in_config.desired_samplerate = 44100;

  out_config.device_index = 0;
  out_config.desired_channels = 2;
  out_config.desired_samplerate = 44100;

  new(ad) AudioDevicePtr(std::make_shared<lab::AudioDevice_RtAudio>(in_config, out_config));

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiodevice_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiodevice_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, ad);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum { DEVICE_DESTINATION_NODE };

static JSValue
js_audiodevice_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioDevicePtr* ad;
  JSValue ret = JS_UNDEFINED;

  if(!(ad = static_cast<AudioDevicePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodevice_class_id))))
    return JS_EXCEPTION;

  switch(magic) {}

  return ret;
}

static JSValue
js_audiodevice_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioDevicePtr* ad;
  JSValue ret = JS_UNDEFINED;

  if(!(ad = static_cast<AudioDevicePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodevice_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case DEVICE_DESTINATION_NODE: {
      AudioDestinationNodePtr* sadn;

      if(!(sadn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque2(ctx, value, js_audiodestinationnode_class_id))))
        return JS_ThrowInternalError(ctx, "value must be AudioDestinationNode");

      (*ad)->setDestinationNode(*sadn);
      break;
    }
  }

  return ret;
}

static void
js_audiodevice_finalizer(JSRuntime* rt, JSValue val) {
  AudioDevicePtr* ad;

  if((ad = static_cast<AudioDevicePtr*>(JS_GetOpaque(val, js_audiodevice_class_id)))) {
    ad->~AudioDevicePtr();
    js_free_rt(rt, ad);
  }
}

static JSClassDef js_audiodevice_class = {
    .class_name = "AudioDevice",
    .finalizer = js_audiodevice_finalizer,
};

static const JSCFunctionListEntry js_audiodevice_funcs[] = {
    JS_CGETSET_MAGIC_DEF("destinationNode", 0, js_audiodevice_set, DEVICE_DESTINATION_NODE),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDevice", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audionode_wrap(JSContext* ctx, JSValueConst new_target, AudioNodePtr& anode) {
  JSValue proto, obj = JS_UNDEFINED;
  AudioNodePtr* an = static_cast<AudioNodePtr*>(js_mallocz(ctx, sizeof(AudioNodePtr)));

  new(an) AudioNodePtr(anode, anode.context);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audionode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audionode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, an);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  AUDIONODE_ISSCHEDULEDNODE,
  AUDIONODE_INITIALIZE,
  AUDIONODE_UNINITIALIZE,
  AUDIONODE_ISINITIALIZED,
  AUDIONODE_NUMBEROFINPUTS,
  AUDIONODE_NUMBEROFOUTPUTS,
  AUDIONODE_PARAMNAMES,
  AUDIONODE_PARAMSHORTNAMES,
  AUDIONODE_PARAMINDEX,
  AUDIONODE_SETTINGNAMES,
  AUDIONODE_SETTINGSHORTNAMES,
  AUDIONODE_SETTINGINDEX,
};

static JSValue
js_audionode_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;

  if(!(an = static_cast<AudioNodePtr*>(JS_GetOpaque2(ctx, this_val, js_audionode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIONODE_ISSCHEDULEDNODE: {
      ret = JS_NewBool(ctx, (*an)->isScheduledNode());
      break;
    }
    case AUDIONODE_INITIALIZE: {
      (*an)->initialize();
      break;
    }
    case AUDIONODE_UNINITIALIZE: {
      (*an)->uninitialize();
      break;
    }
    case AUDIONODE_ISINITIALIZED: {
      ret = JS_NewBool(ctx, (*an)->isInitialized());
      break;
    }
    case AUDIONODE_NUMBEROFINPUTS: {
      ret = JS_NewInt32(ctx, (*an)->numberOfInputs());
      break;
    }
    case AUDIONODE_NUMBEROFOUTPUTS: {
      ret = JS_NewInt32(ctx, (*an)->numberOfOutputs());
      break;
    }
    case AUDIONODE_PARAMNAMES: {
      ret = to_js(ctx, (*an)->paramNames());
      break;
    }
    case AUDIONODE_PARAMSHORTNAMES: {
      ret = to_js(ctx, (*an)->paramShortNames());
      break;
    }
    case AUDIONODE_PARAMINDEX: {
      const char* str;

      if((str = JS_ToCString(ctx, argv[0]))) {
        ret = to_js(ctx, (*an)->param_index(str));
        JS_FreeCString(ctx, str);
      }

      break;
    }
    case AUDIONODE_SETTINGNAMES: {
      ret = to_js(ctx, (*an)->settingNames());
      break;
    }
    case AUDIONODE_SETTINGSHORTNAMES: {
      ret = to_js(ctx, (*an)->settingShortNames());
      break;
    }
    case AUDIONODE_SETTINGINDEX: {
      const char* str;

      if((str = JS_ToCString(ctx, argv[0]))) {
        ret = to_js(ctx, (*an)->setting_index(str));
        JS_FreeCString(ctx, str);
      }

      break;
    }
  }

  return ret;
}

enum {
  AUDIONODE_NAME,
  AUDIONODE_CHANNELCOUNT,
};

static JSValue
js_audionode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;

  if(!(an = static_cast<AudioNodePtr*>(JS_GetOpaque2(ctx, this_val, js_audionode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIONODE_NAME: {
      ret = JS_NewString(ctx, (*an)->name());
      break;
    }
    case AUDIONODE_CHANNELCOUNT: {
      ret = JS_NewInt32(ctx, (*an)->channelCount());
      break;
    }
  }

  return ret;
}

static JSValue
js_audionode_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  AudioNodePtr* an;
  JSValue ret = JS_UNDEFINED;
  double d;

  if(!(an = static_cast<AudioNodePtr*>(JS_GetOpaque2(ctx, this_val, js_audionode_class_id))))
    return JS_EXCEPTION;

  JS_ToFloat64(ctx, &d, value);

  switch(magic) {
    case AUDIONODE_CHANNELCOUNT: {
      int32_t n = -1;
      JS_ToInt32(ctx, &n, value);

      /*ContextGraphLock gLock(this, "AudioContext::handlePreRenderTasks()");
      (*an)->setChannelCount();*/
      break;
    }
  }

  return ret;
}

static void
js_audionode_finalizer(JSRuntime* rt, JSValue val) {
  AudioNodePtr* an;

  if((an = static_cast<AudioNodePtr*>(JS_GetOpaque(val, js_audionode_class_id)))) {
    an->~AudioNodePtr();
    js_free_rt(rt, an);
  }
}

static JSClassDef js_audionode_class = {
    .class_name = "AudioNode",
    .finalizer = js_audionode_finalizer,
};

static const JSCFunctionListEntry js_audionode_funcs[] = {
    JS_CFUNC_MAGIC_DEF("isScheduledNode", 0, js_audionode_methods, AUDIONODE_ISSCHEDULEDNODE),
    JS_CFUNC_MAGIC_DEF("initialize", 0, js_audionode_methods, AUDIONODE_INITIALIZE),
    JS_CFUNC_MAGIC_DEF("uninitialize", 0, js_audionode_methods, AUDIONODE_UNINITIALIZE),
    JS_CFUNC_MAGIC_DEF("isInitialized", 0, js_audionode_methods, AUDIONODE_ISINITIALIZED),
    JS_CFUNC_MAGIC_DEF("numberOfInputs", 0, js_audionode_methods, AUDIONODE_NUMBEROFINPUTS),
    JS_CFUNC_MAGIC_DEF("numberOfOutputs", 0, js_audionode_methods, AUDIONODE_NUMBEROFOUTPUTS),
    JS_CFUNC_MAGIC_DEF("paramNames", 0, js_audionode_methods, AUDIONODE_PARAMNAMES),
    JS_CFUNC_MAGIC_DEF("paramShortNames", 0, js_audionode_methods, AUDIONODE_PARAMSHORTNAMES),
    JS_CFUNC_MAGIC_DEF("param_index", 1, js_audionode_methods, AUDIONODE_PARAMINDEX),
    JS_CFUNC_MAGIC_DEF("settingNames", 0, js_audionode_methods, AUDIONODE_SETTINGNAMES),
    JS_CFUNC_MAGIC_DEF("settingShortNames", 0, js_audionode_methods, AUDIONODE_SETTINGSHORTNAMES),
    JS_CFUNC_MAGIC_DEF("setting_index", 1, js_audionode_methods, AUDIONODE_SETTINGINDEX),
    JS_CGETSET_MAGIC_DEF("name", js_audionode_get, 0, AUDIONODE_NAME),
    JS_CGETSET_MAGIC_DEF("channelCount", js_audionode_get, js_audionode_set, AUDIONODE_CHANNELCOUNT),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioNode", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audiodestinationnode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  lab::AudioContext* ac = nullptr;
  std::shared_ptr<lab::AudioDevice> device;

  if(argc > 0) {
    AudioContextPtr* acptr;

    if(!(acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id))))
      return JS_EXCEPTION;

    ac = acptr->get();
  }

  if(!ac)
    return JS_ThrowInternalError(ctx, "argument 1 must be AudioContext");

  if(argc > 1) {
    AudioDevicePtr* adptr;

    if(!(adptr = static_cast<AudioDevicePtr*>(JS_GetOpaque2(ctx, argv[1], js_audiodevice_class_id))))
      return JS_EXCEPTION;

    device = *adptr;
  }

  if(!device.get())
    return JS_ThrowInternalError(ctx, "argument 2 must be AudioDevice");

  AudioDestinationNodePtr* adn =
      static_cast<AudioDestinationNodePtr*>(js_mallocz(ctx, sizeof(AudioDestinationNodePtr)));

  new(adn) AudioDestinationNodePtr(std::make_shared<lab::AudioDestinationNode>(*ac, device));

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audiodestinationnode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audiodestinationnode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, adn);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  PROP_NAME,
};

static JSValue
js_audiodestinationnode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  AudioDestinationNodePtr* adn;
  JSValue ret = JS_UNDEFINED;

  if(!(adn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodestinationnode_class_id))))
    return JS_EXCEPTION;

  if(adn->get() == nullptr)
    return JS_UNDEFINED;

  switch(magic) {
    case PROP_NAME: {
      ret = JS_NewString(ctx, (*adn)->name());
      break;
    }
  }

  return ret;
}

enum {
  AUDIODESTINATION_RESET,
};

static JSValue
js_audiodestinationnode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioDestinationNodePtr* adn;
  JSValue ret = JS_UNDEFINED;

  if(!(adn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque2(ctx, this_val, js_audiodestinationnode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case AUDIODESTINATION_RESET: {
      adn->reset();
      break;
    }
  }

  return ret;
}

static void
js_audiodestinationnode_finalizer(JSRuntime* rt, JSValue val) {
  AudioDestinationNodePtr* adn;

  if((adn = static_cast<AudioDestinationNodePtr*>(JS_GetOpaque(val, js_audiodestinationnode_class_id)))) {
    adn->~AudioDestinationNodePtr();
    js_free_rt(rt, adn);
  }
}

static JSClassDef js_audiodestinationnode_class = {
    .class_name = "AudioDestinationNode",
    .finalizer = js_audiodestinationnode_finalizer,
};

static const JSCFunctionListEntry js_audiodestinationnode_funcs[] = {
    JS_CGETSET_MAGIC_DEF("name", js_audiodestinationnode_get, 0, PROP_NAME),
    JS_CFUNC_MAGIC_DEF("reset", 0, js_audiodestinationnode_method, AUDIODESTINATION_RESET),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioDestinationNode", JS_PROP_CONFIGURABLE),
};

static JSValue
js_audioscheduledsourcenode_wrap(JSContext* ctx, JSValueConst new_target, AudioScheduledSourceNodePtr& anode) {
  JSValue proto, obj = JS_UNDEFINED;
  AudioScheduledSourceNodePtr* assn =
      static_cast<AudioScheduledSourceNodePtr*>(js_mallocz(ctx, sizeof(AudioScheduledSourceNodePtr)));

  new(assn) AudioScheduledSourceNodePtr(anode, anode.context);

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = audioscheduledsourcenode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_audioscheduledsourcenode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, assn);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  IS_PLAYING_OR_SCHEDULED,
};

static JSValue
js_audioscheduledsourcenode_method(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  AudioScheduledSourceNodePtr* assn;
  JSValue ret = JS_UNDEFINED;

  if(!(assn = static_cast<AudioScheduledSourceNodePtr*>(
           JS_GetOpaque2(ctx, this_val, js_audioscheduledsourcenode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case IS_PLAYING_OR_SCHEDULED: {
      ret = JS_NewBool(ctx, (*assn)->isPlayingOrScheduled());
      break;
    }
  }

  return ret;
}

static void
js_audioscheduledsourcenode_finalizer(JSRuntime* rt, JSValue val) {
  AudioScheduledSourceNodePtr* assn;

  if((assn = static_cast<AudioScheduledSourceNodePtr*>(JS_GetOpaque(val, js_audioscheduledsourcenode_class_id)))) {
    assn->~AudioScheduledSourceNodePtr();
    js_free_rt(rt, assn);
  }
}

static JSClassDef js_audioscheduledsourcenode_class = {
    .class_name = "AudioScheduledSourceNode",
    .finalizer = js_audioscheduledsourcenode_finalizer,
};

static const JSCFunctionListEntry js_audioscheduledsourcenode_funcs[] = {
    JS_CFUNC_MAGIC_DEF("isPlayingOrScheduled", 0, js_audioscheduledsourcenode_method, IS_PLAYING_OR_SCHEDULED),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "AudioScheduledSourceNode", JS_PROP_CONFIGURABLE),
};

static const std::array<const char*, 8> oscillator_types = {
    "oscillator_none",
    "sine",
    "fast_sine",
    "square",
    "sawtooth",
    "falling_sawtooth",
    "triangle",
    "custom",
};

static JSValue
js_oscillatornode_constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst argv[]) {
  JSValue proto, obj = JS_UNDEFINED;
  lab::AudioContext* ac = nullptr;

  if(argc > 0) {
    AudioContextPtr* acptr;

    if(!(acptr = static_cast<AudioContextPtr*>(JS_GetOpaque2(ctx, argv[0], js_audiocontext_class_id))))
      return JS_EXCEPTION;

    ac = acptr->get();
  }

  OscillatorNodePtr* on = static_cast<OscillatorNodePtr*>(js_mallocz(ctx, sizeof(OscillatorNodePtr)));

  new(on) OscillatorNodePtr(std::make_shared<lab::OscillatorNode>(*ac));

  /* using new_target to get the prototype is necessary when the class is extended. */
  proto = JS_GetPropertyStr(ctx, new_target, "prototype");
  if(JS_IsException(proto))
    goto fail;

  if(!JS_IsObject(proto))
    proto = oscillatornode_proto;

  /* using new_target to get the prototype is necessary when the class is extended. */
  obj = JS_NewObjectProtoClass(ctx, proto, js_oscillatornode_class_id);
  JS_FreeValue(ctx, proto);

  if(JS_IsException(obj))
    goto fail;

  JS_SetOpaque(obj, on);
  return obj;

fail:
  JS_FreeValue(ctx, obj);
  return JS_EXCEPTION;
}

enum {
  OSCILLATORNODE_START,
};

static JSValue
js_oscillatornode_methods(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst argv[], int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<OscillatorNodePtr*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case OSCILLATORNODE_START: {
      double when;
      JS_ToFloat64(ctx, &when, argv[0]);

      (*on)->start(when);
      break;
    }
  }

  return ret;
}

enum {
  OSCILLATORNODE_TYPE,
  OSCILLATORNODE_AMPLITUDE,
  OSCILLATORNODE_FREQUENCY,
  OSCILLATORNODE_DETUNE,
  OSCILLATORNODE_BIAS,
};

static JSValue
js_oscillatornode_get(JSContext* ctx, JSValueConst this_val, int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;

  if(!(on = static_cast<OscillatorNodePtr*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id))))
    return JS_EXCEPTION;

  switch(magic) {
    case OSCILLATORNODE_TYPE: {
      ret = JS_NewInt32(ctx, (*on)->type());
      break;
    }
    case OSCILLATORNODE_AMPLITUDE: {
      ret = JS_NewFloat64(ctx, (*on)->amplitude()->value());
      break;
    }
    case OSCILLATORNODE_FREQUENCY: {
      ret = JS_NewFloat64(ctx, (*on)->frequency()->value());
      break;
    }
    case OSCILLATORNODE_DETUNE: {
      ret = JS_NewFloat64(ctx, (*on)->detune()->value());
      break;
    }
    case OSCILLATORNODE_BIAS: {
      ret = JS_NewFloat64(ctx, (*on)->bias()->value());
      break;
    }
  }

  return ret;
}

static JSValue
js_oscillatornode_set(JSContext* ctx, JSValueConst this_val, JSValueConst value, int magic) {
  OscillatorNodePtr* on;
  JSValue ret = JS_UNDEFINED;
  double d;

  if(!(on = static_cast<OscillatorNodePtr*>(JS_GetOpaque2(ctx, this_val, js_oscillatornode_class_id))))
    return JS_EXCEPTION;

  JS_ToFloat64(ctx, &d, value);

  switch(magic) {
    case OSCILLATORNODE_TYPE: {
      int32_t type = -1;
      const char* arg;

      if(JS_IsString(value) && (arg = JS_ToCString(ctx, value))) {

        const auto it = std::find_if(oscillator_types.begin(), oscillator_types.end(), [arg](const char* str) -> bool {
          return !strcasecmp(arg, str);
        });

        if(it != oscillator_types.end())
          type = std::distance(oscillator_types.begin(), it);

        JS_FreeCString(ctx, arg);
      }

      if(type == -1)
        JS_ToInt32(ctx, &type, value);

      (*on)->setType(lab::OscillatorType(type));
      break;
    }

    case OSCILLATORNODE_AMPLITUDE: {
      (*on)->amplitude()->setValue(d);
      break;
    }
    case OSCILLATORNODE_FREQUENCY: {
      (*on)->frequency()->setValue(d);
      break;
    }
    case OSCILLATORNODE_DETUNE: {
      (*on)->detune()->setValue(d);
      break;
    }
    case OSCILLATORNODE_BIAS: {
      (*on)->bias()->setValue(d);
      break;
    }
  }

  return ret;
}

static void
js_oscillatornode_finalizer(JSRuntime* rt, JSValue val) {
  OscillatorNodePtr* on;

  if((on = static_cast<OscillatorNodePtr*>(JS_GetOpaque(val, js_oscillatornode_class_id)))) {
    on->~OscillatorNodePtr();
    js_free_rt(rt, on);
  }
}

static JSClassDef js_oscillatornode_class = {
    .class_name = "OscillatorNode",
    .finalizer = js_oscillatornode_finalizer,
};

static const JSCFunctionListEntry js_oscillatornode_funcs[] = {
    JS_CFUNC_MAGIC_DEF("start", 1, js_oscillatornode_methods, OSCILLATORNODE_START),
    JS_CGETSET_MAGIC_DEF("amplitude", js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_AMPLITUDE),
    JS_CGETSET_MAGIC_DEF("frequency", js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_FREQUENCY),
    JS_CGETSET_MAGIC_DEF("detune", js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_DETUNE),
    JS_CGETSET_MAGIC_DEF("bias", js_oscillatornode_get, js_oscillatornode_set, OSCILLATORNODE_BIAS),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "OscillatorNode", JS_PROP_CONFIGURABLE),
};

int
js_labsound_init(JSContext* ctx, JSModuleDef* m) {
  js_audiocontext_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiocontext_class_id, &js_audiocontext_class);

  audiocontext_ctor = JS_NewCFunction2(ctx, js_audiocontext_constructor, "AudioContext", 1, JS_CFUNC_constructor, 0);
  audiocontext_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, audiocontext_proto, js_audiocontext_funcs, countof(js_audiocontext_funcs));

  JS_SetClassProto(ctx, js_audiocontext_class_id, audiocontext_proto);
  JS_SetConstructor(ctx, audiocontext_ctor, audiocontext_proto);

  js_audiolistener_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiolistener_class_id, &js_audiolistener_class);

  audiolistener_ctor = JS_NewCFunction2(ctx, js_audiolistener_constructor, "AudioListener", 1, JS_CFUNC_constructor, 0);
  audiolistener_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, audiolistener_proto, js_audiolistener_funcs, countof(js_audiolistener_funcs));

  JS_SetClassProto(ctx, js_audiolistener_class_id, audiolistener_proto);
  JS_SetConstructor(ctx, audiolistener_ctor, audiolistener_proto);

  js_audiodevice_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audiodevice_class_id, &js_audiodevice_class);

  audiodevice_ctor = JS_NewCFunction2(ctx, js_audiodevice_constructor, "AudioDevice", 1, JS_CFUNC_constructor, 0);
  audiodevice_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, audiodevice_proto, js_audiodevice_funcs, countof(js_audiodevice_funcs));

  JS_SetClassProto(ctx, js_audiodevice_class_id, audiodevice_proto);
  JS_SetConstructor(ctx, audiodevice_ctor, audiodevice_proto);

  js_audionode_class_id.init();
  JS_NewClass(JS_GetRuntime(ctx), js_audionode_class_id, &js_audionode_class);

  audionode_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audionode_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, audionode_proto, js_audionode_funcs, countof(js_audionode_funcs));

  JS_SetClassProto(ctx, js_audionode_class_id, audionode_proto);
  JS_SetConstructor(ctx, audionode_ctor, audionode_proto);

  js_audiodestinationnode_class_id.init();
  js_audiodestinationnode_class_id.inherit(js_audionode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audiodestinationnode_class_id, &js_audiodestinationnode_class);

  audiodestinationnode_ctor =
      JS_NewCFunction2(ctx, js_audiodestinationnode_constructor, "AudioDestinationNode", 1, JS_CFUNC_constructor, 0);
  audiodestinationnode_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx,
                             audiodestinationnode_proto,
                             js_audiodestinationnode_funcs,
                             countof(js_audiodestinationnode_funcs));

  JS_SetClassProto(ctx, js_audiodestinationnode_class_id, audiodestinationnode_proto);
  JS_SetConstructor(ctx, audiodestinationnode_ctor, audiodestinationnode_proto);

  js_audioscheduledsourcenode_class_id.init();
  js_audioscheduledsourcenode_class_id.inherit(js_audionode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_audioscheduledsourcenode_class_id, &js_audioscheduledsourcenode_class);

  audioscheduledsourcenode_ctor = JS_NewObjectProto(ctx, JS_NULL);
  audioscheduledsourcenode_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx,
                             audioscheduledsourcenode_proto,
                             js_audioscheduledsourcenode_funcs,
                             countof(js_audioscheduledsourcenode_funcs));

  JS_SetClassProto(ctx, js_audioscheduledsourcenode_class_id, audioscheduledsourcenode_proto);
  JS_SetConstructor(ctx, audioscheduledsourcenode_ctor, audioscheduledsourcenode_proto);

  js_oscillatornode_class_id.init();
  js_audiodestinationnode_class_id.inherit(js_audionode_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_oscillatornode_class_id, &js_oscillatornode_class);

  oscillatornode_ctor =
      JS_NewCFunction2(ctx, js_oscillatornode_constructor, "OscillatorNode", 1, JS_CFUNC_constructor, 0);
  oscillatornode_proto = JS_NewObject(ctx);

  JS_SetPropertyFunctionList(ctx, oscillatornode_proto, js_oscillatornode_funcs, countof(js_oscillatornode_funcs));

  JS_SetClassProto(ctx, js_oscillatornode_class_id, oscillatornode_proto);

  if(m) {
    JS_SetModuleExport(ctx, m, "AudioContext", audiocontext_ctor);
    JS_SetModuleExport(ctx, m, "AudioListener", audiolistener_ctor);
    JS_SetModuleExport(ctx, m, "AudioDevice", audiodevice_ctor);
    JS_SetModuleExport(ctx, m, "AudioNode", audionode_ctor);
    JS_SetModuleExport(ctx, m, "AudioDestinationNode", audiodestinationnode_ctor);
    JS_SetModuleExport(ctx, m, "AudioScheduledSourceNode", audioscheduledsourcenode_ctor);
    JS_SetModuleExport(ctx, m, "OscillatorNode", oscillatornode_ctor);
  }

  return 0;
}

extern "C" VISIBLE void
js_init_module_labsound(JSContext* ctx, JSModuleDef* m) {
  JS_AddModuleExport(ctx, m, "AudioContext");
  JS_AddModuleExport(ctx, m, "AudioListener");
  JS_AddModuleExport(ctx, m, "AudioDevice");
  JS_AddModuleExport(ctx, m, "AudioNode");
  JS_AddModuleExport(ctx, m, "AudioDestinationNode");
  JS_AddModuleExport(ctx, m, "AudioScheduledSourceNode");
  JS_AddModuleExport(ctx, m, "OscillatorNode");
}

extern "C" VISIBLE JSModuleDef*
js_init_module(JSContext* ctx, const char* module_name) {
  JSModuleDef* m;

  if((m = JS_NewCModule(ctx, module_name, js_labsound_init)))
    js_init_module_labsound(ctx, m);

  return m;
}
