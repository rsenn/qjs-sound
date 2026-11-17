// labsound-stopped-source-stuck-stopping: a cut source finishes stopping
// BUGS canonical name: labsound-stopped-source-stuck-stopping
// Repro: osc -> gain -> destination; osc->stop() reaches STOPPING, and the
// source is cut from the graph before the next quantum pulls it. It is never
// pulled again, so it stays STOPPING instead of becoming UNSCHEDULED.
// Engine only, no QuickJS.

#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioContext.h"
#include "LabSound/core/AudioNodeOutput.h"
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

    for(int q = 0; q < 8; ++q)
        dest->offlineRender(scratch.get(), quantum);

    osc->stop(ac->currentTime() + 0.01);

    int guard = 0;
    while(osc->schedulingState() != SchedulingState::STOPPING && ++guard < 64)
        dest->offlineRender(scratch.get(), quantum);
    if(osc->schedulingState() != SchedulingState::STOPPING) {
        std::cout << "FAIL: osc never reached STOPPING\n";
        return 1;
    }

    {   // cut the stopping source out of the graph, as an immediate disconnect does
        ContextRenderLock rLock(ac.get(), "test");
        ac->handlePreRenderTasks(rLock);
        ContextGraphLock gLock(ac.get(), "test");
        for(int i = 0; i < osc->numberOfOutputs(); ++i)
            AudioNodeOutput::disconnectAll(gLock, osc->output(i));
    }

    for(int q = 0; q < 16; ++q)
        dest->offlineRender(scratch.get(), quantum);

    auto st = osc->schedulingState();
    if(st == SchedulingState::UNSCHEDULED || st == SchedulingState::FINISHED) {
        std::cout << "PASS: stopped source ended, state=" << schedulingStateName(st) << "\n";
        return 0;
    }
    std::cout << "FAIL: stopped source stuck in " << schedulingStateName(st) << "\n";
    return 1;
}
