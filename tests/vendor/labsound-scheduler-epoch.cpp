// labsound-scheduler-epoch: start/stop times are absolute context time
// BUGS canonical name: labsound-scheduler-epoch-inconsistent-start-stop
// Repro: offline context, osc -> gain -> destination playing for 1 s, then
// osc->stop(currentTime + 0.1): the stop used to fire 1 s late because the
// scheduler added its own epoch to the time. Engine only, no QuickJS.

#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioContext.h"
#include "LabSound/core/AudioDevice.h"
#include "LabSound/core/GainNode.h"
#include "LabSound/core/OscillatorNode.h"

#include <cmath>
#include <iostream>
#include <memory>

int main() {
    using namespace lab;

    auto ac = std::make_shared<AudioContext>(true, true);
    AudioStreamConfig inCfg, outCfg;
    outCfg.device_index = -1;
    outCfg.desired_channels = 1;
    outCfg.desired_samplerate = 44100;
    auto device = std::make_shared<AudioDevice_Null>(inCfg, outCfg);
    auto dest = std::make_shared<AudioDestinationNode>(*ac, device);
    device->setDestinationNode(dest);
    ac->setDestinationNode(dest);

    auto osc = std::make_shared<OscillatorNode>(*ac.get());
    osc->frequency()->setValue(440.f);
    osc->setType(SINE);
    auto gain = std::make_shared<GainNode>(*ac.get());
    gain->gain()->setValue(0.5f);
    ac->connect(dest, gain, 0, 0);
    ac->connect(gain, osc, 0, 0);
    osc->start(0.0f);

    const int quantum = AudioNode::ProcessingSizeInFrames;
    const int perSecond = 44100 / quantum;
    auto scratch = std::make_shared<AudioBus>(1, quantum, true);

    for(int q = 0; q < perSecond; ++q)
        dest->offlineRender(scratch.get(), quantum);

    osc->stop(ac->currentTime() + 0.1);

    // 0.1 s of playing is left; the next 0.5 s must end silent
    float tail = 0.f;
    for(int q = 0; q < perSecond / 2; ++q) {
        dest->offlineRender(scratch.get(), quantum);
        if(q >= perSecond / 2 - 8)
            for(int i = 0; i < quantum; ++i)
                tail = std::max(tail, std::abs(scratch->channel(0)->data()[i]));
    }

    if(tail < 0.001f) {
        std::cout << "PASS: stopped on time, tail=" << tail << "\n";
        return 0;
    }
    std::cout << "FAIL: still playing after stop(now + 0.1), tail=" << tail << "\n";
    return 1;
}
