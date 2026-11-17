// labsound-functionnode-offline-silent: FunctionNode renders offline
// BUGS canonical name: labsound-functionnode-offline-silent
// Repro: offline context, FunctionNode (first sample 1.f) -> GainNode ->
// destination, start(0), render 8 quanta. The destination must receive the
// function's output. Engine only, no QuickJS and no binding.

#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioContext.h"
#include "LabSound/core/AudioDevice.h"
#include "LabSound/core/GainNode.h"
#include "LabSound/extended/FunctionNode.h"

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

    int calls = 0;
    auto fn = std::make_shared<FunctionNode>(*ac.get(), 1);
    fn->setFunction([&calls](ContextRenderLock&, FunctionNode*, int, float* buf, int n) {
        ++calls;
        for(int i = 0; i < n; ++i) buf[i] = i == 0 ? 1.f : 0.f;
    });
    auto gain = std::make_shared<GainNode>(*ac.get());
    gain->gain()->setValue(0.5f);

    ac->connect(dest, gain, 0, 0);
    ac->connect(gain, fn, 0, 0);
    fn->start(0.0f);

    const int quantum = AudioNode::ProcessingSizeInFrames;
    auto scratch = std::make_shared<AudioBus>(1, quantum, true);
    float peak = 0.f;
    for(int q = 0; q < 8; ++q) {
        dest->offlineRender(scratch.get(), quantum);
        for(int i = 0; i < quantum; ++i)
            peak = std::max(peak, std::abs(scratch->channel(0)->data()[i]));
    }

    bool ok = calls > 0 && peak > 0.001f;
    std::cout << (ok ? "PASS" : "FAIL") << ": calls=" << calls << " peak=" << peak << "\n";
    return ok ? 0 : 1;
}
