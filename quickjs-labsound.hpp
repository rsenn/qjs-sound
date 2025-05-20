#ifndef QUICKJS_LABSOUND_HPP
#define QUICKJS_LABSOUND_HPP

#include <quickjs.h>
#include "LabSound/LabSound.h"
#include "cpputils.hpp"

/*template<>
inline lab::ChannelCountMode
from_js<lab::ChannelCountMode>(JSContext* ctx, JSValueConst value) {
  int32_t ret = -1;
  const char* s;
  static const char* const names[] = {"Max", "ClampedMax", "Explicit", "End"};

  if((s = JS_ToCString(ctx, value))) {
    for(size_t i = 0; i < countof(names); ++i)
      if(!strcasecmp(s, names[i])) {
        ret = i;
        break;
      }
    JS_FreeCString(ctx, s);
  }
  if(ret == -1)
    JS_ToInt32(ctx, &ret, value);
  return lab::ChannelCountMode(ret + int(lab::ChannelCountMode::Max));
}*/

template<> struct enumeration_type<lab::SettingType> {
  static constexpr const char* enums[] = {
      "None",
      "Bool",
      "Integer",
      "Float",
      "Enum",
      "Bus",
      nullptr,
  };
};

template<> struct enumeration_type<lab::ChannelInterpretation> {
  static constexpr const char* enums[] = {
      "Speakers",
      "Discrete",
      nullptr,
  };
};

template<> struct enumeration_type<lab::Channel> {
  static constexpr const char* enums[] = {
      "Left",
      "Right",
      "Center",
      "LFE",
      "SurroundLeft",
      "SurroundRight",
      "BackLeft",
      "BackRight",
      nullptr,
  };
};

template<> struct enumeration_type<lab::ChannelCountMode> {
  static constexpr const char* enums[] = {
      "Max",
      "ClampedMax",
      "Explicit",
      "End",
      nullptr,
  };
};

template<> struct enumeration_type<lab::OscillatorType> {
  static constexpr const char* enums[] = {
      "OSCILLATOR_NONE",
      "SINE",
      "FAST_SINE",
      "SQUARE",
      "SAWTOOTH",
      "FALLING_SAWTOOTH",
      "TRIANGLE",
      "CUSTOM",
      nullptr,
  };
};

using std::shared_ptr;
using std::vector;
using std::weak_ptr;

class DestinationNode : public lab::AudioDestinationNode {
public:
  shared_ptr<lab::AudioDevice>
  platformAudioDevice() const {
    return _platformAudioDevice;
  }

  shared_ptr<lab::AudioContext> getContext() const;
};

class Device : public lab::AudioDevice {
public:
  shared_ptr<lab::AudioDestinationNode>
  destination() const {
    return _destinationNode;
  }
};

template<class T>
T
js_call(const ObjectRef& fn, int argc = 0, JSValueConst* argv = 0) {
  JSValue ret = JS_Call(fn.context(), fn.constValue(), JS_NULL, argc, argv);
  T result = from_js<T>(fn.context(), ret);
  JS_FreeValue(fn.context(), ret);
  return result;
}

template<>
void
js_call<void>(const ObjectRef& fn, int argc, JSValueConst* argv) {
  JSValue ret = JS_Call(fn.context(), fn.constValue(), JS_NULL, argc, argv);
  JS_FreeValue(fn.context(), ret);
}

class AudioProcessor : public lab::AudioProcessor {
protected:
  struct AudioProcessorCallbacks {
    AudioProcessorCallbacks(JSContext* ctx, JSValueConst val)
        : initialize(ctx, val, "initialize"), uninitialize(ctx, val, "uninitialize"), process(ctx, val, "process"), reset(ctx, val, "reset"), tailTime(ctx, val, "tailTime"),
          latencyTime(ctx, val, "latencyTime"){};

    ObjectRef initialize, uninitialize, process, reset, tailTime, latencyTime;
  } m_callbacks;

public:
  AudioProcessor(JSContext* ctx, JSValueConst val) : m_callbacks(ctx, val), lab::AudioProcessor() {}

  virtual ~AudioProcessor() override {
    if(isInitialized())
      uninitialize();
  }

  // Full initialization can be done here instead of in the constructor.
  virtual void
  initialize() override {
    js_call<void>(m_callbacks.initialize);

    m_initialized = true;
  };

  virtual void
  uninitialize() override {
    js_call<void>(m_callbacks.uninitialize);

    m_initialized = false;
  };

  // Processes the source to destination bus.
  // The number of channels must match in source and destination.
  virtual void
  process(lab::ContextRenderLock&, const lab::AudioBus* source, lab::AudioBus* destination, int bufferSize) override {
    js_call<void>(m_callbacks.process);
  };

  // Resets filter state
  virtual void
  reset() override {
    js_call<void>(m_callbacks.reset);
  };

  virtual double
  tailTime(lab::ContextRenderLock& r) const override {
    return js_call<double>(m_callbacks.tailTime);
  };

  virtual double
  latencyTime(lab::ContextRenderLock& r) const override {
    return js_call<double>(m_callbacks.latencyTime);
  };
};

typedef shared_ptr<lab::AudioBus> AudioBufferPtr;
typedef ClassPtr<lab::AudioContext, void> AudioContextPtr;
typedef ClassPtr<lab::AudioDestinationNode, shared_ptr<lab::AudioContext>> AudioDestinationNodePtr;
typedef shared_ptr<lab::AudioListener> AudioListenerPtr;
typedef shared_ptr<lab::AudioDevice> AudioDevicePtr;
typedef ClassPtr<lab::AudioParam, shared_ptr<lab::AudioParamDescriptor>> AudioParamPtr;
typedef ClassPtr<lab::AudioSetting, shared_ptr<lab::AudioSettingDescriptor>> AudioSettingPtr;
typedef shared_ptr<lab::AudioSummingJunction> AudioSummingJunctionPtr;
typedef shared_ptr<lab::AudioNodeInput> AudioNodeInputPtr;
typedef shared_ptr<lab::AudioNodeOutput> AudioNodeOutputPtr;
typedef ClassPtr<lab::AudioNode, shared_ptr<lab::AudioContext>> AudioNodePtr;
typedef ClassPtr<lab::AudioScheduledSourceNode, shared_ptr<lab::AudioContext>> AudioScheduledSourceNodePtr;
typedef ClassPtr<lab::OscillatorNode, shared_ptr<lab::AudioContext>> OscillatorNodePtr;
typedef shared_ptr<lab::SampledAudioNode> AudioBufferSourceNodePtr;
typedef shared_ptr<lab::PeriodicWave> PeriodicWavePtr;
typedef shared_ptr<lab::AudioProcessor> AudioProcessorPtr;

typedef ClassPtr<lab::AudioBus, int> AudioChannelPtr;

typedef vector<JSObjectPtr> JSObjectArray;

template<class T> struct std::less<weak_ptr<T>> {
  bool
  operator()(const weak_ptr<T>& p1, const weak_ptr<T>& p2) const {
    shared_ptr<T> b1(p1);
    shared_ptr<T> b2(p2);

    return b1.get() < b2.get();
  }
};

#endif // defined(QUICKJS_LABSOUND_HPP)
