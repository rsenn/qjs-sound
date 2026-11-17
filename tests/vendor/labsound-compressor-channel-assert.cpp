// labsound-compressor-channel-assert: DynamicsCompressorNode channel count
// BUGS canonical name: labsound-compressor-channel-assert
// Repro: sources of 1 and 2 channels through a compressor into a 1 or 2
// channel destination must render without tripping the output channel
// assertion and must pass audio through. Engine only, no QuickJS.

#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioContext.h"
#include "LabSound/core/AudioDevice.h"
#include "LabSound/core/DynamicsCompressorNode.h"
#include "LabSound/core/GainNode.h"
#include "LabSound/core/OscillatorNode.h"
#include "LabSound/core/StereoPannerNode.h"

#include <cmath>
#include <iostream>
#include <memory>

static float run(int destChannels, bool stereoSource) {
    using namespace lab;
    auto ac = std::make_shared<AudioContext>(true, true);
    AudioStreamConfig inCfg, outCfg;
    outCfg.device_index = -1;
    outCfg.desired_channels = destChannels;
    outCfg.desired_samplerate = 44100;
    auto device = std::make_shared<AudioDevice_Null>(inCfg, outCfg);
    auto dest = std::make_shared<AudioDestinationNode>(*ac, device);
    device->setDestinationNode(dest);
    ac->setDestinationNode(dest);

    auto osc = std::make_shared<OscillatorNode>(*ac.get());
    osc->frequency()->setValue(440.f);
    osc->setType(SINE);
    auto comp = std::make_shared<DynamicsCompressorNode>(*ac.get());
    std::shared_ptr<AudioNode> src = osc;
    if(stereoSource) {
        auto pan = std::make_shared<StereoPannerNode>(*ac.get());
        ac->connect(pan, osc, 0, 0);
        src = pan;
    }
    ac->connect(comp, src, 0, 0);
    ac->connect(dest, comp, 0, 0);
    osc->start(0.0f);

    const int quantum = AudioNode::ProcessingSizeInFrames;
    auto scratch = std::make_shared<AudioBus>(destChannels, quantum, true);
    float peak = 0.f;
    for(int q = 0; q < 64; ++q) {
        dest->offlineRender(scratch.get(), quantum);
        if(q >= 32)
            for(int c = 0; c < destChannels; ++c)
                for(int i = 0; i < quantum; ++i)
                    peak = std::max(peak, std::abs(scratch->channel(c)->data()[i]));
    }
    return peak;
}

int main() {
    int rc = 0;
    for(int dc : {1, 2})
        for(bool st : {false, true}) {
            float p = run(dc, st);
            bool ok = p > 0.01f;
            std::cout << (ok ? "PASS" : "FAIL") << ": dest=" << dc << " stereoSource=" << st
                      << " peak=" << p << "\n";
            if(!ok) rc = 1;
        }
    return rc;
}
