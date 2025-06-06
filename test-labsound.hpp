// SPDX-License-Identifier: BSD-2-Clause
// Copyright (C) 2015+, The LabSound Authors. All rights reserved.

#pragma once

#ifndef TEST_LABSOUND_HPP
#define TEST_LABSOUND_HPP

#include "LabSound/LabSound.h"
#include "LabSound/backends/AudioDevice_RtAudio.h"
#include "LabSound/extended/Util.h"

#include <iostream>
#include <utility>
#include <algorithm>
#include <stdexcept>
#include <vector>

// using namespace lab;

// Returns input, output
inline std::pair<lab::AudioStreamConfig, lab::AudioStreamConfig>
GetDefaultAudioDeviceConfiguration(const bool with_input = true) {
  const std::vector<lab::AudioDeviceInfo> audioDevices = lab::AudioDevice_RtAudio::MakeAudioDeviceList();

  lab::AudioDeviceInfo defaultOutputInfo, defaultInputInfo;

  for(auto& info : audioDevices) {
    if(info.is_default_output)
      defaultOutputInfo = info;

    if(info.is_default_input)
      defaultInputInfo = info;
  }

  lab::AudioStreamConfig outputConfig;

  if(defaultOutputInfo.index != -1) {
    outputConfig.device_index = defaultOutputInfo.index;
    outputConfig.desired_channels = std::min(uint32_t(2), defaultOutputInfo.num_output_channels);
    outputConfig.desired_samplerate = defaultOutputInfo.nominal_samplerate;
  }

  lab::AudioStreamConfig inputConfig;

  if(with_input) {
    if(defaultInputInfo.index != -1) {
      inputConfig.device_index = defaultInputInfo.index;
      inputConfig.desired_channels = std::min(uint32_t(1), defaultInputInfo.num_input_channels);
      inputConfig.desired_samplerate = defaultInputInfo.nominal_samplerate;
    } else {
      throw std::invalid_argument("the default audio input device was requested but none were found");
    }
  }

  // RtAudio doesn't support mismatched input and output rates.
  // this may be a pecularity of RtAudio, but for now, force an RtAudio
  // compatible configuration
  if(defaultOutputInfo.nominal_samplerate != defaultInputInfo.nominal_samplerate) {
    float min_rate = std::min(defaultOutputInfo.nominal_samplerate, defaultInputInfo.nominal_samplerate);

    inputConfig.desired_samplerate = min_rate;
    outputConfig.desired_samplerate = min_rate;

    std::cout << "Warning ~ input and output sample rates don't match, attempting to set minimum" << std::endl;
  }

  return {inputConfig, outputConfig};
}

#endif
