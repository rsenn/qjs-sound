// labsound-disconnect-deferred: deferred disconnect wipes later connect
// BUGS canonical name: labsound-disconnect-deferred-wipes-later-connect
// Repro: OfflineAudioContext, connect osc->gain->destination, disconnect gain,
// connect again -> gain.connect(destination) is wiped by the pending disconnect
// and the output goes silent. Test renders quanta and checks the peak is nonzero.
//
// Uses the lab::AudioContext offline path directly (no QuickJS, no binding):
// the binding's own disconnect_now() workaround is a workaround for this engine
// bug, so the test exercises the engine underneath it.

#include "LabSound/core/AudioBus.h"
#include "LabSound/core/AudioContext.h"
#include "LabSound/core/AudioDevice.h"
#include "LabSound/core/GainNode.h"
#include "LabSound/core/OscillatorNode.h"

#include <iostream>
#include <memory>

int main() {
    using namespace lab;

    // Offline context: AudioDevice_Null so no real audio hardware is needed.
    auto ac = std::make_shared<AudioContext>(/* isOffline */ true,
                                             /* autoDispatchEvents */ true);

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

    // osc -> gain -> destination
    ac->connect(dest, gain, 0, 0);
    ac->connect(gain, osc, 0, 0);
    osc->start(0.0f);

    // Engine-level: disconnect gain from the graph, then reconnect it.
    // The bug: disconnect() enqueues a Disconnect that is applied a tick later,
    // so a Connect enqueued right after is wiped and the pass-through gain stays
    // stopped -> the destination receives zeros.
    ac->disconnect(gain);
    ac->connect(dest, gain, 0, 0);

    const int quantum = AudioNode::ProcessingSizeInFrames;
    const int numQuanta = 16;
    auto scratch = std::make_shared<AudioBus>(1, quantum, true);
    float peak = 0.0f;

    for(int q = 0; q < numQuanta; ++q) {
        dest->offlineRender(scratch.get(), quantum);
        const float* data = scratch->channel(0)->data();
        for(int i = 0; i < quantum; ++i) {
            float v = std::abs(data[i]);
            if(v > peak) peak = v;
        }
    }

    if(peak > 0.001f) {
        std::cout << "PASS: peak=" << peak
                  << " (reconnect survived the deferred disconnect)\n";
        return 0;
    } else {
        std::cout << "FAIL: peak=" << peak
                  << " (reconnect was wiped by the deferred disconnect; output silent)\n";
        return 1;
    }
}