#include <memory>
#include <thread>

#include "test-labsound.hpp"
#include "LabSound/core/GainNode.h"

using lab::GainNode;
using lab::OscillatorNode;
using lab::OscillatorType;

int
main(int argc, char* argv[]) {
  AudioStreamConfig _inputConfig;
  AudioStreamConfig _outputConfig;
  auto config = GetDefaultAudioDeviceConfiguration(true);
  _inputConfig = config.first;
  _outputConfig = config.second;
  std::shared_ptr<lab::AudioDevice_RtAudio> device(
      new lab::AudioDevice_RtAudio(_inputConfig, _outputConfig));
  auto context = std::make_shared<lab::AudioContext>(false, true);
  auto destinationNode = std::make_shared<lab::AudioDestinationNode>(*context.get(), device);
  device->setDestinationNode(destinationNode);
  context->setDestinationNode(destinationNode);

  auto oscillator = std::make_shared<OscillatorNode>(*context.get());
  auto gain = std::make_shared<GainNode>(*context.get());
  gain->gain()->setValue(0.0625f);

  // osc -> gain -> destination
  context->connect(gain, oscillator, 0, 0);
  context->connect(context->destinationNode(), gain, 0, 0);

  oscillator->frequency()->setValue(440.f);
  oscillator->setType(OscillatorType::SINE);
  oscillator->start(0.0f);
  std::this_thread::sleep_for(std::chrono::seconds(1));
  return 0;
}
